#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "compiler.h"
#include "dialect.h"
#include "mlir/IR/Builders.h"
#include "nlohmann/json.hpp"
#include "tools.h"
#include <fcntl.h>
#include <fstream>
#include <map>
#include <regex>
#include <set>

namespace splice_ir {
namespace {
using Json = nlohmann::json;
using Args = std::vector<std::string>;
constexpr std::string_view kOutput = R"("$out")";
constexpr std::string_view kStore = "/nix/store/";
constexpr std::string_view kBuild = "/build/";
const std::regex scratchName("[A-Za-z0-9][A-Za-z0-9._+-]*");

struct Command {
  Args argv;
  std::string source;
  std::string path;
  int flags;
  uint64_t step = 0;
};

void require(bool condition, std::string_view reason) {
  if (!condition) {
    throw std::runtime_error(std::string(reason));
  }
}

struct Script {
  Args args;
  std::set<std::string> sources;
  std::vector<Command> commands;
};

Script readBuilder(const std::filesystem::path &path) {
  std::ifstream file(path);
  const auto document = Json::parse(file);
  const auto &derivations = document.at("derivations");
  require(document.at("version") == 4 && derivations.size() == 1,
          "expected a single Nix format 4 derivation");
  const auto &drv = *derivations.begin();
  const auto &env = drv.at("env");
  const std::string output = env.at("out");
  const auto expectedEnv = Json{{"out", output},
                                {"builder", kBash},
                                {"system", "x86_64-linux"},
                                {"name", drv.at("name")}};
  require(env == expectedEnv && drv.at("builder") == kBash &&
              drv.at("system") == "x86_64-linux",
          "expected the pinned Bash builder and its standard environment");
  require(
      drv.at("outputs").size() == 1 &&
          drv.at("outputs").at("out").size() == 1 &&
          output ==
              absl::StrCat(
                  kStore,
                  drv.at("outputs").at("out").at("path").get<std::string>()),
      "expected one input-addressed out path");

  Script script;
  script.args = drv.at("args").get<Args>();
  require(script.args.size() == 3 && script.args[0] == "-eu" &&
              script.args[1] == "-c",
          "expected bash -eu -c with an inline script");
  for (const std::string source : drv.at("inputs").at("srcs")) {
    script.sources.insert(absl::StrCat(kStore, source));
  }

  // Accept whole commands before interpreting any filesystem effects.
  for (const std::string_view line :
       absl::StrSplit(script.args.back(), '\n', absl::SkipEmpty())) {
    Args argv = absl::StrSplit(line, ' ');
    constexpr size_t copyArgs = 4;
    constexpr size_t appendArgs = 6;
    const bool append = argv.size() == appendArgs;
    require((append || argv.size() == copyArgs) && argv.front() == kDd &&
                argv[1].starts_with("if=") && argv[2].starts_with("of=") &&
                argv.back() == "status=none",
            "unsupported builder command");
    require(!append || (argv[3] == "oflag=append" && argv[4] == "conv=notrunc"),
            "unsupported dd flags");
    const std::string src = argv[1].substr(3);
    const std::string dst = argv[2].substr(3);
    require(
        (script.sources.contains(src) || std::regex_match(src, scratchName)) &&
            (dst == kOutput || std::regex_match(dst, scratchName)) &&
            src != dst,
        "unsupported input or output path");
    const auto target = dst == kOutput ? output : absl::StrCat(kBuild, dst);
    if (dst == kOutput) {
      argv[2] = absl::StrCat("of=", output);
    }
    script.commands.push_back(
        {std::move(argv),
         script.sources.contains(src) ? src : absl::StrCat(kBuild, src), target,
         O_CREAT | O_WRONLY | (append ? O_APPEND : O_TRUNC)});
  }
  require(!script.commands.empty() && script.commands.back().path == output &&
              std::ranges::count(script.commands, output, &Command::path) == 1,
          "expected exactly one final write to $out");
  return script;
}

enum class Expect { Builder, Command, Open, Exit, Done };

void matchTrace(Script &script, const std::filesystem::path &path) {
  std::ifstream file(path);
  require(file.is_open(), "cannot open Rewind events");
  Expect next = Expect::Builder;
  size_t index = 0;
  int pid = 0;
  int builderPid = 0;
  uint64_t previous = 0;
  for (std::string line; std::getline(file, line);) {
    const auto event = Json::parse(line);
    const uint64_t step = event.at("step");
    require(step >= previous, "out-of-order Rewind events");
    previous = step;
    const auto &kind = event.at("kind");
    const auto &type = kind.at("type");
    const int sender = event.at("pid");

    // Boot and shutdown have their own execs and device opens.
    if (sender == 1) {
      require(type != "exec" ||
                  (next == Expect::Builder && kind.at("filename") == "/init"),
              "unexpected init exec");
      continue;
    }
    if (type == "exec" && next == Expect::Builder) {
      Args argv = script.args;
      argv.insert(argv.begin(), "bash");
      require(kind.at("filename") == kBash && kind.at("argv") == argv,
              "builder exec differs from derivation (possibly truncated argv)");
      next = Expect::Command;
      builderPid = sender;
      continue;
    }
    if (type == "exec") {
      require(next == Expect::Command, "unexpected exec or missing child exit");
      auto &cmd = script.commands.at(index);
      require(kind.at("filename") == kDd && kind.at("argv") == cmd.argv,
              "dd exec differs from script");
      cmd.step = step;
      pid = sender;
      next = Expect::Open;
      continue;
    }
    if (type == "open") {
      require(next == Expect::Open && sender == pid, "unexpected write-open");
      const auto &cmd = script.commands.at(index);
      require(kind.at("path") == cmd.path && kind.at("flags") == cmd.flags,
              "recorded write path or flags differ from dd command");
      next = Expect::Exit;
      continue;
    }
    if (type == "exit" && sender == pid) {
      require(next == Expect::Exit && kind.at("status") == 0,
              "missing write-open or failed child");
      next = ++index == script.commands.size() ? Expect::Done : Expect::Command;
      pid = 0;
      continue;
    }
    if (type == "exit" && sender == builderPid) {
      require(next == Expect::Done && kind.at("status") == 0,
              "failed or incomplete builder");
    }
    require(type != "unlink" && type != "rename" && type != "unknown",
            "unsupported filesystem event");
  }
  require(file.eof() && next == Expect::Done, "incomplete Rewind events");
}
} // namespace

mlir::OwningOpRef<mlir::ModuleOp>
importTrace(mlir::MLIRContext &context, const std::filesystem::path &drv,
            const std::filesystem::path &events) {
  auto script = readBuilder(drv);
  matchTrace(script, events);
  mlir::OpBuilder builder(&context);
  auto loc = builder.getUnknownLoc();
  mlir::OwningOpRef module(mlir::ModuleOp::create(loc));
  builder.setInsertionPointToStart(module->getBody());
  const auto file = FileType::get(&context);

  // Bind the reads before installing a new value at the destination.
  std::map<std::string, mlir::Value> latest;
  std::map<std::string, uint64_t> versions;
  for (const auto &cmd : script.commands) {
    if (!latest.contains(cmd.source) && script.sources.contains(cmd.source)) {
      latest[cmd.source] =
          SourceOp::create(builder, loc, file, cmd.source).getFile();
    }
    require(latest.contains(cmd.source),
            "read of a scratch file before its first write");
    auto input = latest.at(cmd.source);
    auto version = ++versions[cmd.path];
    if ((cmd.flags & O_APPEND) && latest.contains(cmd.path)) {
      latest[cmd.path] =
          AppendOp::create(builder, loc, file, latest.at(cmd.path), input,
                           cmd.path, cmd.step, version)
              .getFile();
      continue;
    }
    latest[cmd.path] =
        CopyOp::create(builder, loc, file, input, cmd.path, cmd.step, version)
            .getFile();
  }
  PublishOp::create(builder, loc, latest.at(script.commands.back().path));
  return module;
}
} // namespace splice_ir

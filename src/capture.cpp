#include "absl/strings/str_split.h"
#include "dialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Verifier.h"
#include "nlohmann/json.hpp"
#include "package.h"
#include "process.h"
#include "tools.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
using Json = nlohmann::json;
const fs::path store = "/nix/store";
struct Cut {
  std::string name, program;
  uint64_t step, pid, end = 0;
};

std::string selectedAction(const Json &event) {
  const fs::path program(event.at("filename").get<std::string>());
  const auto argv = event.at("argv").get<Arguments>();
  if (program.filename() == "g++" && !argv.empty() && argv.front() == program) {
    return "compile";
  }
  if (program.filename() == "install" && argv.size() == 4 &&
      argv[1] == "-Dm755") {
    return "install";
  }
  if (program.filename() == "patchelf" && argv.size() == 3 &&
      argv[1] == "--shrink-rpath") {
    return "patchelf";
  }
  if (program.filename() == "strip" && !argv.empty() &&
      argv.front() == program) {
    return "strip";
  }
  return {};
}

std::vector<Cut> boundaries(const fs::path &events) {
  std::ifstream stream(events);
  std::vector<Cut> cuts;
  for (std::string line; std::getline(stream, line);) {
    const auto event = Json::parse(line);
    const auto &kind = event.at("kind");
    if (kind.at("type") == "exec") {
      auto name = selectedAction(kind);
      if (!name.empty()) {
        check(cuts.empty() || cuts.back().end, "overlapping package actions");
        cuts.push_back(
            {name, kind.at("filename"), event.at("step"), event.at("pid")});
      }
      continue;
    }
    if (!cuts.empty() && !cuts.back().end && kind.at("type") == "exit" &&
        event.at("pid") == cuts.back().pid) {
      check(kind.at("status") == 0, "recorded package action failed");
      cuts.back().end = event.at("step");
    }
  }
  Arguments names;
  for (const auto &cut : cuts) {
    check(cut.end > cut.step, "missing successful process exit");
    names.push_back(cut.name);
  }
  check(names == Arguments{"compile", "install", "patchelf", "strip"},
        "expected a GCC link, one install, shrink-rpath and strip");
  return cuts;
}

Arguments closure(const Json &drv, const fs::path &temp) {
  const auto &inputs = drv.at("inputs");
  Arguments query{std::string(kNixStore), "--query", "--requisites"};
  for (const auto &[name, dep] : inputs.at("drvs").items()) {
    for (const std::string output : dep.at("outputs")) {
      execute({std::string(kNixStore), "--query", "--binding", output,
               (store / name).string()},
              {}, temp);
      auto root = readBytes(temp);
      check(root.ends_with('\n'), "missing input store path");
      root.pop_back();
      query.push_back(root);
    }
  }
  for (const std::string src : inputs.at("srcs")) {
    query.push_back((store / src).string());
  }
  execute(query, {}, temp);
  const auto bytes = readBytes(temp);
  return absl::StrSplit(bytes, '\n', absl::SkipEmpty());
}

// /proc carries NUL-delimited argv and envp, including empty arguments.
Arguments fields(const fs::path &file) {
  auto data = readBytes(file);
  check(!data.empty() && data.back() == '\0', "incomplete process record");
  data.pop_back();
  return absl::StrSplit(data, '\0');
}

Json inspect(const Cut &cut, const std::string &run, const fs::path &temp) {
  const std::string proc = "/proc/" + std::to_string(cut.pid) + "/";
  auto step = std::to_string(cut.step);
  execute({"rewind", "cat", run, step, proc + "cmdline"}, {}, temp / "argv");
  execute({"rewind", "cat", run, step, proc + "environ"}, {}, temp / "env");
  // Archive the proc link itself to recover the kernel's cwd independently of
  // PWD.
  execute({"rewind", "cat", "--archive", "tar", run, step, proc + "cwd"}, {},
          temp / "cwd.tar");
  fs::remove(temp / "cwd");
  execute({std::string(kTar), "-xf", (temp / "cwd.tar").string(), "-C",
           temp.string(), "cwd"});
  return Json{{"action", cut.name},
              {"program", cut.program},
              {"argv", fields(temp / "argv")},
              {"env", fields(temp / "env")},
              {"cwd", fs::read_symlink(temp / "cwd").string()},
              {"step", cut.step},
              {"exit", cut.end}};
}

void writePlan(const fs::path &bundle, const std::vector<Cut> &cuts) {
  mlir::MLIRContext context;
  context.getOrLoadDialect<SpliceDialect>();
  mlir::OpBuilder builder(&context);
  const auto loc = builder.getUnknownLoc();
  mlir::OwningOpRef module(mlir::ModuleOp::create(loc));
  builder.setInsertionPointToStart(module->getBody());
  const auto nar = NarType::get(&context);
  mlir::Value tree = SeedOp::create(builder, loc, nar, "source.nar").getTree();
  for (const auto &cut : cuts) {
    tree =
        ExecOp::create(builder, loc, nar, tree, cut.name, cut.step).getTree();
  }
  PackageOp::create(builder, loc, tree);
  check(mlir::succeeded(mlir::verify(*module)), "invalid package IR");
  std::error_code ec;
  llvm::raw_fd_ostream output((bundle / "package.mlir").string(), ec);
  if (ec) {
    throw std::system_error(ec);
  }
  module->print(output);
  output << "\n";
  output.close();
  check(!output.has_error(), "failed to write package.mlir");
}
} // namespace

void capturePackage(const fs::path &reportPath, const fs::path &drvPath,
                    const fs::path &bundle) {
  Scratch scratch;
  const auto temp = scratch.path / "result";
  const auto report = Json::parse(readBytes(reportPath));
  check(report.at("ending") == "exited:0" && report.at("outputs").size() == 1,
        "expected one output from a successful Nix recording");
  const std::string run = report.at("id");
  execute({"rewind", "show", "--json", run}, {}, temp);
  const auto show = Json::parse(readBytes(temp));
  const auto command = show.at("commands").at(0).at("args").get<Arguments>();
  check(show.at("id") == run && command.size() >= 2 && command[0] == "nix" &&
            command[1] == drvPath,
        "recording does not belong to this derivation");
  execute({std::string(kNix), "derivation", "show", drvPath.string()}, {},
          temp);
  const auto drv = Json::parse(readBytes(temp))
                       .at("derivations")
                       .at(drvPath.filename().string());
  const std::string out = drv.at("env").at("out");
  check(report.at("outputs").at(0).at("path") == out &&
            drv.at("outputs").size() == 1,
        "output differs between derivation and recording");
  const auto inputs = closure(drv, temp);
  execute({"rewind", "events", "--json", run}, {}, temp);
  const auto cuts = boundaries(temp);
  std::vector<Json> specs;
  for (const auto &cut : cuts) {
    specs.push_back(inspect(cut, run, scratch.path));
  }

  // Bind the compiler result to the input named by install, and the directory
  // versions to the same installed program throughout fixup.
  const auto &install = specs.at(1);
  const auto installArgs = install.at("argv").get<Arguments>();
  check(installArgs.size() == 4 && installArgs[1] == "-Dm755",
        "unsupported install arguments");
  const fs::path binary =
      fs::path(install.at("cwd").get<std::string>()) / installArgs[2];
  const fs::path installed = installArgs[3];
  const auto src = drv.at("env").at("src").get<std::string>();
  check(!installArgs[2].empty() &&
            fs::path(installArgs[2]).filename() == installArgs[2] &&
            installed.parent_path() == fs::path(out) / "bin" &&
            std::ranges::find(inputs, src) != inputs.end(),
        "unsupported package layout");
  for (size_t i = 0; i < specs.size(); ++i) {
    auto &spec = specs[i];
    if (cuts[i].name == "patchelf" || cuts[i].name == "strip") {
      check(spec.at("argv").back() == installed.string(),
            "fixup targets another file");
    }
    spec.update(Json{{"out", out},
                     {"closure", inputs},
                     {"binary", binary.string()},
                     {"installed", installed.string()},
                     {"run", run}});
    execute({"rewind", "cat", run, std::to_string(cuts[i].end),
             (i == 0 ? binary : installed).string()},
            {}, temp);
    spec["fileHash"] = fileHash(temp);
  }
  specs.back()["narHash"] = report.at("outputs").at(0).at("hash");

  // Only source bytes and invocation metadata become build inputs.
  check(fs::create_directory(bundle), "bundle destination already exists");
  try {
    execute({std::string(kNixStore), "--dump", src}, {}, bundle / "source.nar");
    for (const auto &spec : specs) {
      std::ofstream file;
      file.exceptions(std::ios::badbit | std::ios::failbit);
      file.open(bundle / (spec.at("action").get<std::string>() + ".json"));
      file << spec.dump(2) << '\n';
      file.close();
    }
    writePlan(bundle, cuts);
  } catch (...) {
    fs::remove_all(bundle);
    throw;
  }
}
} // namespace splice_ir

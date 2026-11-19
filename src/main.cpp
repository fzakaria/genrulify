#include "access.h"
#include "compiler.h"
#include "dialect.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "rules.h"
#include "action.h"
#include "trace.h"
#include <iostream>
#include <set>
#include <span>
#include <stdexcept>

namespace {
constexpr std::string_view kUsage =
    "usage: genrulify convert [--coarse] [--open PROGRAMS] [--source PROJECT] INSTALLABLE DIRECTORY\n"
    "       genrulify record [--coarse] [--source PROJECT] RUN DERIVATION.drv RECORDING [OPEN]\n"
    "       genrulify groups RECORDING\n"
    "       genrulify origins RECORDING GRAPH.mlir\n"
    "       genrulify same-source TREE DIRECTORY\n"
    "       genrulify cuts [--coarse] [--close PID@STEP]... EVENTS.jsonl TRACE.bin [OPEN]\n"
    "       genrulify accessed STRACE-DIRECTORY\n"
    "       genrulify check RECORDING [PASS...]\n"
    "       genrulify learn RECORDING < BUILD.log\n"
    "       genrulify import RECORDING\n"
    "       genrulify opt [--recording RECORDING] GRAPH.mlir [PASS...]\n"
    "       genrulify rule PROGRAM [ARGUMENT...]\n"
    "       genrulify emit RECORDING GRAPH.mlir WORKSPACE\n"
    "       genrulify describe RECORDING RECORDED.mlir\n"
    "       genrulify reference RECORDING OUTPUT ARCHIVE.nar\n"
    "       genrulify verify REFERENCE.nar ARCHIVE.nar\n"
    "\n"
    "PASS is one of link, dce, fold, merge, raise and rules. With none\n"
    "named, all run. The rules pass reads command lines, so it needs the\n"
    "recording and does nothing without one.\n"
    "OPEN is a regular expression over the names of programs to treat as\n"
    "driver code. Programs that only run other commands are found without\n"
    "being named, unless --coarse is given.\n";

// The option that stops the cuts at the commands the builder runs, and the
// option that keeps one image from being opened.
constexpr std::string_view kCoarseOption = "--coarse";
constexpr std::string_view kCloseOption = "--close";

// The option of `convert` that names programs to treat as driver code.
constexpr std::string_view kOpenOption = "--open";

// The option of `convert` and `record` that names the directory of a
// project to convert in place.
constexpr std::string_view kSourceOption = "--source";

// The option of `opt` that names the recording a graph was imported from.
constexpr std::string_view kRecordingOption = "--recording";

// The separator between the process and the step of an image.
constexpr char kImageSeparator = '@';

// The passes named on a command line, or every pass when none is named.
std::vector<splice_ir::Pass> passesFrom(std::span<char *> names) {
  if (names.empty()) {
    return {splice_ir::kAllPasses.begin(), splice_ir::kAllPasses.end()};
  }
  std::vector<splice_ir::Pass> passes;
  for (const std::string_view name : names) {
    const auto pass = splice_ir::passNamed(name);
    if (!pass) {
      throw std::runtime_error("unknown pass " + std::string(name));
    }
    passes.push_back(*pass);
  }
  return passes;
}

mlir::OwningOpRef<mlir::ModuleOp> parse(mlir::MLIRContext &context,
                                        const char *file) {
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(file, &context);
  if (!module) {
    throw std::runtime_error("cannot parse module");
  }
  return module;
}

void print(mlir::ModuleOp module) {
  if (mlir::failed(mlir::verify(module))) {
    throw std::runtime_error("invalid module");
  }
  module.print(llvm::outs());
  llvm::outs() << "\n";
}

void run(std::span<char *> args) {
  const std::string_view command = args.empty() ? "" : args[0];

  // Take the granularity option out of the arguments of the two commands
  // that cut a run.
  auto granularity = splice_ir::Granularity::Fine;
  std::vector<char *> rest(args.begin(), args.end());
  if (rest.size() > 1 && rest[1] == kCoarseOption) {
    granularity = splice_ir::Granularity::Coarse;
    rest.erase(rest.begin() + 1);
    args = rest;
  }

  // Take the images to close, written PID@STEP, out as well.
  std::set<splice_ir::Image> closed;
  while (rest.size() > 2 && rest[1] == kCloseOption) {
    const std::string image = rest[2];
    const auto separator = image.find(kImageSeparator);
    if (separator == std::string::npos) {
      throw std::runtime_error("expected PID@STEP, found " + image);
    }
    closed.insert({std::stoi(image.substr(0, separator)),
                   std::stoull(image.substr(separator + 1))});
    rest.erase(rest.begin() + 1, rest.begin() + 3);
    args = rest;
  }

  // Takes the project directory out of the arguments of the commands that
  // can convert in place.
  const auto takeProject = [&]() -> std::optional<std::filesystem::path> {
    if (rest.size() <= 2 || rest[1] != kSourceOption) {
      return std::nullopt;
    }
    const std::filesystem::path project = rest[2];
    rest.erase(rest.begin() + 1, rest.begin() + 3);
    args = rest;
    return project;
  };

  // The whole pipeline, from a derivation to a workspace.
  if (command == "convert") {
    std::string open;
    if (rest.size() > 2 && rest[1] == kOpenOption) {
      open = rest[2];
      rest.erase(rest.begin() + 1, rest.begin() + 3);
      args = rest;
    }
    const auto project = takeProject();
    if (args.size() != 3) {
      throw std::runtime_error(std::string(kUsage));
    }
    splice_ir::convert(args[1], args[2], open, granularity, project);
    return;
  }

  // Whether a directory still holds the source tree a derivation was
  // evaluated with.
  if (args.size() == 3 && command == "same-source") {
    splice_ir::checkSameSource(args[1], args[2]);
    return;
  }

  // Commands that only need a recording.
  if (command == "record") {
    const auto project = takeProject();
    if (args.size() != 4 && args.size() != 5) {
      throw std::runtime_error(std::string(kUsage));
    }
    splice_ir::recordRun(args[1], args[2], args[3],
                         args.size() == 5 ? args[4] : "", granularity, project);
    return;
  }
  if (args.size() == 2 && command == "accessed") {
    // The paths a traced replay touched, with a trailing slash on the
    // directories it listed.
    const auto accessed = splice_ir::readAccesses(args[1]);
    for (const auto &path : accessed.paths) {
      if (!accessed.listed.contains(path)) {
        llvm::outs() << path << "\n";
      }
    }
    for (const auto &path : accessed.listed) {
      llvm::outs() << path << "/\n";
    }
    return;
  }
  if ((args.size() == 3 || args.size() == 4) && command == "cuts") {
    // One JSON object per cut, for inspecting a run before recording it.
    const std::string open = args.size() == 4 ? args[3] : "";
    for (const auto &cut :
         splice_ir::analyze(args[1], args[2], open, granularity, closed).cuts) {
      nlohmann::json line = {{"pid", cut.pid},     {"step", cut.step},
                             {"end", cut.end},     {"status", cut.status},
                             {"name", cut.name},   {"reads", cut.reads}};
      if (cut.opener) {
        line["opener"] = {{"pid", cut.opener->pid}, {"step", cut.opener->step}};
      }
      llvm::outs() << line.dump() << "\n";
    }
    return;
  }
  if (args.size() == 2 && command == "groups") {
    // One line per group: its first and last step, then the step of each
    // process in the group. For seeing which commands a recording keeps
    // together.
    for (const auto &group :
         splice_ir::regroup(splice_ir::Recording::load(args[1]))) {
      llvm::outs() << group.start << " " << group.end;
      for (const auto &process : group.processes) {
        llvm::outs() << " " << process.step;
      }
      llvm::outs() << "\n";
    }
    return;
  }
  if (args.size() >= 2 && command == "check") {
    splice_ir::checkRecording(splice_ir::Recording::load(args[1]),
                              passesFrom(args.subspan(2)));
    return;
  }
  if (args.size() == 2 && command == "learn") {
    // The exit status tells a script whether rebuilding can help.
    if (!splice_ir::learnFromBuild(splice_ir::Recording::load(args[1]),
                                   std::cin)) {
      throw std::runtime_error("the build log names no rule to adjust");
    }
    return;
  }
  if (args.size() == 4 && command == "reference") {
    splice_ir::writeReference(splice_ir::Recording::load(args[1]), args[2],
                              args[3]);
    return;
  }
  if (args.size() == 3 && command == "verify") {
    splice_ir::verifyArchives(args[1], args[2]);
    return;
  }

  if (args.size() >= 2 && command == "rule") {
    // The kind of build rule a command line would be raised to, for
    // seeing what the rules pass makes of a command.
    splice_ir::Process process;
    process.filename = args[1];
    process.argv.assign(args.begin() + 1, args.end());
    if (const auto kind = splice_ir::ruleKind(process)) {
      llvm::outs() << *kind << "\n";
    }
    return;
  }

  // Commands that read or write the IR.
  mlir::MLIRContext context;
  context.getOrLoadDialect<splice_ir::SpliceDialect>();
  if (args.size() == 2 && command == "import") {
    const auto recording = splice_ir::Recording::load(args[1]);
    auto module = splice_ir::importRecording(context, recording,
                                             splice_ir::loadVerdicts(recording));
    print(*module);
    return;
  }
  if (args.size() >= 4 && command == "opt" && args[1] == kRecordingOption) {
    const auto recording = splice_ir::Recording::load(args[2]);
    auto module = parse(context, args[3]);
    splice_ir::optimize(*module, passesFrom(args.subspan(4)), &recording);
    print(*module);
    return;
  }
  if (args.size() >= 2 && command == "opt") {
    auto module = parse(context, args[1]);
    splice_ir::optimize(*module, passesFrom(args.subspan(2)));
    print(*module);
    return;
  }
  if (args.size() == 3 && command == "origins") {
    // Where the files of each output come from in a graph.
    auto module = parse(context, args[2]);
    const auto recording = splice_ir::Recording::load(args[1]);
    llvm::outs() << splice_ir::originsText(
                        splice_ir::outputOrigins(*module, recording))
                 << splice_ir::frozenText(
                        splice_ir::frozenFiles(*module, recording));
    return;
  }
  if (args.size() == 3 && command == "describe") {
    auto module = parse(context, args[2]);
    splice_ir::describeGraph(*module, splice_ir::Recording::load(args[1]));
    return;
  }
  if (args.size() == 4 && command == "emit") {
    auto module = parse(context, args[2]);
    splice_ir::emitWorkspace(*module, splice_ir::Recording::load(args[1]),
                             args[3]);
    return;
  }
  throw std::runtime_error(std::string(kUsage));
}
} // namespace

int main(int argc, char **argv) {
  try {
    run(std::span(argv + 1, argc - 1));
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    llvm::errs() << "genrulify: " << error.what() << "\n";
    return EXIT_FAILURE;
  }
}

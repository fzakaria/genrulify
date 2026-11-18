#include "access.h"
#include "compiler.h"
#include "dialect.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "plan.h"
#include "trace.h"
#include <iostream>
#include <span>
#include <stdexcept>

namespace {
constexpr std::string_view kUsage =
    "usage: genrulify record RUN DERIVATION.drv RECORDING [OPEN]\n"
    "       genrulify cuts EVENTS.jsonl TRACE.bin [OPEN]\n"
    "       genrulify accessed STRACE-DIRECTORY\n"
    "       genrulify check RECORDING\n"
    "       genrulify learn RECORDING < BUILD.log\n"
    "       genrulify import RECORDING\n"
    "       genrulify opt GRAPH.mlir\n"
    "       genrulify emit RECORDING GRAPH.mlir WORKSPACE\n"
    "       genrulify describe RECORDING GRAPH.mlir\n"
    "       genrulify reference RECORDING OUTPUT ARCHIVE.nar\n"
    "       genrulify verify REFERENCE.nar ARCHIVE.nar\n"
    "       genrulify replay PLAN.json RESULT.layer LAYER...\n"
    "       genrulify pack PLAN.json ARCHIVE.nar LAYER...\n";

// The number of arguments before the layer list of `replay` and `pack`:
// the command, the plan and the output.
constexpr size_t kPlanArguments = 3;

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

  // The two commands a generated genrule runs.
  if (args.size() >= kPlanArguments && (command == "replay" || command == "pack")) {
    const auto plan = splice_ir::loadPlan(args[1]);
    const std::vector<std::filesystem::path> layers(
        args.begin() + kPlanArguments, args.end());
    if (command == "replay") {
      splice_ir::replayPlan(plan, layers, args[2]);
      return;
    }
    splice_ir::packPlan(plan, layers, args[2]);
    return;
  }

  // Commands that only need a recording.
  if ((args.size() == 4 || args.size() == 5) && command == "record") {
    splice_ir::recordRun(args[1], args[2], args[3],
                         args.size() == 5 ? args[4] : "");
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
    for (const auto &cut : splice_ir::analyze(args[1], args[2], open).cuts) {
      const nlohmann::json line = {{"pid", cut.pid},     {"step", cut.step},
                                   {"end", cut.end},     {"status", cut.status},
                                   {"name", cut.name},   {"reads", cut.reads}};
      llvm::outs() << line.dump() << "\n";
    }
    return;
  }
  if (args.size() == 2 && command == "check") {
    splice_ir::checkRecording(splice_ir::Recording::load(args[1]));
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
  if (args.size() == 2 && command == "opt") {
    auto module = parse(context, args[1]);
    splice_ir::optimize(*module);
    print(*module);
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

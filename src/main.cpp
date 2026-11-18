#include "access.h"
#include "compiler.h"
#include "dialect.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "action.h"
#include "trace.h"
#include <iostream>
#include <span>
#include <stdexcept>

namespace {
constexpr std::string_view kUsage =
    "usage: genrulify record RUN DERIVATION.drv RECORDING [OPEN]\n"
    "       genrulify cuts EVENTS.jsonl TRACE.bin [OPEN]\n"
    "       genrulify accessed STRACE-DIRECTORY\n"
    "       genrulify check RECORDING [PASS...]\n"
    "       genrulify learn RECORDING < BUILD.log\n"
    "       genrulify import RECORDING\n"
    "       genrulify opt GRAPH.mlir [PASS...]\n"
    "       genrulify emit RECORDING GRAPH.mlir WORKSPACE\n"
    "       genrulify describe RECORDING RECORDED.mlir\n"
    "       genrulify reference RECORDING OUTPUT ARCHIVE.nar\n"
    "       genrulify verify REFERENCE.nar ARCHIVE.nar\n"
    "\n"
    "PASS is one of link, dce, fold, merge and raise. With none named, all\n"
    "run.\n";

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
  if (args.size() >= 2 && command == "opt") {
    auto module = parse(context, args[1]);
    splice_ir::optimize(*module, passesFrom(args.subspan(2)));
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

#include "compiler.h"
#include "dialect.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"
#include <span>
#include <stdexcept>

namespace {
constexpr std::string_view usage =
    "usage: nixsplice import derivation.json events.jsonl\n"
    "       nixsplice opt graph.mlir\n"
    "       nixsplice emit graph.mlir directory\n"
    "       nixsplice verify run.json reference.nar backend.nar\n";
void run(std::span<char *> args) {
  if (args.size() == 4 && std::string_view(args[0]) == "verify") {
    splice_ir::verifyArchives(args[1], args[2], args[3]);
    return;
  }
  mlir::MLIRContext context;
  context.getOrLoadDialect<splice_ir::SpliceDialect>();
  mlir::OwningOpRef<mlir::ModuleOp> module;
  if (args.size() == 3 && std::string_view(args[0]) == "import") {
    module = splice_ir::importTrace(context, args[1], args[2]);
  } else if (args.size() == 2 && std::string_view(args[0]) == "opt") {
    module = mlir::parseSourceFile<mlir::ModuleOp>(args[1], &context);
    mlir::PassManager passes(&context);
    passes.addPass(mlir::createCanonicalizerPass());
    if (!module || mlir::failed(passes.run(*module))) {
      throw std::runtime_error("cannot optimize module");
    }
  } else if (args.size() == 3 && std::string_view(args[0]) == "emit") {
    module = mlir::parseSourceFile<mlir::ModuleOp>(args[1], &context);
    if (!module) {
      throw std::runtime_error("cannot parse module");
    }
    splice_ir::emitWorkspace(*module, args[2]);
    return;
  } else {
    throw std::runtime_error(std::string(usage));
  }
  if (mlir::failed(mlir::verify(*module))) {
    throw std::runtime_error("invalid module");
  }
  module->print(llvm::outs());
  llvm::outs() << "\n";
}
} // namespace

int main(int argc, char **argv) {
  try {
    run(std::span(argv + 1, argc - 1));
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    llvm::errs() << "nixsplice: " << error.what() << "\n";
    return EXIT_FAILURE;
  }
}

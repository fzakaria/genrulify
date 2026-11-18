#include "dialect.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"

int main(int argc, char **argv) {
  if (argc != 3 || llvm::StringRef(argv[1]) != "opt") {
    llvm::errs() << "usage: nixsplice opt graph.mlir\n";
    return 1;
  }

  mlir::MLIRContext context;
  context.getOrLoadDialect<splice_ir::SpliceDialect>();
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(argv[2], &context);
  if (!module) {
    return 1;
  }

  mlir::PassManager passes(&context);
  passes.addPass(mlir::createCanonicalizerPass());
  if (mlir::failed(passes.run(*module))) {
    return 1;
  }
  module->print(llvm::outs());
  llvm::outs() << "\n";
}

#include "dialect.h"
#include "llvm/ADT/TypeSwitch.h"
#include "mlir/IR/Builders.h"

#include "Dialect.cpp.inc"
#define GET_TYPEDEF_CLASSES
#include "Types.cpp.inc"
#define GET_OP_CLASSES
#include "Ops.cpp.inc"

void splice_ir::SpliceDialect::initialize() {
  addTypes<FileType>();
  addOperations<SourceOp, CopyOp, AppendOp, PublishOp>();
}

// A copy preserves all bytes represented by !splice.file. Path and step
// record provenance. Neither attribute changes the value of the result.
mlir::OpFoldResult splice_ir::CopyOp::fold(FoldAdaptor) { return getInput(); }

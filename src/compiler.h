#pragma once

#include "mlir/IR/BuiltinOps.h"
#include <filesystem>

namespace splice_ir {
mlir::OwningOpRef<mlir::ModuleOp>
importTrace(mlir::MLIRContext &context, const std::filesystem::path &derivation,
            const std::filesystem::path &events);
void verifyArchives(const std::filesystem::path &report,
                    const std::filesystem::path &reference,
                    const std::filesystem::path &archive);
void emitWorkspace(mlir::ModuleOp module, const std::filesystem::path &assets,
                   const std::filesystem::path &destination);
} // namespace splice_ir

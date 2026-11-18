#pragma once

#include "entry.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include <optional>

#include "Dialect.h.inc"
#define GET_TYPEDEF_CLASSES
#include "Types.h.inc"
#define GET_ATTRDEF_CLASSES
#include "Attrs.h.inc"
#define GET_OP_CLASSES
#include "Ops.h.inc"

namespace splice_ir {
// Conversions between the recording's entry and the entry attribute.
Entry fromAttribute(EntryAttr attribute);
EntryAttr toAttribute(mlir::MLIRContext *context, const Entry &entry);
mlir::ArrayAttr toAttributes(mlir::MLIRContext *context,
                             const std::vector<Entry> &entries);

// The recorded entry an SSA value stands for, read from the `writes` of the
// splice.exec or splice.literal that defines the value, or from the entry
// of a splice.copy. Empty for a value that another dialect defines.
std::optional<Entry> entryOf(mlir::Value value);

// The value whose bytes a file value has: the value itself, or for the
// result of a splice.copy, the value the chain of copies starts from.
mlir::Value contentSource(mlir::Value value);

// Whether an entry is known without any bytes being computed: a directory
// and a symlink are described in full by their entry, and so is a file
// with nothing in it.
bool isConstant(const Entry &entry);
} // namespace splice_ir

#pragma once

#include "dialect.h"
#include "plan.h"
#include <functional>

namespace splice_ir {
// The build target that carries out an operation: `glue_784` for a literal
// of the builder's own writes, `lit_3614_mktemp` for a demoted group,
// `s2607_c__` for a replayed one and `archive` for an output.
std::string targetName(mlir::Operation *op, size_t outputCount);

// Names the layer that holds the entry an SSA value stands for.
using LayerOf = std::function<std::string(mlir::Value)>;

// The replay plan for a splice.exec. The plan asks only for the results
// that have a use: a write nothing reads may differ from run to run.
Plan execPlan(ExecOp op, const Recording &recording, const LayerOf &layerOf);

// The pack plan for a splice.output.
Plan outputPlan(OutputOp op, const LayerOf &layerOf);

// Writes a layer holding recorded entries, with file bytes from the
// recording's blobs.
void writeRecordedLayer(const Recording &recording,
                        const std::vector<Entry> &entries,
                        const std::filesystem::path &file);
} // namespace splice_ir

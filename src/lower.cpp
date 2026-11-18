#include "lower.h"
#include "absl/strings/str_cat.h"
#include "layer.h"

namespace splice_ir {
namespace {
constexpr std::string_view kArchiveName = "archive";

// Replaces every character that is not safe in both a Bazel and a Buck2
// target name and in a file name.
std::string sanitize(std::string_view text) {
  std::string safe;
  for (const char c : text) {
    const bool keep = std::isalnum(static_cast<unsigned char>(c)) != 0 ||
                      c == '_' || c == '.' || c == '-';
    safe.push_back(keep ? c : '_');
  }
  return safe;
}
} // namespace

std::string targetName(mlir::Operation *op, size_t outputCount) {
  if (auto exec = llvm::dyn_cast<ExecOp>(op)) {
    return absl::StrCat("s", exec.getStep(), "_", sanitize(exec.getName()));
  }
  if (auto literal = llvm::dyn_cast<LiteralOp>(op)) {
    // A demoted group and the builder's writes just before the group share
    // a step, so the two kinds of literal get different prefixes.
    if (literal.getDemoted()) {
      return absl::StrCat("lit_", literal.getStep(), "_",
                          sanitize(literal.getName()));
    }
    return absl::StrCat(sanitize(literal.getName()), "_", literal.getStep());
  }
  auto output = llvm::cast<OutputOp>(op);

  // A derivation with one output gets the short name.
  if (outputCount == 1) {
    return std::string(kArchiveName);
  }
  return absl::StrCat(kArchiveName, "_", sanitize(output.getName()));
}

Plan execPlan(ExecOp op, const Recording &recording, const LayerOf &layerOf) {
  Plan plan;
  plan.name = targetName(op, 0);
  plan.closure = recording.closure;
  plan.platform = kPlatformLayer;
  addProcesses(plan, recording, recording.group(op.getStep()));
  for (const auto input : op.getInputs()) {
    plan.inputs.push_back({layerOf(input), entryOf(input)->path});
  }
  for (const auto result : op.getResults()) {
    if (!result.use_empty()) {
      plan.results.push_back(*entryOf(result));
    }
  }
  return plan;
}

Plan outputPlan(OutputOp op, const LayerOf &layerOf) {
  Plan plan;
  plan.name = op.getName().str();
  plan.output = op.getPath().str();
  for (const auto entry : op.getEntries()) {
    plan.inputs.push_back({layerOf(entry), entryOf(entry)->path});
  }
  return plan;
}

void writeRecordedLayer(const Recording &recording,
                        const std::vector<Entry> &entries,
                        const std::filesystem::path &file) {
  std::vector<LayerSource> sources;
  for (const auto &entry : entries) {
    sources.push_back({entry, entry.kind == EntryKind::File
                                  ? recording.blob(entry.content)
                                  : std::filesystem::path()});
  }
  writeLayer(file, sources);
}
} // namespace splice_ir

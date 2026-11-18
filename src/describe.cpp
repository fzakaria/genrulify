#include "compiler.h"
#include "action.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
using Json = nlohmann::json;

constexpr std::string_view kReportFile = "check.json";

// A description is read by a web page, so long lists are cut to their
// first entries and a count, and digests to a recognizable prefix.
constexpr size_t kListedPaths = 40;
constexpr size_t kDigestPrefix = 12;

// The most bytes of an operation's printed form a description carries. An
// operation given a whole source tree prints thousands of operands.
constexpr size_t kPrintedBytes = 6000;

// An operation as the module prints it, cut to a readable length.
std::string printed(mlir::Operation &op) {
  std::string text;
  llvm::raw_string_ostream stream(text);
  op.print(stream);
  if (text.size() > kPrintedBytes) {
    text.resize(kPrintedBytes);
    text += "\n…";
  }
  return text;
}

Json describeEntry(const Entry &entry) {
  return {{"path", entry.path},
          {"kind", kindName(entry.kind)},
          {"mode", entry.mode},
          {"content", entry.kind == EntryKind::File
                          ? entry.content.substr(0, kDigestPrefix)
                          : entry.content}};
}

// The entries an operation wrote, with whether anything uses each one.
Json describeWrites(mlir::Operation &op) {
  Json writes = Json::array();
  for (const auto result : op.getResults()) {
    if (writes.size() == kListedPaths) {
      break;
    }
    auto entry = describeEntry(*entryOf(result));
    entry["used"] = !result.use_empty();
    writes.push_back(std::move(entry));
  }
  return writes;
}

// An operation's operands, grouped by the target that produces them.
Json describeInputs(mlir::OperandRange operands, size_t outputCount) {
  std::map<std::string, Json> producers;
  for (const auto value : operands) {
    const auto name = targetName(value.getDefiningOp(), outputCount);
    auto &producer = producers[name];
    if (producer.is_null()) {
      producer = {{"node", name}, {"count", 0}, {"paths", Json::array()}};
    }
    producer["count"] = producer["count"].get<size_t>() + 1;
    if (producer["paths"].size() < kListedPaths) {
      producer["paths"].push_back(entryOf(value)->path);
    }
  }
  Json inputs = Json::array();
  for (auto &[name, producer] : producers) {
    inputs.push_back(std::move(producer));
  }
  return inputs;
}
} // namespace

void describeGraph(mlir::ModuleOp module, const Recording &recording) {
  size_t outputCount = 0;
  for (auto &op : module.getBody()->getOperations()) {
    outputCount += llvm::isa<OutputOp>(op) ? 1 : 0;
  }

  // One node per operation, in the order the build ran them.
  Json nodes = Json::array();
  for (auto &op : module.getBody()->getOperations()) {
    Json node = {{"id", targetName(&op, outputCount)}, {"mlir", printed(op)}};
    if (auto exec = llvm::dyn_cast<ExecOp>(op)) {
      node.update({{"op", "exec"},
                   {"name", exec.getName()},
                   {"step", exec.getStep()},
                   {"inputs", describeInputs(exec.getInputs(), outputCount)},
                   {"writeCount", op.getNumResults()},
                   {"writes", describeWrites(op)}});
    } else if (auto literal = llvm::dyn_cast<LiteralOp>(op)) {
      node.update({{"op", "literal"},
                   {"name", literal.getName()},
                   {"step", literal.getStep()},
                   {"demoted", literal.getDemoted()},
                   {"inputs", Json::array()},
                   {"writeCount", op.getNumResults()},
                   {"writes", describeWrites(op)}});
    } else if (auto output = llvm::dyn_cast<OutputOp>(op)) {
      node.update({{"op", "output"},
                   {"name", output.getName()},
                   {"path", output.getPath()},
                   {"inputs", describeInputs(output.getEntries(), outputCount)}});
    }
    nodes.push_back(std::move(node));
  }

  // Every process group, including the ones the graph no longer contains,
  // with what the checker concluded when a report exists.
  std::map<uint64_t, Json> verdicts;
  if (const auto report = recording.directory / kReportFile; fs::exists(report)) {
    for (auto &group : Json::parse(readBytes(report)).at("groups")) {
      verdicts[group.at("step").get<uint64_t>()] = group;
    }
  }
  Json groups = Json::array();
  for (const auto &group : recording.groups) {
    Json processes = Json::array();
    for (const auto &process : group.processes) {
      processes.push_back({{"name", process.name},
                           {"argv", process.argv},
                           {"cwd", process.cwd}});
    }
    Json item = {{"step", group.start},
                 {"end", group.end},
                 {"name", label(group)},
                 {"processes", processes}};
    if (const auto verdict = verdicts.find(group.start);
        verdict != verdicts.end()) {
      item["outcome"] = verdict->second.at("outcome");
      item["pinned"] = verdict->second.at("pinned");
      item["inputs"] = verdict->second.value("inputs", 0);
      item["reason"] = verdict->second.value("reason", "");
    }
    groups.push_back(std::move(item));
  }

  const Json document = {{"name", recording.name},
                         {"run", recording.run},
                         {"open", recording.open},
                         {"outputs", recording.outputs},
                         {"steps", {recording.initialStep, recording.finalStep}},
                         {"closure", recording.closure.size()},
                         {"groups", groups},
                         {"nodes", nodes}};
  llvm::outs() << document.dump() << "\n";
}
} // namespace splice_ir

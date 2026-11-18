#include "compiler.h"
#include "action.h"
#include "llvm/Support/raw_ostream.h"
#include <array>
#include <fstream>
#include <set>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
using Json = nlohmann::json;


// A description is read by a web page, so long lists are cut to their
// first entries and a count, and digests to a recognizable prefix.
constexpr size_t kListedPaths = 12;
constexpr size_t kListedWrites = 20;
constexpr size_t kDigestPrefix = 12;

// The most bytes of an operation's printed form a description carries. An
// operation given a whole source tree prints thousands of operands.
constexpr size_t kPrintedBytes = 1500;

// How many of the copies raised from one process group are printed.
constexpr size_t kPrintedCopies = 6;

// The name of the stage before any pass has run.
constexpr std::string_view kRecordedStage = "recorded";

// The kinds of node, as a page reads them.
constexpr std::string_view kExecKind = "exec";
constexpr std::string_view kLiteralKind = "literal";
constexpr std::string_view kCopyKind = "copy";
constexpr std::string_view kOutputKind = "output";

// What a page draws as one node: one operation, or all the copies raised
// from one process group, which share a target name.
struct Node {
  std::string id;
  std::vector<mlir::Operation *> operations;

  // The number MLIR prints for the first value each operation defines,
  // which is how the printed text of other operations refers to them.
  std::vector<size_t> values;
};

// How much of a node a description carries.
enum class Detail {
  // Counts only, for a node the next pass removes.
  Counts,

  // The paths of its operands and the entries it wrote as well.
  Paths,
};

size_t outputsOf(mlir::ModuleOp module) {
  size_t count = 0;
  for (auto &op : module.getBody()->getOperations()) {
    count += llvm::isa<OutputOp>(op) ? 1 : 0;
  }
  return count;
}

// The nodes of a module, in the order the build ran them.
std::vector<Node> nodesOf(mlir::ModuleOp module) {
  const auto outputCount = outputsOf(module);
  std::vector<Node> nodes;
  std::map<std::string, size_t> copies;
  size_t nextValue = 0;
  for (auto &op : module.getBody()->getOperations()) {
    // MLIR numbers the operations that define something, in order.
    const auto value = nextValue;
    nextValue += op.getNumResults() == 0 ? 0 : 1;
    const auto id = targetName(&op, outputCount);
    if (llvm::isa<CopyOp>(op)) {
      const auto [known, added] = copies.try_emplace(id, nodes.size());
      if (!added) {
        nodes[known->second].operations.push_back(&op);
        nodes[known->second].values.push_back(value);
        continue;
      }
    }
    nodes.push_back({id, {&op}, {value}});
  }
  return nodes;
}

// A node as the module prints it, cut to a readable length.
std::string printed(const Node &node) {
  std::string text;
  llvm::raw_string_ostream stream(text);
  for (size_t index = 0; index < node.operations.size(); ++index) {
    if (index == kPrintedCopies) {
      stream << "\n… " << node.operations.size() - kPrintedCopies << " more";
      break;
    }
    if (index != 0) {
      stream << "\n";
    }
    node.operations[index]->print(stream);
  }
  if (text.size() > kPrintedBytes) {
    // Cut at a line so that an entry is shown whole or not at all.
    const auto line = text.rfind('\n', kPrintedBytes);
    text.resize(line == std::string::npos ? kPrintedBytes : line);
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

// The entries a node wrote, with whether anything uses each one.
Json describeWrites(const Node &node) {
  Json writes = Json::array();
  for (auto *op : node.operations) {
    for (const auto result : op->getResults()) {
      if (writes.size() == kListedWrites) {
        return writes;
      }
      auto entry = describeEntry(*entryOf(result));
      entry["used"] = !result.use_empty();
      writes.push_back(std::move(entry));
    }
  }
  return writes;
}

// A node's operands, grouped by the node that produces them.
Json describeInputs(const Node &node, size_t outputCount, Detail detail) {
  std::map<std::string, Json> producers;
  for (auto *op : node.operations) {
    for (const auto value : op->getOperands()) {
      const auto name = targetName(value.getDefiningOp(), outputCount);
      auto &producer = producers[name];
      if (producer.is_null()) {
        producer = {{"node", name}, {"count", 0}, {"paths", Json::array()}};
      }
      producer["count"] = producer["count"].get<size_t>() + 1;
      if (detail == Detail::Paths && producer["paths"].size() < kListedPaths) {
        producer["paths"].push_back(entryOf(value)->path);
      }
    }
  }
  Json inputs = Json::array();
  for (auto &[name, producer] : producers) {
    inputs.push_back(std::move(producer));
  }
  return inputs;
}

// The values a node defines, as runs of consecutive numbers.
Json describeValues(const Node &node) {
  Json runs = Json::array();
  for (size_t index = 0; index < node.values.size(); ++index) {
    if (node.operations[index]->getNumResults() == 0) {
      continue;
    }
    const auto value = node.values[index];
    if (!runs.empty() &&
        runs.back()[0].get<size_t>() + runs.back()[1].get<size_t>() == value) {
      runs.back()[1] = runs.back()[1].get<size_t>() + 1;
      continue;
    }
    runs.push_back({value, 1});
  }
  return runs;
}

Json describeNode(const Node &node, size_t outputCount, Detail detail) {
  auto *first = node.operations.front();
  size_t writeCount = 0;
  for (auto *op : node.operations) {
    writeCount += op->getNumResults();
  }
  Json json = {{"id", node.id},
               {"mlir", printed(node)},
               {"values", describeValues(node)},
               {"counted", detail == Detail::Counts},
               {"inputs", describeInputs(node, outputCount, detail)},
               {"writeCount", writeCount},
               {"writes", detail == Detail::Paths ? describeWrites(node)
                                                  : Json::array()}};
  if (auto exec = llvm::dyn_cast<ExecOp>(first)) {
    json.update({{"op", kExecKind},
                 {"name", exec.getName()},
                 {"step", exec.getStep()}});
  } else if (auto literal = llvm::dyn_cast<LiteralOp>(first)) {
    json.update({{"op", kLiteralKind},
                 {"name", literal.getName()},
                 {"step", literal.getStep()},
                 {"demoted", literal.getDemoted()},
                 {"folded", literal.getFolded()}});
  } else if (auto copy = llvm::dyn_cast<CopyOp>(first)) {
    json.update({{"op", kCopyKind},
                 {"name", copy.getName()},
                 {"step", copy.getStep()}});
  } else if (auto output = llvm::dyn_cast<OutputOp>(first)) {
    json.update({{"op", kOutputKind},
                 {"name", output.getName()},
                 {"path", output.getPath()}});
  }
  return json;
}
} // namespace

void describeGraph(mlir::ModuleOp module, const Recording &recording) {
  const auto outputCount = outputsOf(module);

  // The module after each pass of the pipeline, starting with none.
  std::vector<std::pair<std::string, mlir::OwningOpRef<mlir::ModuleOp>>> modules;
  modules.emplace_back(kRecordedStage, module.clone());
  for (const auto pass : kAllPasses) {
    mlir::OwningOpRef<mlir::ModuleOp> next(modules.back().second->clone());
    optimize(*next, std::array{pass});
    modules.emplace_back(passName(pass), std::move(next));
  }

  // One stage per module. A node the next pass removes is described by
  // counts alone, to keep the description small: a build has many of them
  // and a page shows them only on the way to removing them.
  Json stages = Json::array();
  for (size_t index = 0; index < modules.size(); ++index) {
    std::set<std::string> kept;
    const bool last = index + 1 == modules.size();
    if (!last) {
      for (const auto &node : nodesOf(*modules[index + 1].second)) {
        kept.insert(node.id);
      }
    }
    Json nodes = Json::array();
    for (const auto &node : nodesOf(*modules[index].second)) {
      const auto detail =
          last || kept.contains(node.id) ? Detail::Paths : Detail::Counts;
      nodes.push_back(describeNode(node, outputCount, detail));
    }
    stages.push_back({{"pass", modules[index].first}, {"nodes", nodes}});
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
                         {"fine", recording.fine},
                         {"outputs", recording.outputs},
                         {"steps", {recording.initialStep, recording.finalStep}},
                         {"closure", recording.closure.size()},
                         {"groups", groups},
                         {"stages", stages}};
  llvm::outs() << document.dump() << "\n";
}
} // namespace splice_ir

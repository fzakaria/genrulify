#include "absl/strings/str_cat.h"
#include "action.h"
#include "compiler.h"
#include "llvm/Support/raw_ostream.h"
#include "mlir/IR/AsmState.h"
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

};

// How much of a node a description carries.
enum class Detail {
  // Counts only, for a node that elimination would remove.
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
  for (auto &op : module.getBody()->getOperations()) {
    const auto id = targetName(&op, outputCount);
    if (llvm::isa<CopyOp>(op)) {
      const auto [known, added] = copies.try_emplace(id, nodes.size());
      if (!added) {
        nodes[known->second].operations.push_back(&op);
        continue;
      }
    }
    nodes.push_back({id, {&op}});
  }
  return nodes;
}

// One operation as the module prints it. `state` holds the names the
// module gives its values, so that every operation is printed with the
// names the others use for it.
std::string printedOperation(mlir::Operation &op, mlir::AsmState &state) {
  std::string text;
  llvm::raw_string_ostream stream(text);
  op.print(stream, state);
  return text;
}

// A node as the module prints it, cut to a readable length.
std::string printed(const Node &node, mlir::AsmState &state) {
  std::string text;
  for (size_t index = 0; index < node.operations.size(); ++index) {
    if (index == kPrintedCopies) {
      absl::StrAppend(&text, "\n… ", node.operations.size() - kPrintedCopies,
                      " more");
      break;
    }
    if (index != 0) {
      text += "\n";
    }
    text += printedOperation(*node.operations[index], state);
  }
  if (text.size() > kPrintedBytes) {
    // Cut at a line so that an entry is shown whole or not at all.
    const auto line = text.rfind('\n', kPrintedBytes);
    text.resize(line == std::string::npos ? kPrintedBytes : line);
    text += "\n…";
  }
  return text;
}

// The names of the values a node defines, as printed IR refers to them
// without the `%`: `cc_2607` for `%cc_2607 = ...` and for `%cc_2607:3`.
Json describeDefinitions(const Node &node, mlir::AsmState &state) {
  Json names = Json::array();
  for (auto *op : node.operations) {
    if (op->getNumResults() == 0) {
      continue;
    }
    const auto text = printedOperation(*op, state);
    const auto end = text.find_first_of(":= ");
    names.push_back(text.substr(1, end - 1));
  }
  return names;
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

Json describeNode(const Node &node, size_t outputCount, Detail detail,
                  mlir::AsmState &state) {
  auto *first = node.operations.front();
  size_t writeCount = 0;
  for (auto *op : node.operations) {
    writeCount += op->getNumResults();
  }
  Json json = {{"id", node.id},
               {"mlir", printed(node, state)},
               {"defines", describeDefinitions(node, state)},
               {"counted", detail == Detail::Counts},
               {"inputs", describeInputs(node, outputCount, detail)},
               {"writeCount", writeCount},
               {"writes", detail == Detail::Paths ? describeWrites(node)
                                                  : Json::array()}};
  if (auto exec = llvm::dyn_cast<ExecOp>(first)) {
    json.update({{"op", kExecKind},
                 {"name", exec.getName()},
                 {"step", exec.getStep()}});

    // The kind of build rule the group was raised to, when it was.
    if (const auto kind = exec.getRule()) {
      json["rule"] = *kind;
    }
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

  // The module under every choice of passes. Choice number `mask` runs
  // the passes whose bit is set, bit 0 being the first of the pipeline.
  const size_t choices = size_t{1} << kToggledPasses.size();
  const auto chosen = [](size_t mask) {
    std::vector<Pass> passes;
    for (size_t bit = 0; bit < kToggledPasses.size(); ++bit) {
      if ((mask >> bit & 1) != 0) {
        passes.push_back(kToggledPasses[bit]);
      }
    }
    return passes;
  };
  std::vector<mlir::OwningOpRef<mlir::ModuleOp>> modules;
  for (size_t mask = 0; mask < choices; ++mask) {
    modules.emplace_back(module.clone());
    optimize(*modules.back(), chosen(mask), &recording);
  }

  // Which bit elimination has, to find the same choice with it added.
  const auto eliminateBit =
      std::ranges::find(kToggledPasses, Pass::Eliminate) - kToggledPasses.begin();

  // A node is the same under many choices, so each distinct node is listed
  // once and a choice is a list of positions in that list. A node that
  // elimination would remove is described by counts alone: a build has
  // many of them and a page shows them only on the way to removing them.
  Json records = Json::array();
  std::map<std::string, size_t> known;
  Json variants = Json::array();
  for (size_t mask = 0; mask < choices; ++mask) {
    std::set<std::string> live;
    for (const auto &node :
         nodesOf(*modules[mask | size_t{1} << eliminateBit])) {
      live.insert(node.id);
    }
    mlir::AsmState state(*modules[mask]);
    Json variant = Json::array();
    for (const auto &node : nodesOf(*modules[mask])) {
      const auto detail = live.contains(node.id) ? Detail::Paths : Detail::Counts;
      auto record = describeNode(node, outputCount, detail, state);
      const auto [position, added] =
          known.try_emplace(record.dump(), records.size());
      if (added) {
        records.push_back(std::move(record));
      }
      variant.push_back(position->second);
    }
    variants.push_back(std::move(variant));
  }
  Json names = Json::array();
  for (const auto pass : kToggledPasses) {
    names.push_back(passName(pass));
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
                         {"passes", names},
                         {"records", records},
                         {"variants", variants}};
  llvm::outs() << document.dump() << "\n";
}
} // namespace splice_ir

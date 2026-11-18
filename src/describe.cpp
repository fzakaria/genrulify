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
// operation given a whole source tree prints thousands of operands. An
// eliminated operation gets less room, because a build has many of them
// and a page shows them only on the way to removing them.
constexpr size_t kPrintedBytes = 6000;
constexpr size_t kEliminatedPrintedBytes = 1200;

// An operation as the module prints it, cut to a readable length.
std::string printed(mlir::Operation &op, size_t limit) {
  std::string text;
  llvm::raw_string_ostream stream(text);
  op.print(stream);
  if (text.size() > limit) {
    // Cut at a line so that an entry is shown whole or not at all.
    const auto line = text.rfind('\n', limit);
    text.resize(line == std::string::npos ? limit : line);
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
// `survivor` is the same operation in the optimized module, where a use by
// an eliminated operation no longer counts.
Json describeWrites(mlir::Operation &op, mlir::Operation &survivor) {
  Json writes = Json::array();
  for (unsigned index = 0; index < op.getNumResults(); ++index) {
    if (writes.size() == kListedPaths) {
      break;
    }
    auto entry = describeEntry(*entryOf(op.getResult(index)));
    entry["used"] = !survivor.getResult(index).use_empty();
    writes.push_back(std::move(entry));
  }
  return writes;
}

// Whether a description lists the paths of an operation's operands or
// only counts them.
enum class Paths { Listed, Counted };

// An operation's operands, grouped by the target that produces them.
Json describeInputs(mlir::OperandRange operands, size_t outputCount,
                    Paths paths) {
  std::map<std::string, Json> producers;
  for (const auto value : operands) {
    const auto name = targetName(value.getDefiningOp(), outputCount);
    auto &producer = producers[name];
    if (producer.is_null()) {
      producer = {{"node", name}, {"count", 0}, {"paths", Json::array()}};
    }
    producer["count"] = producer["count"].get<size_t>() + 1;
    if (paths == Paths::Listed && producer["paths"].size() < kListedPaths) {
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

  // Optimize a copy, and find each operation that survives by its target
  // name, which elimination does not change.
  mlir::OwningOpRef<mlir::ModuleOp> optimized(module.clone());
  optimize(*optimized);
  std::map<std::string, mlir::Operation *> survivors;
  for (auto &op : optimized->getBody()->getOperations()) {
    survivors[targetName(&op, outputCount)] = &op;
  }

  // One node per recorded operation, in the order the build ran them. The
  // printed form keeps the value numbers of the module before elimination,
  // so that the text of a surviving operation still names the same values
  // while the eliminated ones are on the page.
  Json nodes = Json::array();
  for (auto &op : module.getBody()->getOperations()) {
    const auto id = targetName(&op, outputCount);
    const auto survivor = survivors.find(id);
    const bool dead = survivor == survivors.end();
    Json node = {{"id", id},
                 {"dead", dead},
                 {"mlir", printed(op, dead ? kEliminatedPrintedBytes
                                           : kPrintedBytes)}};
    const auto listed = dead ? Paths::Counted : Paths::Listed;
    const auto writes = dead ? Json::array()
                             : (llvm::isa<OutputOp>(op)
                                    ? Json::array()
                                    : describeWrites(op, *survivor->second));
    if (auto exec = llvm::dyn_cast<ExecOp>(op)) {
      node.update({{"op", "exec"},
                   {"name", exec.getName()},
                   {"step", exec.getStep()},
                   {"inputs",
                    describeInputs(exec.getInputs(), outputCount, listed)},
                   {"writeCount", op.getNumResults()},
                   {"writes", writes}});
    } else if (auto literal = llvm::dyn_cast<LiteralOp>(op)) {
      node.update({{"op", "literal"},
                   {"name", literal.getName()},
                   {"step", literal.getStep()},
                   {"demoted", literal.getDemoted()},
                   {"inputs", Json::array()},
                   {"writeCount", op.getNumResults()},
                   {"writes", writes}});
    } else if (auto output = llvm::dyn_cast<OutputOp>(op)) {
      node.update({{"op", "output"},
                   {"name", output.getName()},
                   {"path", output.getPath()},
                   {"inputs", describeInputs(output.getEntries(), outputCount,
                                             listed)}});
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

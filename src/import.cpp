#include "compiler.h"
#include "dialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"
#include "action.h"
#include <set>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
using Json = nlohmann::json;

// The names of the literals that hold what Nix put in the sandbox before
// the builder started and what the builder wrote itself.
constexpr std::string_view kSandboxName = "sandbox";
constexpr std::string_view kGlueName = "glue";

// Whether a literal stands for a process group that replay rejected.
enum class Origin { Builder, DemotedGroup };

constexpr std::string_view kVerdictsFile = "verdicts.json";
constexpr std::string_view kNarrowName = "narrow";
constexpr std::string_view kNearName = "near";
constexpr std::string_view kTracedName = "traced";
constexpr std::string_view kWideName = "wide";
constexpr std::string_view kLiteralName = "literal";
constexpr int kJsonIndent = 1;

// Adds a path and every directory above it that existed in `state`.
void addWithParents(const std::string &path, const State &state,
                    std::set<std::string> &paths) {
  for (fs::path current = path; current.has_relative_path();
       current = current.parent_path()) {
    if (state.contains(current.string())) {
      paths.insert(current.string());
    }
  }
}

// The paths a group is given at Narrow: the files it opened or executed,
// the prior version of everything it modified or removed, its working
// directories, the files behind its inherited descriptors, and the
// directories that lead to all of those and to what it created.
std::set<std::string> narrowInputs(const Group &group, const State &before,
                                   const Difference &effect) {
  std::set<std::string> paths;
  for (const auto &path : group.reads) {
    addWithParents(path, before, paths);
  }
  for (const auto &entry : effect.changed) {
    addWithParents(entry.path, before, paths);
  }
  for (const auto &path : effect.removed) {
    addWithParents(path, before, paths);
  }
  for (const auto &process : group.processes) {
    addWithParents(process.cwd, before, paths);
    for (const auto &descriptor : process.fds) {
      addWithParents(descriptor.target, before, paths);
    }
  }
  return paths;
}

// The paths a group is given at Near: its narrow inputs and every entry
// that shares a directory with one of them.
std::set<std::string> nearInputs(const Group &group, const State &before,
                                 const Difference &effect) {
  auto paths = narrowInputs(group, before, effect);

  // The directories the group worked in: each directory it was given and
  // the directory of each file it was given.
  std::set<std::string> directories;
  for (const auto &path : paths) {
    if (before.at(path).kind == EntryKind::Directory) {
      directories.insert(path);
    }
    directories.insert(fs::path(path).parent_path().string());
  }
  for (const auto &[path, entry] : before) {
    if (directories.contains(fs::path(path).parent_path().string())) {
      paths.insert(path);
    }
  }
  return paths;
}

// Tracks the SSA value that holds the current version of every path while
// the importer walks the recording in step order.
class Versions {
public:
  Versions(mlir::OpBuilder &builder) : builder(builder) {}

  // Installs an operation's results as the current versions of the paths
  // the operation wrote, and forgets the paths that were removed.
  void apply(const Difference &effect, mlir::ResultRange results) {
    for (const auto &path : effect.removed) {
      current.erase(path);
    }
    for (size_t index = 0; index < effect.changed.size(); ++index) {
      current[effect.changed[index].path] = results[index];
    }
  }

  // Emits a splice.literal for whatever changed between the last state the
  // graph accounts for and `state`.
  void literal(std::string_view name, uint64_t step, const State &state,
               Origin origin = Origin::Builder) {
    const auto effect = difference(accounted, state);
    accounted = state;
    if (effect.changed.empty()) {
      apply(effect, mlir::ResultRange(nullptr, 0));
      return;
    }
    auto op = LiteralOp::create(
        builder, builder.getUnknownLoc(), types(effect.changed.size()), name,
        step, toAttributes(builder.getContext(), effect.changed),
        origin == Origin::DemotedGroup);
    apply(effect, op->getResults());
  }

  // Emits a splice.exec for a group that took the listed paths and left
  // `after`. The group's pinned writes come from a demoted literal instead,
  // so nothing downstream depends on the replay reproducing them.
  void exec(const Group &group, const std::set<std::string> &inputs,
            const std::set<std::string> &pinned, const State &after) {
    const auto effect = difference(accounted, after);
    accounted = after;
    Difference computed{{}, effect.removed};
    Difference recorded;
    for (const auto &entry : effect.changed) {
      (pinned.contains(entry.path) ? recorded : computed)
          .changed.push_back(entry);
    }

    llvm::SmallVector<mlir::Value> operands;
    for (const auto &path : inputs) {
      operands.push_back(current.at(path));
    }
    auto *context = builder.getContext();
    auto exec =
        ExecOp::create(builder, builder.getUnknownLoc(),
                       types(computed.changed.size()), operands, label(group),
                       group.start, toAttributes(context, computed.changed),
                       /*rule=*/mlir::StringAttr());
    apply(computed, exec->getResults());
    if (recorded.changed.empty()) {
      return;
    }
    auto literal = LiteralOp::create(
        builder, builder.getUnknownLoc(), types(recorded.changed.size()),
        label(group), group.start, toAttributes(context, recorded.changed),
        /*demoted=*/true);
    apply(recorded, literal->getResults());
  }

  // The current version of every path at or below `root`.
  llvm::SmallVector<mlir::Value> below(const std::string &root) const {
    llvm::SmallVector<mlir::Value> values;
    for (const auto &[path, value] : current) {
      if (isBelow(path, root)) {
        values.push_back(value);
      }
    }
    return values;
  }

  const State &state() const { return accounted; }

private:
  llvm::SmallVector<mlir::Type> types(size_t count) {
    return llvm::SmallVector<mlir::Type>(count,
                                         EntryType::get(builder.getContext()));
  }

  mlir::OpBuilder &builder;
  std::map<std::string, mlir::Value> current;

  // The filesystem state the operations emitted so far add up to.
  State accounted;
};
} // namespace

std::string_view rungName(Rung rung) {
  switch (rung) {
  case Rung::Narrow:
    return kNarrowName;
  case Rung::Near:
    return kNearName;
  case Rung::Traced:
    return kTracedName;
  case Rung::Wide:
    return kWideName;
  case Rung::Literal:
    return kLiteralName;
  }
  throw std::logic_error("unknown rung");
}

namespace {
Rung rungFromName(const std::string &name) {
  for (const auto rung :
       {Rung::Narrow, Rung::Near, Rung::Traced, Rung::Wide, Rung::Literal}) {
    if (name == rungName(rung)) {
      return rung;
    }
  }
  throw std::runtime_error("unknown rung " + name);
}
} // namespace

Verdicts loadVerdicts(const Recording &recording) {
  Verdicts verdicts;
  const auto file = recording.directory / kVerdictsFile;
  if (!fs::exists(file)) {
    return verdicts;
  }
  for (const auto &[step, json] : Json::parse(readBytes(file)).items()) {
    Verdict verdict;
    verdict.rung = rungFromName(json.at("rung"));
    verdict.pinned = json.value("pinned", std::set<std::string>{});
    verdict.inputs = json.value("inputs", std::set<std::string>{});
    verdict.traced = json.value("traced", false);
    verdict.fallback =
        rungFromName(json.value("fallback", std::string(kWideName)));
    verdicts[std::stoull(step)] = std::move(verdict);
  }
  return verdicts;
}

void saveVerdicts(const Recording &recording, const Verdicts &verdicts) {
  Json document = Json::object();
  for (const auto &[step, verdict] : verdicts) {
    if (verdict == Verdict{}) {
      continue;
    }
    document[std::to_string(step)] = {{"rung", rungName(verdict.rung)},
                                      {"pinned", verdict.pinned},
                                      {"inputs", verdict.inputs},
                                      {"traced", verdict.traced},
                                      {"fallback", rungName(verdict.fallback)}};
  }
  writeBytes(recording.directory / kVerdictsFile,
             document.dump(kJsonIndent) + "\n");
}

mlir::OwningOpRef<mlir::ModuleOp> importRecording(mlir::MLIRContext &context,
                                                  const Recording &recording,
                                                  const Verdicts &verdicts) {
  mlir::OpBuilder builder(&context);
  mlir::OwningOpRef module(mlir::ModuleOp::create(builder.getUnknownLoc()));
  builder.setInsertionPointToStart(module->getBody());
  Versions versions(builder);

  // Nix creates the build directory, and for some derivations files inside
  // it, before the builder runs.
  versions.literal(kSandboxName, recording.initialStep,
                   recording.state(recording.initialStep));

  for (const auto &group : recording.groups) {
    // Whatever changed since the previous group ended was written by the
    // builder itself. There is no execve to replay, so keep the bytes.
    const auto before = recording.state(group.start);
    versions.literal(kGlueName, group.start, before);
    const auto after = recording.state(group.end);

    // A recording that dropped read records cannot know what a group read.
    Verdict verdict;
    if (const auto known = verdicts.find(group.start);
        known != verdicts.end()) {
      verdict = known->second;
    }
    if (recording.lost != 0 && verdict.rung == Rung::Narrow) {
      verdict.rung = Rung::Wide;
    }
    if (unreplayable(group)) {
      verdict.rung = Rung::Literal;
    }

    switch (verdict.rung) {
    case Rung::Literal:
      versions.literal(label(group), group.start, after, Origin::DemotedGroup);
      break;
    case Rung::Wide: {
      std::set<std::string> everything;
      for (const auto &[path, entry] : before) {
        everything.insert(path);
      }
      versions.exec(group, everything, verdict.pinned, after);
      break;
    }
    case Rung::Near:
      versions.exec(group, nearInputs(group, before, difference(before, after)),
                    verdict.pinned, after);
      break;
    case Rung::Traced: {
      // What the replay touched, on top of what the recording says the
      // group opened and changed.
      auto inputs = narrowInputs(group, before, difference(before, after));
      for (const auto &path : verdict.inputs) {
        addWithParents(path, before, inputs);
      }
      versions.exec(group, inputs, verdict.pinned, after);
      break;
    }
    case Rung::Narrow:
      versions.exec(group,
                    narrowInputs(group, before, difference(before, after)),
                    verdict.pinned, after);
      break;
    }
  }

  // The builder can write after its last child exits.
  versions.literal(kGlueName, recording.finalStep,
                   recording.state(recording.finalStep));
  for (const auto &[name, path] : recording.outputs) {
    OutputOp::create(builder, builder.getUnknownLoc(), versions.below(path),
                     name, path);
  }
  return module;
}

} // namespace splice_ir

#include "compiler.h"
#include "dialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"
#include "process.h"
#include "rules.h"
#include <algorithm>
#include <filesystem>
#include <map>
#include <set>

namespace splice_ir {
namespace {
// The names of the passes on the command line and in reports.
constexpr std::string_view kLinkName = "link";
constexpr std::string_view kEliminateName = "dce";
constexpr std::string_view kFoldName = "fold";
constexpr std::string_view kMergeName = "merge";
constexpr std::string_view kRaiseName = "raise";
constexpr std::string_view kRulesName = "rules";

// The process groups of a module, collected before any of them is
// rewritten, so that a pass can add operations while it walks them.
std::vector<ExecOp> processGroups(mlir::ModuleOp module) {
  std::vector<ExecOp> groups;
  for (auto &op : module.getBody()->getOperations()) {
    if (auto exec = llvm::dyn_cast<ExecOp>(op)) {
      groups.push_back(exec);
    }
  }
  return groups;
}

// The operations of a project's graph that depend on the project's source
// tree: the literal that holds the tree, and every operation given a file
// or a symlink one of those defines. A directory does not carry the
// dependence on: that a command made the directory is not a matter of
// what any source holds.
//
// A project is built again after its sources are edited, with the same
// commands. What a command that read a source left behind was computed
// from the bytes the source had on the day of the recording. That a result
// had the bytes of an input, or was empty, is a fact about those bytes and
// not about the command: `sort` of a sorted file leaves its input. So no
// pass may replace such a result on the strength of its bytes.
//
// A graph without a source tree depends on nothing that changes, and
// nothing in it is in the set.
std::set<mlir::Operation *> dependsOnSource(mlir::ModuleOp module) {
  std::set<mlir::Operation *> dependent;

  // Operations are in the order of their steps, so every operand is
  // defined above its use and one walk is enough.
  for (auto &op : module.getBody()->getOperations()) {
    auto literal = llvm::dyn_cast<LiteralOp>(&op);
    bool depends = literal && literal.getName().str() == kSourceName &&
                   !literal.getDemoted() && !literal.getFolded();
    for (const auto operand : op.getOperands()) {
      depends = depends ||
                (entryOf(operand)->kind != EntryKind::Directory &&
                 dependent.contains(operand.getDefiningOp()));
    }
    if (depends) {
      dependent.insert(&op);
    }
  }
  return dependent;
}

// Constant folding. A directory, a symlink and an empty file are known in
// full from their entry: there are no bytes for a process to compute. The
// pass moves every such result that something reads into a splice.literal
// marked `folded`, so that nothing depends on the process group for it. A
// group left with no result in use, such as a mkdir or a touch, is then
// dead.
//
// The rewrite is sound for the reason demotion is: a literal has the
// recorded entry by definition, so every reader is given what it was
// given before.
//
// Of a command that read a project's source, only the directories are
// folded. That the command makes the directory does not depend on what a
// source holds. That a file came out empty, or where a symlink points,
// can.
struct FoldConstants
    : mlir::PassWrapper<FoldConstants, mlir::OperationPass<mlir::ModuleOp>> {
  llvm::StringRef getArgument() const final { return "splice-fold-constants"; }
  llvm::StringRef getDescription() const final {
    return "Take results that need no bytes computed out of process groups";
  }

  void runOnOperation() final {
    auto *context = &getContext();
    const auto dependent = dependsOnSource(getOperation());
    for (auto exec : processGroups(getOperation())) {
      // Find the constant results that have a reader.
      const bool readsSource = dependent.contains(exec.getOperation());
      std::vector<mlir::Value> constants;
      std::vector<Entry> entries;
      for (const auto result : exec->getResults()) {
        const auto entry = *entryOf(result);
        if (result.use_empty() || !isConstant(entry)) {
          continue;
        }
        if (readsSource && entry.kind != EntryKind::Directory) {
          continue;
        }
        constants.push_back(result);
        entries.push_back(entry);
      }
      if (constants.empty()) {
        continue;
      }

      // Define them once more as a literal, and point the readers there.
      mlir::OpBuilder builder(exec);
      const llvm::SmallVector<mlir::Type> types(entries.size(),
                                                EntryType::get(context));
      auto literal = LiteralOp::create(
          builder, exec->getLoc(), types, exec.getName(), exec.getStep(),
          toAttributes(context, entries), /*demoted=*/false, /*folded=*/true);
      for (size_t index = 0; index < constants.size(); ++index) {
        constants[index].replaceAllUsesWith(literal->getResult(index));
      }
    }
  }
};

// Links the files the driver wrote itself to the files they came from. A
// driver that renames or links a file in-process, as cargo does with the
// binary it has just had compiled, leaves a literal with the bytes of a
// file that already exists. The driver could see every file there was, so
// the literal is a copy of the latest version with those bytes, wherever
// that version is.
//
// The pass runs before elimination. Until the copy exists nothing reads
// the compiled file, and elimination would delete the compiler and leave
// the output as recorded bytes.
struct LinkDriverWrites
    : mlir::PassWrapper<LinkDriverWrites,
                        mlir::OperationPass<mlir::ModuleOp>> {
  llvm::StringRef getArgument() const final {
    return "splice-link-driver-writes";
  }
  llvm::StringRef getDescription() const final {
    return "Turn driver-written files with existing bytes into copies";
  }

  void runOnOperation() final {
    auto *context = &getContext();

    // The latest file version with each content, among the operations
    // walked so far.
    std::map<std::string, mlir::Value> latest;
    const auto remember = [&](mlir::Operation &op) {
      for (const auto result : op.getResults()) {
        const auto entry = *entryOf(result);
        if (entry.kind == EntryKind::File && !isConstant(entry)) {
          latest[entry.content] = contentSource(result);
        }
      }
    };

    // Collect the operations first: the walk adds copies to the block.
    std::vector<mlir::Operation *> operations;
    for (auto &op : getOperation().getBody()->getOperations()) {
      operations.push_back(&op);
    }
    for (auto *op : operations) {
      auto literal = llvm::dyn_cast<LiteralOp>(op);

      // A demoted literal stands for a process group and a folded one has
      // no bytes, so only what the builder wrote is looked at.
      if (!literal || literal.getDemoted() || literal.getFolded()) {
        remember(*op);
        continue;
      }
      mlir::OpBuilder builder(literal);
      std::set<std::string> linked;
      for (mlir::Value result : literal->getResults()) {
        const auto entry = *entryOf(result);
        const auto original = latest.find(entry.content);
        if (result.use_empty() || entry.kind != EntryKind::File ||
            original == latest.end()) {
          continue;
        }
        auto copy = CopyOp::create(builder, literal->getLoc(),
                                   EntryType::get(context), original->second,
                                   literal.getName(), literal.getStep(),
                                   toAttribute(context, entry));
        result.replaceAllUsesWith(copy.getOutput());
        linked.insert(entry.content);
      }

      // What the literal still defines is the latest version of its bytes.
      // A file that was linked is not: the version it copies stays the one
      // to link to, so that a second link of the same bytes also names
      // the file a process computed.
      for (const auto result : literal->getResults()) {
        const auto entry = *entryOf(result);
        if (entry.kind == EntryKind::File && !isConstant(entry) &&
            !linked.contains(entry.content)) {
          latest[entry.content] = result;
        }
      }
    }
  }
};

// Which operand a result is compared with.
enum class Source {
  // The operand at the path the result was written to.
  SamePath,

  // An operand at any other path.
  OtherPath,
};

// Turns results that have the bytes of an operand into copies of the
// operand. A process group that was given a file and left the same bytes,
// at that path or at another, computed nothing a later step can tell from
// the file it was given: later steps see a path, a kind, a mode, an mtime
// and content, and the copy has the recorded value of each.
//
// With Source::SamePath the pass merges a result into the version it
// replaced, which is what a chmod or a rewrite that changed nothing leaves.
// With Source::OtherPath the pass raises cp, install and mv. A group left
// with no result in use is then dead.
struct RaiseCopies
    : mlir::PassWrapper<RaiseCopies, mlir::OperationPass<mlir::ModuleOp>> {
  explicit RaiseCopies(Source source) : source(source) {}

  llvm::StringRef getArgument() const final {
    return source == Source::SamePath ? "splice-merge-same-outputs"
                                      : "splice-raise-copies";
  }
  llvm::StringRef getDescription() const final {
    return "Turn results with the bytes of an operand into copies";
  }

  void runOnOperation() final {
    auto *context = &getContext();
    const auto dependent = dependsOnSource(getOperation());
    for (auto exec : processGroups(getOperation())) {
      // A command that read a project's source may leave other bytes
      // once the source is edited, so the command is kept.
      if (dependent.contains(exec.getOperation())) {
        continue;
      }
      mlir::OpBuilder builder(exec);
      for (mlir::Value result : exec->getResults()) {
        const auto entry = *entryOf(result);
        if (result.use_empty() || entry.kind != EntryKind::File) {
          continue;
        }
        const auto original = operandWithContent(exec, entry);
        if (!original) {
          continue;
        }
        auto copy = CopyOp::create(builder, exec->getLoc(),
                                   EntryType::get(context), *original,
                                   exec.getName(), exec.getStep(),
                                   toAttribute(context, entry));
        result.replaceAllUsesWith(copy.getOutput());
      }
    }
  }

private:
  // The file operand of `exec` that has the content of `entry` and is at
  // the path this pass looks at. Among operands at other paths, one with
  // the result's file name is preferred, which is the file a person would
  // say was copied.
  std::optional<mlir::Value> operandWithContent(ExecOp exec,
                                                const Entry &entry) const {
    const auto name = std::filesystem::path(entry.path).filename();
    std::optional<mlir::Value> found;
    for (const auto input : exec.getInputs()) {
      const auto operand = *entryOf(input);
      if (operand.kind != EntryKind::File || operand.content != entry.content) {
        continue;
      }
      const bool samePath = operand.path == entry.path;
      if (source == Source::SamePath) {
        if (samePath) {
          return input;
        }
        continue;
      }
      if (samePath) {
        continue;
      }
      if (std::filesystem::path(operand.path).filename() == name) {
        return input;
      }
      if (!found) {
        found = input;
      }
    }
    return found;
  }

  Source source;
};

// Raises a process group to the kind of build rule it stands for. A group
// that is one command is looked up in the recording, and the command's
// program and arguments say whether it compiles, links or archives. The
// kind is written on the splice.exec as `rule`.
//
// Nothing about the graph changes: the operation keeps its operands, its
// results and its step, and is replayed and checked as before. The kind
// only chooses the name of the rule the operation is lowered to and how
// that rule names its inputs. Tool names are used here and nowhere else in
// the compiler, and a name that is misread gives a correct rule a wrong
// label.
struct RaiseToRules
    : mlir::PassWrapper<RaiseToRules, mlir::OperationPass<mlir::ModuleOp>> {
  explicit RaiseToRules(const Recording &recording) : recording(recording) {}

  llvm::StringRef getArgument() const final { return "splice-raise-to-rules"; }
  llvm::StringRef getDescription() const final {
    return "Name the kind of build rule each command stands for";
  }

  void runOnOperation() final {
    // The recorded groups by the step they started at.
    std::map<uint64_t, const Group *> groups;
    for (const auto &group : recording.groups) {
      groups[group.start] = &group;
    }

    mlir::Builder builder(&getContext());
    for (auto exec : processGroups(getOperation())) {
      // An operation written by hand has no group to read a command
      // from. A pipeline is several commands, and no one of them is the
      // rule.
      const auto group = groups.find(exec.getStep());
      if (group == groups.end() || group->second->processes.size() != 1) {
        continue;
      }
      const auto kind = ruleKind(group->second->processes.front());
      if (!kind) {
        continue;
      }
      exec.setRuleAttr(builder.getStringAttr(std::string(*kind)));
    }
  }

private:
  const Recording &recording;
};
} // namespace

std::string_view passName(Pass pass) {
  switch (pass) {
  case Pass::Link:
    return kLinkName;
  case Pass::Eliminate:
    return kEliminateName;
  case Pass::Fold:
    return kFoldName;
  case Pass::Merge:
    return kMergeName;
  case Pass::Raise:
    return kRaiseName;
  case Pass::Rules:
    return kRulesName;
  }
  throw std::logic_error("unknown pass");
}

std::optional<Pass> passNamed(std::string_view name) {
  for (const auto pass : kAllPasses) {
    if (passName(pass) == name) {
      return pass;
    }
  }
  return std::nullopt;
}

void optimize(mlir::ModuleOp module, std::span<const Pass> passes,
              const Recording *recording) {
  const auto chosen = [&](Pass pass) {
    return std::ranges::find(passes, pass) != passes.end();
  };
  mlir::PassManager manager(module.getContext());
  for (const auto pass : kAllPasses) {
    if (!chosen(pass)) {
      continue;
    }
    switch (pass) {
    case Pass::Link:
      manager.addPass(std::make_unique<LinkDriverWrites>());
      break;
    case Pass::Eliminate:
      break;
    case Pass::Fold:
      manager.addPass(std::make_unique<FoldConstants>());
      break;
    case Pass::Merge:
      manager.addPass(std::make_unique<RaiseCopies>(Source::SamePath));
      break;
    case Pass::Raise:
      manager.addPass(std::make_unique<RaiseCopies>(Source::OtherPath));
      break;
    case Pass::Rules:
      if (recording != nullptr) {
        manager.addPass(std::make_unique<RaiseToRules>(*recording));
      }
      break;
    }

    // Elimination is MLIR's canonicalizer. Every splice operation but the
    // output is Pure, so the canonicalizer deletes the ones whose results
    // nothing uses: processes that only looked at the filesystem,
    // temporary files, the builder's own bookkeeping. It runs where
    // elimination comes in the pipeline and again after each later pass,
    // which takes the last reader from more operations. It also makes a
    // copy of a copy name the original. Linking comes before elimination
    // and is not followed by it.
    if (chosen(Pass::Eliminate) && pass != Pass::Link) {
      manager.addPass(mlir::createCanonicalizerPass());
    }
  }
  check(mlir::succeeded(manager.run(module)), "cannot optimize module");
}
} // namespace splice_ir

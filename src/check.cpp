#include "absl/strings/str_cat.h"
#include "compiler.h"
#include "layer.h"
#include "lower.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>
#include <set>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
using Json = nlohmann::json;

// The single layer a checked group is given: its operands, straight from
// the recording.
constexpr std::string_view kRecordedLayer = "recorded";

constexpr std::string_view kReportFile = "check.json";

// How many consecutive passes over every live group must reproduce the
// recording without an adjustment before the verdicts are accepted.
constexpr size_t kCleanPasses = 2;
constexpr int kJsonIndent = 1;

// One live splice.exec, ready to replay without the module.
struct Task {
  uint64_t step = 0;
  Verdict verdict;
  Plan plan;
  std::vector<Entry> operands;

  // Unset when the replay reproduced the recording.
  std::optional<ReplayFailure> failure;
};

// The next verdict to try after a failed replay.
//
// A group that fails with narrow inputs may have depended on a path it
// never opened, so the first response is always to give it everything.
// With every input present, a process that still ends differently cannot
// be replayed at all. One that ends correctly but writes some files
// differently is nondeterministic in exactly those files, and they are
// pinned to the recording.
Verdict adjust(Verdict verdict, const ReplayFailure &failure) {
  if (verdict.rung == Rung::Narrow) {
    verdict.rung = Rung::Wide;
    return verdict;
  }
  if (failure.wrongStatus || failure.differing.empty()) {
    verdict.rung = Rung::Literal;
    return verdict;
  }
  verdict.pinned.insert(failure.differing.begin(), failure.differing.end());
  return verdict;
}

// Replays one group against recorded inputs in a private directory.
void replayAlone(const Recording &recording, Task &task) {
  Scratch scratch;
  const auto extension = std::string(kLayerExtension);
  const auto recorded =
      scratch.path / (std::string(kRecordedLayer) + extension);
  const auto platform =
      scratch.path / (std::string(kPlatformLayer) + extension);
  writeRecordedLayer(recording, task.operands, recorded);

  std::vector<Entry> platformEntries;
  for (const auto &[path, entry] : recording.platform()) {
    platformEntries.push_back(entry);
  }
  writeRecordedLayer(recording, platformEntries, platform);
  try {
    const std::vector<fs::path> layers{recorded, platform};
    replayPlan(task.plan, layers, scratch.path / "result");
  } catch (const ReplayFailure &failure) {
    task.failure = failure;
  } catch (const std::exception &error) {
    // Anything else, such as a sandbox that could not start, says nothing
    // about which results differ.
    task.failure = ReplayFailure(error.what());
    task.failure->wrongStatus = true;
  }
}
} // namespace

void checkRecording(const Recording &recording) {
  auto verdicts = loadVerdicts(recording);
  std::set<std::pair<uint64_t, Verdict>> reproduced;
  std::map<uint64_t, std::string> failures;

  // A pass replays every group the outputs depend on until nothing needs
  // adjusting. One clean pass is one sample of each group, and a group
  // that differs from run to run can pass a single sample by luck. So a
  // pass is repeated, from scratch, until the required number in a row
  // adjust nothing.
  for (size_t clean = 0; clean < kCleanPasses;) {
    reproduced.clear();
    bool adjusted = false;

    // Each round imports with the verdicts so far. A demotion changes which
    // groups are live and what they are given, so repeat until a round
    // demotes nothing.
    for (bool changed = true; changed;) {
      changed = false;
      mlir::MLIRContext context;
      context.getOrLoadDialect<SpliceDialect>();
      auto module = importRecording(context, recording, verdicts);
      optimize(*module);

      std::vector<Task> tasks;
      for (auto &op : module->getBody()->getOperations()) {
        auto exec = llvm::dyn_cast<ExecOp>(op);
        if (!exec) {
          continue;
        }
        Task task;
        task.step = exec.getStep();
        if (const auto known = verdicts.find(task.step);
            known != verdicts.end()) {
          task.verdict = known->second;
        }
        if (recording.lost != 0 && task.verdict.rung == Rung::Narrow) {
          task.verdict.rung = Rung::Wide;
        }
        if (reproduced.contains({task.step, task.verdict})) {
          continue;
        }
        task.plan = execPlan(exec, recording, [](mlir::Value) {
          return std::string(kRecordedLayer);
        });
        for (const auto input : exec.getInputs()) {
          task.operands.push_back(*entryOf(input));
        }
        tasks.push_back(std::move(task));
      }

      parallelFor(tasks.size(),
                  [&](size_t index) { replayAlone(recording, tasks[index]); });
      for (const auto &task : tasks) {
        if (!task.failure) {
          reproduced.insert({task.step, task.verdict});
          continue;
        }
        verdicts[task.step] = adjust(task.verdict, *task.failure);
        failures[task.step] = task.failure->what();
        changed = true;
        adjusted = true;
        llvm::errs() << "step " << task.step << " failed at "
                     << rungName(task.verdict.rung) << ": "
                     << task.failure->what() << "\n";
      }
    }
    clean = adjusted ? 0 : clean + 1;
  }
  saveVerdicts(recording, verdicts);

  // Report how each group ended up in the graph the verdicts produce.
  mlir::MLIRContext context;
  context.getOrLoadDialect<SpliceDialect>();
  auto module = importRecording(context, recording, verdicts);
  optimize(*module);
  std::set<uint64_t> replayedSteps;
  std::set<uint64_t> demotedSteps;
  for (auto &op : module->getBody()->getOperations()) {
    if (auto exec = llvm::dyn_cast<ExecOp>(op)) {
      replayedSteps.insert(exec.getStep());
    }
    if (auto literal = llvm::dyn_cast<LiteralOp>(op);
        literal && literal.getDemoted()) {
      demotedSteps.insert(literal.getStep());
    }
  }

  Json groups = Json::array();
  size_t kept = 0;
  for (const auto &group : recording.groups) {
    Verdict verdict;
    if (const auto known = verdicts.find(group.start);
        known != verdicts.end()) {
      verdict = known->second;
    }

    // A group the optimized graph does not contain is dead: no output
    // depends on anything the group wrote.
    std::string outcome = "dead";
    const bool replayed = replayedSteps.contains(group.start);
    if (replayed) {
      outcome = std::string(rungName(verdict.rung));
    } else if (demotedSteps.contains(group.start)) {
      outcome = std::string(rungName(Rung::Literal));
    }
    Json item = {
        {"step", group.start},
        {"name", label(group)},
        {"processes", group.processes.size()},
        {"outcome", outcome},
        {"pinned", replayed ? verdict.pinned : std::set<std::string>{}}};
    if (!replayed && demotedSteps.contains(group.start)) {
      ++kept;
      const auto structural = unreplayable(group);
      item["reason"] = structural ? *structural : failures[group.start];
    }
    groups.push_back(std::move(item));
  }
  writeBytes(recording.directory / kReportFile,
             Json{{"groups", groups}}.dump(kJsonIndent) + "\n");
  llvm::errs() << recording.groups.size()
               << " process groups: " << replayedSteps.size()
               << " replayed and reproduced, " << kept
               << " kept as recorded bytes, "
               << recording.groups.size() - replayedSteps.size() - kept
               << " dead\n";
}
} // namespace splice_ir

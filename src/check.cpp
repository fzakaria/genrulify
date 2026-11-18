#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "access.h"
#include "compiler.h"
#include "layer.h"
#include "lower.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>
#include <regex>
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
// never opened, so the first responses are to give it more: the neighbours
// of what it opened, then everything. With every input present, a process
// that still ends differently cannot be replayed at all. One that ends
// correctly but writes some files differently is nondeterministic in exactly
// those files, and they are pinned to the recording.
Verdict adjust(Verdict verdict, const ReplayFailure &failure) {
  if (verdict.rung == Rung::Narrow) {
    verdict.rung = Rung::Near;
    return verdict;
  }

  // Inputs taken from a trace were a guess. Go back to the rung that was
  // known to work.
  if (verdict.rung == Rung::Traced) {
    verdict.rung = verdict.fallback;
    return verdict;
  }
  if (verdict.rung == Rung::Near) {
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
void replayAlone(const Recording &recording, Task &task,
                 const fs::path &trace = {}) {
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
    replayPlan(task.plan, layers, scratch.path / "result", trace);
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

bool learnFromBuild(const Recording &recording, std::istream &log) {
  auto verdicts = loadVerdicts(recording);
  const auto before = verdicts;

  // A failure is a headline naming the rule, `s<step>_<label>`, followed by
  // one indented line per problem. Build tools prefix and interleave their
  // own output, so the headline is searched for anywhere in a line.
  static const std::regex headline(
      absl::StrCat("s([0-9]+)_\\S*", std::string(kNotReproduced)));
  std::optional<uint64_t> step;
  for (std::string line; std::getline(log, line);) {
    std::smatch match;
    if (std::regex_search(line, match, headline)) {
      step = std::stoull(match[1].str());
      continue;
    }
    if (!step) {
      continue;
    }
    const std::string_view problem = absl::StripLeadingAsciiWhitespace(line);
    auto &verdict = verdicts[*step];
    if (problem.starts_with(kDiffersPrefix)) {
      const auto end = problem.find(kDiffersSeparator);
      verdict.pinned.emplace(
          problem.substr(kDiffersPrefix.size(), end - kDiffersPrefix.size()));
      continue;
    }
    if (problem.starts_with(kMissingPrefix)) {
      verdict.pinned.emplace(problem.substr(kMissingPrefix.size()));
      continue;
    }
    if (problem.find(kExitedWith) != std::string_view::npos) {
      verdict.rung = Rung::Literal;
      continue;
    }

    // Anything else ends this failure's list of problems.
    step.reset();
  }

  // A verdict created only by looking it up is still the default.
  std::erase_if(verdicts,
                [](const auto &entry) { return entry.second == Verdict{}; });
  if (verdicts == before) {
    return false;
  }
  saveVerdicts(recording, verdicts);
  return true;
}

void checkRecording(const Recording &recording) {
  auto verdicts = loadVerdicts(recording);
  std::set<std::pair<uint64_t, Verdict>> reproduced;
  std::map<uint64_t, std::string> failures;

  // The live splice.exec operations under the current verdicts, each as a
  // task that can be replayed without the module.
  const auto liveTasks = [&] {
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
      task.plan = execPlan(exec, recording, [](mlir::Value) {
        return std::string(kRecordedLayer);
      });
      for (const auto input : exec.getInputs()) {
        task.operands.push_back(*entryOf(input));
      }
      tasks.push_back(std::move(task));
    }
    return tasks;
  };

  // A pass replays every group the outputs depend on until nothing needs
  // adjusting. One clean pass is one sample of each group, and a group
  // that differs from run to run can pass a single sample by luck. So a
  // pass is repeated, from scratch, until the required number in a row
  // adjust nothing.
  const auto converge = [&] {
    for (size_t clean = 0; clean < kCleanPasses;) {
      reproduced.clear();
      bool adjusted = false;

      // A demotion changes which groups are live and what they are given,
      // so repeat until a round demotes nothing.
      for (bool changed = true; changed;) {
        changed = false;
        auto tasks = liveTasks();
        std::erase_if(tasks, [&](const Task &task) {
          return reproduced.contains({task.step, task.verdict});
        });
        parallelFor(tasks.size(), [&](size_t index) {
          replayAlone(recording, tasks[index]);
        });
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
  };

  // Rewind's log holds opens. A group that needed its directories or the
  // whole tree was missing a stat or a listing, and a trace of the group's
  // own replay has those. Replay each such group once under strace, cut
  // its inputs down to what the trace shows it touching, and let the next
  // convergence decide whether the smaller set is enough. Returns whether
  // any group was narrowed.
  const auto narrow = [&] {
    auto tasks = liveTasks();
    std::erase_if(tasks, [](const Task &task) {
      const bool wider =
          task.verdict.rung == Rung::Near || task.verdict.rung == Rung::Wide;
      return !wider || task.verdict.traced;
    });
    std::vector<Accessed> accesses(tasks.size());
    parallelFor(tasks.size(), [&](size_t index) {
      Scratch trace;
      replayAlone(recording, tasks[index], trace.path);
      if (!tasks[index].failure) {
        accesses[index] = readAccesses(trace.path);
      }
    });

    for (size_t index = 0; index < tasks.size(); ++index) {
      const auto &task = tasks[index];
      auto &verdict = verdicts[task.step];
      verdict = task.verdict;
      verdict.traced = true;

      // A replay that fails under the tracer says nothing about inputs.
      if (task.failure) {
        continue;
      }
      const auto before = recording.state(task.step);
      const auto &accessed = accesses[index];
      for (const auto &path : accessed.paths) {
        if (before.contains(path)) {
          verdict.inputs.insert(path);
        }
      }
      for (const auto &[path, entry] : before) {
        const auto parent = fs::path(path).parent_path().string();
        if (accessed.listed.contains(path) ||
            accessed.listed.contains(parent)) {
          verdict.inputs.insert(path);
        }
      }
      verdict.fallback = task.verdict.rung;
      verdict.rung = Rung::Traced;
    }
    return !tasks.empty();
  };

  converge();
  while (narrow()) {
    converge();
  }
  saveVerdicts(recording, verdicts);

  // Report how each group ended up in the graph the verdicts produce.
  mlir::MLIRContext context;
  context.getOrLoadDialect<SpliceDialect>();
  auto module = importRecording(context, recording, verdicts);
  optimize(*module);
  std::set<uint64_t> replayedSteps;
  std::set<uint64_t> demotedSteps;

  // How many entries each replayed group is given.
  std::map<uint64_t, size_t> inputCounts;
  for (auto &op : module->getBody()->getOperations()) {
    if (auto exec = llvm::dyn_cast<ExecOp>(op)) {
      replayedSteps.insert(exec.getStep());
      inputCounts[exec.getStep()] = exec.getInputs().size();
    }
    if (auto literal = llvm::dyn_cast<LiteralOp>(op);
        literal && literal.getDemoted()) {
      demotedSteps.insert(literal.getStep());
    }
  }

  Json groups = Json::array();
  size_t keptCount = 0;
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
    const auto structural = unreplayable(group);

    // A group that is not replayed but still has entries in the graph is
    // kept as recorded bytes, whether replay rejected the whole group or
    // only the one file something else depends on.
    const bool kept = !replayed && demotedSteps.contains(group.start);
    if (replayed) {
      outcome = std::string(rungName(verdict.rung));
    } else if (kept) {
      outcome = std::string(rungName(Rung::Literal));
    }
    Json item = {{"step", group.start},
                 {"name", label(group)},
                 {"processes", group.processes.size()},
                 {"outcome", outcome},
                 {"inputs", replayed ? inputCounts[group.start] : 0},
                 {"pinned", replayed ? verdict.pinned : std::set<std::string>{}}};
    if (kept) {
      ++keptCount;
      item["reason"] = structural ? *structural : failures[group.start];
    }
    groups.push_back(std::move(item));
  }
  writeBytes(recording.directory / kReportFile,
             Json{{"groups", groups}}.dump(kJsonIndent) + "\n");
  llvm::errs() << recording.groups.size()
               << " process groups: " << replayedSteps.size()
               << " replayed and reproduced, " << keptCount
               << " kept as recorded bytes, "
               << recording.groups.size() - replayedSteps.size() - keptCount
               << " dead\n";
}
} // namespace splice_ir

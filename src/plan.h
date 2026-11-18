#pragma once

#include "recording.h"
#include <optional>
#include <stdexcept>

namespace splice_ir {
// A replay runs natively and the recording ran in a VM, so a group that
// needs many times the whole build's recorded wall time is stuck. The
// floor covers recordings too short to scale from.
inline constexpr uint64_t kTimeoutFactor = 10;
inline constexpr uint64_t kMinimumTimeoutSeconds = 300;

// One entry an action is given, and the layer that holds the entry.
struct PlanInput {
  std::string layer;
  std::string path;
};

// Everything one build action needs besides its input layers. A replay
// plan has processes and results. A pack plan has an output path instead.
struct Plan {
  std::string name;

  // Store paths mounted read-only, and the layer holding /bin and /etc.
  Arguments closure;
  std::string platform;

  // The processes to start together. Each one's environment index points
  // into `environments`.
  std::vector<Process> processes;
  std::vector<Arguments> environments;

  std::vector<PlanInput> inputs;

  // The entries the processes must leave behind, checked against what they
  // do leave behind before any of it is passed on.
  std::vector<Entry> results;

  // For a pack plan, the store path to serialize as a NAR.
  std::string output;

  // How long the processes may run before the replay is abandoned.
  uint64_t timeoutSeconds = kMinimumTimeoutSeconds;
};

void savePlan(const Plan &plan, const std::filesystem::path &file);
Plan loadPlan(const std::filesystem::path &file);

// The processes and environments of a group, copied into a plan.
void addProcesses(Plan &plan, const Recording &recording, const Group &group);

// Why a group cannot be replayed whatever its inputs are, or nothing when
// replay can at least be attempted.
std::optional<std::string> unreplayable(const Group &group);

// A short label for a group, made of the names of its processes.
std::string label(const Group &group);

// Whether a descriptor is open for reading or for writing, from the
// permission bits of its /proc link.
bool readable(const Descriptor &descriptor);
bool writable(const Descriptor &descriptor);
bool isPipe(const Descriptor &descriptor);

// Why a replay did not reproduce its plan.
struct ReplayFailure : std::runtime_error {
  using std::runtime_error::runtime_error;

  // Whether a process ended differently than the recording saw it end.
  bool wrongStatus = false;

  // The results that were missing or had different content.
  std::vector<std::string> differing;
};

// Runs a replay plan in a sandbox and writes the results as a layer.
// Throws a ReplayFailure with every difference when the processes did not
// reproduce the plan's results.
void replayPlan(const Plan &plan, const std::vector<std::filesystem::path> &layers,
                const std::filesystem::path &output);

// Assembles a pack plan's inputs and writes the output path's NAR.
void packPlan(const Plan &plan, const std::vector<std::filesystem::path> &layers,
              const std::filesystem::path &output);

// The name every plan gives the layer that holds the guest's /bin and /etc.
inline constexpr std::string_view kPlatformLayer = "platform";
} // namespace splice_ir

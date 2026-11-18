#pragma once

#include "dialect.h"
#include "recording.h"
#include <functional>
#include <optional>
#include <stdexcept>

namespace splice_ir {
// One entry a step is given or must leave. A regular file's bytes travel
// as an artifact, a file in the build system's tree, named here. A
// directory or a symlink is fully described by its entry and has none.
struct ActionEntry {
  Entry entry;
  std::string artifact;
};

// One build step: what to stage, which recorded processes to start and how
// they are wired together, and what they must leave behind. A replay step
// has processes and results. A pack step has an output path instead.
struct Action {
  std::string name;
  std::vector<Process> processes;
  std::vector<ActionEntry> inputs;
  std::vector<ActionEntry> results;

  // How long the processes may run before the step is abandoned.
  uint64_t timeoutSeconds = 0;

  // For a pack step, the store path to serialize as a NAR.
  std::string output;
};

// Whether a descriptor is open for reading or for writing, from the
// permission bits of its /proc link.
bool readable(const Descriptor &descriptor);
bool writable(const Descriptor &descriptor);
bool isPipe(const Descriptor &descriptor);

// Why a group cannot be replayed whatever its inputs are, or nothing when
// replay can at least be attempted.
std::optional<std::string> unreplayable(const Group &group);

// A short label for a group, made of the names of its processes.
std::string label(const Group &group);

// The build target that carries out an operation: `glue_784` for a literal
// of the builder's own writes, `lit_3614_mktemp` for a demoted group,
// `s2607_c__` for a replayed one and `archive` for an output.
std::string targetName(mlir::Operation *op, size_t outputCount);

// The file that carries a regular file's bytes: the target that wrote the
// version, then the guest path, with every character that is unsafe in a
// Bazel label or a file name written as `=XX`.
std::string artifactName(std::string_view target, std::string_view guestPath);

// The name Buck2 knows an artifact by inside its rule. Buck2 allows fewer
// characters in an output's name than in its path, so dots are escaped too.
std::string buckOutputName(std::string_view target, std::string_view artifact);

// Names the artifact that holds a regular file's bytes.
using ArtifactOf = std::function<std::string(const Entry &)>;

// The step for a splice.exec. `operandArtifact` names the artifact of an
// operand, `platformArtifact` of a file from the guest's /bin and /etc.
// The step asks only for the results that have a use: a write nothing
// reads may differ from run to run.
Action execAction(ExecOp op, const Recording &recording,
                  const std::function<std::string(mlir::Value)> &operandArtifact,
                  const ArtifactOf &platformArtifact);

// The step for a splice.output.
Action outputAction(OutputOp op, size_t outputCount,
                    const std::function<std::string(mlir::Value)> &operandArtifact);

// The arguments that make sandbox.sh carry out a step, after the options
// that say where its inputs and outputs are. They are grouped as they are
// meant to be read: the step's own options, then for each process its
// wiring on one line and its command line on the next.
std::vector<Arguments> actionArguments(const Action &action,
                                       const Recording &recording);

// Writes what a step's command line refers to by name: its manifest, and
// the bytes the builder had written into a pipe for one of its processes.
// Returns the names written, relative to `directory`.
Arguments writeActionFiles(const Action &action,
                           const std::filesystem::path &directory);

// Writes the files every step shares: the sandbox script, the store
// closure and the environments.
void writeSharedFiles(const Recording &recording,
                      const std::filesystem::path &directory);

// The shared files one step reads: the script, the closure and the
// environments of its processes.
Arguments sharedFiles(const Action &action);

// The text of a failed step. A build log is the only channel a failed
// genrule has back to the compiler, so the sandbox script prints these
// phrases and `learn` and the checker parse them.
inline constexpr std::string_view kNotReproduced =
    " did not reproduce the recording:";
inline constexpr std::string_view kDiffersPrefix = "differs ";
inline constexpr std::string_view kDiffersSeparator = ": got ";
inline constexpr std::string_view kMissingPrefix = "missing ";
inline constexpr std::string_view kExitedWith = " exited with ";

// Why a step did not reproduce its recording.
struct ReplayFailure : std::runtime_error {
  using std::runtime_error::runtime_error;

  // Whether a process ended differently than the recording saw it end.
  bool wrongStatus = false;

  // The results that were missing or had different content.
  std::vector<std::string> differing;
};

// Runs a step with the sandbox.sh in `shared`, a directory filled by
// writeSharedFiles, taking artifacts from the recording, and reports how
// the step failed if it did. A non-empty `trace` names a directory to fill
// with a system call trace of every process, for readAccesses.
//
// The shared files are written once and not per step because steps run on
// several threads: a script written by one thread while another forks is
// still open for writing in the fork's child, and the kernel refuses to
// execute a file that is open for writing.
std::optional<ReplayFailure> runAction(const Action &action,
                                       const Recording &recording,
                                       const std::filesystem::path &shared,
                                       const std::filesystem::path &trace = {});

// File names inside an emitted workspace and a checker's scratch directory.
inline constexpr std::string_view kSandboxFile = "sandbox.sh";
inline constexpr std::string_view kClosureFile = "closure.txt";
inline constexpr std::string_view kEnvironmentsDirectory = "env";
inline constexpr std::string_view kActionsDirectory = "actions";
inline constexpr std::string_view kPlatformTarget = "platform";
inline constexpr std::string_view kStampExtension = ".ok";
inline constexpr std::string_view kArchiveExtension = ".nar";
} // namespace splice_ir

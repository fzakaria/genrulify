#pragma once

#include "mlir/IR/BuiltinOps.h"
#include "recording.h"
#include "trace.h"
#include <array>
#include <filesystem>
#include <istream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string_view>

namespace splice_ir {
// How a process group's inputs are chosen, from least to most recorded
// state. Replay moves a group down the list when the group fails.
enum class Rung {
  // Give the group the entries the recording saw the group use.
  Narrow,

  // Also give the group every other entry of the directories it worked
  // in. A stat, an existence test or a directory listing looks at the
  // neighbours of the files a process opens, and none of those calls is in
  // the trace.
  Near,

  // Give the group the entries a trace of its own replay showed it
  // touching. A group reaches this rung from Near or Wide, never directly.
  Traced,

  // Give the group every entry that existed when the group started. A
  // group that depends on a path it never opened, through a stat or a
  // directory listing, passes here.
  Wide,

  // Do not replay the group. Take everything the group wrote from the
  // recording.
  Literal,
};

// What replay has learned about one process group.
struct Verdict {
  Rung rung = Rung::Narrow;

  // Paths the group writes differently on every run, such as a log with a
  // timestamp. Their bytes come from the recording while the rest of the
  // group's writes are still computed.
  std::set<std::string> pinned;

  // For the Traced rung, the paths the group's replay touched.
  std::set<std::string> inputs;

  // Whether narrowing by trace has been attempted, and the rung to return
  // to if the traced inputs do not reproduce the recording.
  bool traced = false;
  Rung fallback = Rung::Wide;

  bool operator==(const Verdict &) const = default;
  auto operator<=>(const Verdict &) const = default;
};

// The verdict for each group replay has had to adjust, by the group's
// start step. A group without one is Narrow with nothing pinned.
using Verdicts = std::map<uint64_t, Verdict>;
Verdicts loadVerdicts(const Recording &recording);
void saveVerdicts(const Recording &recording, const Verdicts &verdicts);
std::string_view rungName(Rung rung);

// Pulls everything the compiler needs about a Rewind run into a directory.
// `open` names the programs to treat as driver code and `granularity`
// says whether to find more of them, as trace.h describes.
//
// `project` is the directory of a project to convert in place, or nothing.
// With one, the derivation's `src` has to be a source tree that the
// directory still holds, and the recording keeps that tree as part of the
// build: its entries, and which of them each command read.
void recordRun(const std::string &run, const std::filesystem::path &drv,
               const std::filesystem::path &directory, const std::string &open,
               Granularity granularity = Granularity::Fine,
               const std::optional<std::filesystem::path> &project = {});

// A number of files and how many bytes they hold together.
struct FileCount {
  size_t files = 0;
  uint64_t bytes = 0;

  FileCount &operator+=(const FileCount &other) {
    files += other.files;
    bytes += other.bytes;
    return *this;
  }
};

// Where the entries of one derivation output come from in a graph. The
// counts of process groups do not say: a file the builder's shell wrote
// is in no group, and one command can write most of an output.
struct OutputOrigins {
  // The output's name, such as `out`, and its store path.
  std::string name;
  std::string path;

  // Files a command that runs wrote, and copies of those.
  FileCount computed;

  // Files of a project's own source tree.
  FileCount source;

  // Files carried as recorded bytes: what the builder wrote itself, and
  // what a command that is not run wrote.
  FileCount recorded;

  // Each recorded file's path and why the file is recorded.
  std::vector<std::pair<std::string, std::string>> recordedFiles;

  // Directories, symlinks and empty files.
  size_t withoutBytes = 0;
};

// How many recorded files a report names for one output before saying how
// many more there are.
inline constexpr size_t kListedOrigins = 20;

// The origins of every output of a graph, in the order of the outputs.
std::vector<OutputOrigins> outputOrigins(mlir::ModuleOp module,
                                         const Recording &recording);

// The origins as text: a line for each output, and under the line the
// files that were carried as recorded bytes.
std::string originsText(const std::vector<OutputOrigins> &outputs);

// A file of a project's graph that is recorded bytes and still in use. A
// rebuild after an edit gives the file the bytes of the day of the
// recording, whether or not the real build would. Nothing in a recording
// says which, so each one is reported.
struct FrozenFile {
  std::string path;

  // Why the file is recorded bytes.
  std::string reason;

  // How many commands read the file, and the outputs the file is in.
  size_t commands = 0;
  std::vector<std::string> outputs;
};

// The recorded files in use in a project's graph, in the order they were
// written. Empty for a graph without a source tree.
std::vector<FrozenFile> frozenFiles(mlir::ModuleOp module);

// The list as text under a heading, or nothing when the list is empty.
std::string frozenText(const std::vector<FrozenFile> &frozen);

// The groups the processes of a recording fall into, worked out again
// from each process's lifetime and descriptors. Recording a run groups
// its commands this way. The groups come back without their reads.
std::vector<Group> regroup(const Recording &recording);

// Checks that a directory holds a source tree: every file of the tree is
// in the directory with the same bytes, every symlink with the same
// target, and every directory as a directory. The directory may hold
// more. Throws with the first path that is missing or differs.
void checkSameSource(const std::filesystem::path &tree,
                     const std::filesystem::path &directory);

// Builds the splice module for a recording: one splice.exec per process
// group, splice.literal for what the builder wrote itself and for whatever
// the verdicts take from the recording, and one splice.output per
// derivation output.
mlir::OwningOpRef<mlir::ModuleOp> importRecording(mlir::MLIRContext &context,
                                                  const Recording &recording,
                                                  const Verdicts &verdicts);

// The rewrites of a module, in the order the pipeline runs them. Each one
// leaves every value with the entry the recording gave the value, which is
// all the composition proof in docs/theory.md asks of a graph.
enum class Pass {
  // Turns a file the driver wrote itself, with the bytes of a file that
  // already exists, into a copy of that file. A driver that links or
  // renames a compiled file in-process leaves one.
  Link,

  // Removes every operation the outputs do not depend on.
  Eliminate,

  // Takes results that need no bytes computed, which are directories,
  // symlinks and empty files, out of their process group. A group left
  // with no other result is no longer run.
  Fold,

  // Turns a result with the bytes of the operand at the same path into a
  // copy of the operand: a chmod, or a rewrite that changed nothing.
  Merge,

  // Turns a result with the bytes of an operand at another path into a
  // copy of the operand: cp, install, mv.
  Raise,

  // Gives a process group that is one command the kind of build rule the
  // command stands for: a compile, a link, an archive. The kind names the
  // rule the group is lowered to. The rule runs the recorded command
  // whatever its kind is.
  Rules,
};
inline constexpr std::array kAllPasses{Pass::Link,  Pass::Eliminate,
                                       Pass::Fold,  Pass::Merge,
                                       Pass::Raise, Pass::Rules};

// The name of the literal that holds a project's source tree.
inline constexpr std::string_view kSourceName = "source";

// The file in a recording directory where `check` reports what became of
// every process group, and with which passes.
inline constexpr std::string_view kReportFile = "check.json";

// What a report says of a group that is not replayed: its results in use
// are copies of files it was given, or constants, or recorded bytes, or
// nothing reads anything it wrote.
inline constexpr std::string_view kCopyOutcome = "copy";
inline constexpr std::string_view kFoldedOutcome = "folded";
inline constexpr std::string_view kDeadOutcome = "dead";

// The name a pass has on the command line and in reports.
std::string_view passName(Pass pass);
std::optional<Pass> passNamed(std::string_view name);

// Runs the given passes in pipeline order. With Eliminate among them,
// whatever a later pass leaves without a use is removed after that pass.
// Without it nothing is removed, and the operations a pass has taken the
// readers from stay in the module.
//
// Rules reads command lines, which are in the recording and not in the
// module. Without a recording that pass does nothing.
void optimize(mlir::ModuleOp module, std::span<const Pass> passes = kAllPasses,
              const Recording *recording = nullptr);

// Replays each live process group alone against recorded inputs and widens
// or demotes the groups that do not reproduce their recorded writes, until
// every remaining splice.exec has been reproduced. Saves the verdicts.
// `passes` are the rewrites the graph will be lowered with: a group they
// remove is not replayed, and one they leave must be.
void checkRecording(const Recording &recording,
                    std::span<const Pass> passes = kAllPasses);

// The whole pipeline for one derivation: builds a flake installable or a
// .drv in Rewind, records the run, replays every command, and writes into
// `directory` the recording, the optimized graph and the workspace. Prints
// the NAR hash Rewind recorded for each output, which is what a build of
// the workspace has to reproduce.
//
// With `project`, the directory of a project whose derivation takes that
// directory as its source, the build files are written into the project
// and read its files where they are.
void convert(const std::string &installable,
             const std::filesystem::path &directory, const std::string &open,
             Granularity granularity,
             const std::optional<std::filesystem::path> &project = {});

// Reads the output of a failed build of an emitted workspace and adjusts
// the verdicts of the rules that did not reproduce the recording: results
// that differed are pinned, and a group whose process ended differently is
// demoted. A build is a further sample of every rule, taken under the
// build system's own conditions. Returns whether any verdict changed.
bool learnFromBuild(const Recording &recording, std::istream &log);

// Checks that build files can be written into a project: the directory
// exists and has no BUILD.bazel or BUCK that somebody else wrote. Throws
// naming the file that is in the way.
void checkProjectDirectory(const std::filesystem::path &project);

// Writes a Bazel and Buck2 workspace with one genrule per operation.
void emitWorkspace(mlir::ModuleOp module, const Recording &recording,
                   const std::filesystem::path &destination);

// Prints a JSON description of a recorded graph and of every process
// group in its recording, for tools that show a conversion to people.
// `module` is the graph before optimization: the description marks the
// operations optimization removes, so both graphs can be drawn from it.
void describeGraph(mlir::ModuleOp module, const Recording &recording);

// Writes the NAR of a derivation output as the recording saw it when the
// builder exited.
void writeReference(const Recording &recording, const std::string &output,
                    const std::filesystem::path &archive);

// Requires two archives to have the same SHA-256 and prints it.
void verifyArchives(const std::filesystem::path &reference,
                    const std::filesystem::path &archive);
} // namespace splice_ir

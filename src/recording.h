#pragma once

#include "entry.h"
#include "process.h"
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace splice_ir {
// One open descriptor a process held at its execve, as /proc/PID/fd and
// /proc/PID/fdinfo showed it.
struct Descriptor {
  int fd = 0;

  // The link target: a path, or `pipe:[inode]` for a pipe.
  std::string target;

  // The link's permission bits. Owner-read marks a descriptor open for
  // reading and owner-write one open for writing.
  uint32_t mode = 0;

  // The open flags and file offset from fdinfo.
  uint32_t flags = 0;
  uint64_t position = 0;

  // For the read end of a pipe that nothing replayable writes: the bytes
  // that were waiting in the pipe at the execve, in hex. A shell writes a
  // here-string or a short here-document into a pipe before it starts the
  // command, so these bytes are the command's whole input. Unset when the
  // recording did not capture them, which is not the same as a pipe that
  // was captured and held nothing: that one is at end of file.
  std::optional<std::string> input;
};

// The file-creation mask a Nix build starts its builder with, and so the
// mask of a process in a recording made before masks were kept.
inline constexpr uint32_t kBuilderUmask = 022;

// One process the builder started, at the step of the execve that turned
// the process into something other than the builder's own program.
struct Process {
  int pid = 0;
  uint64_t step = 0;

  // The step by which the process and its descendants had exited, and the
  // wait status the process ended with.
  uint64_t exit = 0;
  int status = 0;

  // The last name of the path passed to execve, for labels, and the path
  // itself, which is empty in recordings made before it was kept.
  std::string name;
  std::string filename;

  // The program the kernel loaded. For a script this path is the
  // interpreter, and argv already carries the script's path.
  std::string exe;
  std::string cwd;
  Arguments argv;

  // The file-creation mask the process had at its execve. A builder that
  // calls `umask` changes the mode of every file its later commands
  // create.
  uint32_t umask = kBuilderUmask;

  // An index into Recording::environments.
  size_t environment = 0;
  std::vector<Descriptor> fds;
};

nlohmann::json toJson(const Process &process);
Process processFromJson(const nlohmann::json &json);

// Processes that cannot be replayed apart: they share a pipe, or their
// lifetimes overlap so a snapshot cannot separate their writes.
struct Group {
  // The step of the first execve in the group and the step after which
  // every process in the group has exited.
  uint64_t start = 0;
  uint64_t end = 0;
  std::vector<Process> processes;

  // Every regular file below a writable root that a process of the group,
  // or a descendant, opened for reading or executed.
  std::vector<std::string> reads;
};

// Everything the compiler takes from a Rewind run. The directory holds
// recording.json, one state per snapshot step under states/, and file bytes
// by SHA-256 under blobs/. Nothing after `record` talks to Rewind.
struct Recording {
  std::filesystem::path directory;

  // The Rewind run and the derivation the run built.
  std::string run;
  std::string name;

  // Output name to store path, and every store path the builder may read.
  std::map<std::string, std::string> outputs;
  Arguments closure;

  // Directories whose contents the build may change.
  Arguments roots;

  // For a project converted in place, the store path of the source tree
  // the derivation was given: the tree someone is working in, as Nix
  // copied it. Empty for any other recording. The tree is then part of
  // the graph and is not read from the host's store.
  std::string source;

  // The pattern of program names that were treated as driver code.
  std::string open;

  // Whether programs that only run other commands were opened as well.
  // False in a recording made before there was a choice.
  bool fine = false;

  // The step the builder started at, and the step after the builder exited.
  uint64_t initialStep = 0;
  uint64_t finalStep = 0;

  // How long the recorded build took on the wall clock.
  uint64_t wallMilliseconds = 0;

  // Read and write records the guest kernel dropped. A recording that lost
  // records does not know every file a group read.
  uint64_t lost = 0;

  std::vector<Arguments> environments;
  std::vector<Group> groups;

  // The entries below the writable roots at a snapshot step, and those of
  // the source tree, which are the same at every step.
  State state(uint64_t step) const;

  // The entries of the source tree, or none when there is no source tree.
  State sourceState() const;

  // The source tree's entries as `load` read them. The tree is the same
  // at every step and every snapshot includes it, so a loaded recording
  // parses the tree once and not once per snapshot. Unset in a recording
  // that was not loaded, which reads the tree when asked.
  std::shared_ptr<const State> loadedSource;

  // /bin and /etc as the guest had them when the builder started.
  State platform() const;

  // The file holding the bytes with this SHA-256.
  std::filesystem::path blob(const std::string &digest) const;

  // The group whose first execve happened at `step`.
  const Group &group(uint64_t step) const;

  void save() const;
  static Recording load(const std::filesystem::path &directory);
};

// File names inside a recording directory.
inline constexpr std::string_view kRecordingFile = "recording.json";
inline constexpr std::string_view kStatesDirectory = "states";
inline constexpr std::string_view kBlobsDirectory = "blobs";
inline constexpr std::string_view kPlatformState = "platform";
inline constexpr std::string_view kSourceState = "source";

// The directories of the guest that are part of the execution platform.
inline constexpr std::string_view kPlatformRoots[] = {"/bin", "/etc"};

// The entries that differ between two states. `changed` holds the new
// version of every created or modified path and `removed` every path that
// no longer exists.
struct Difference {
  std::vector<Entry> changed;
  std::vector<std::string> removed;
};
Difference difference(const State &before, const State &after);

void writeState(const std::filesystem::path &file, const State &state);
} // namespace splice_ir

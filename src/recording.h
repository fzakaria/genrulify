#pragma once

#include "entry.h"
#include "process.h"
#include <filesystem>
#include <map>
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
};

// One process the builder started, at the step of the execve that turned
// the process into something other than the builder's own program.
struct Process {
  int pid = 0;
  uint64_t step = 0;

  // The step by which the process and its descendants had exited, and the
  // wait status the process ended with.
  uint64_t exit = 0;
  int status = 0;

  // The last name of the path passed to execve, for labels.
  std::string name;

  // The program the kernel loaded. For a script this path is the
  // interpreter, and argv already carries the script's path.
  std::string exe;
  std::string cwd;
  Arguments argv;

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

  // The step the builder started at, and the step after the builder exited.
  uint64_t initialStep = 0;
  uint64_t finalStep = 0;

  // Read and write records the guest kernel dropped. A recording that lost
  // records does not know every file a group read.
  uint64_t lost = 0;

  std::vector<Arguments> environments;
  std::vector<Group> groups;

  // The entries below the writable roots at a snapshot step.
  State state(uint64_t step) const;

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

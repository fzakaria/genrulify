#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace splice_ir {
// A process the builder started and the stretch of the run the process and
// its descendants were alive for. A cut is where the build can be split:
// everything inside one is replayed by starting the one process.
struct Cut {
  int pid = 0;

  // The step of the execve that began the cut and the step by which the
  // process and every descendant had exited.
  uint64_t step = 0;
  uint64_t end = 0;

  // The wait status of the process.
  int status = 0;

  // The last name of the path passed to execve.
  std::string name;

  // Every path the process or a descendant opened for reading or executed,
  // each once, in the order first seen.
  std::vector<std::string> reads;
};

// What the event stream and the read and write records say about a run.
struct Analysis {
  // The step of the builder's execve and the step of the builder's exit.
  uint64_t initialStep = 0;
  uint64_t finalStep = 0;

  // The program the builder's execve named.
  std::string builder;

  // Records the guest kernel had no room for.
  uint64_t lost = 0;

  // Cuts in the order they began.
  std::vector<Cut> cuts;
};

// Reads `rewind events --json` output and a run's trace.bin.
//
// The builder is the first process after init to call execve. The builder
// and the children it forks without an execve are the driver: shell code
// that runs in-process and leaves no command to replay. A cut begins where
// a driver process calls execve, which covers a child running a command, a
// subshell that ends in `exec`, and a shell that replaces itself with its
// last command.
Analysis analyze(const std::filesystem::path &events,
                 const std::filesystem::path &trace);
} // namespace splice_ir

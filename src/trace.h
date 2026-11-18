#pragma once

#include <compare>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace splice_ir {
// One program in one process: the process and the step of the execve that
// started the program.
struct Image {
  int pid = 0;
  uint64_t step = 0;
  auto operator<=>(const Image &) const = default;
};

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

  // The path passed to execve, and its last name.
  std::string filename;
  std::string name;

  // Every path the process or a descendant opened for reading or executed,
  // each once, in the order first seen.
  std::vector<std::string> reads;

  // The nearest image above the cut that is driver code only because the
  // trace shows it running other commands. Closing that image makes the
  // image the cut instead. Empty when the builder, or a program opened by
  // name, started the cut.
  std::optional<Image> opener;
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

// How far down the process tree the cuts go.
enum class Granularity {
  // Stop at the commands the builder itself runs, and at what is below
  // the programs `open` names.
  Coarse,

  // Also treat as driver code every process that runs other commands and
  // opens no file for writing itself: make, a recipe shell, xargs, a
  // wrapper script. All that such a process did is what its children did.
  Fine,
};

// Reads `rewind events --json` output and a run's trace.bin.
//
// The builder is the first process after init to call execve. The builder
// and the children it forks without an execve are the driver: shell code
// that runs in-process and leaves no command to replay. A cut begins where
// a driver process calls execve, which covers a child running a command, a
// subshell that ends in `exec`, and a shell that replaces itself with its
// last command.
//
// `open` is a regular expression over program names, or empty. A driver
// process that execs a matching program stays a driver, and so does one
// that execs a shell which later becomes, or has below it, a matching
// process. The cuts fall on the first programs with nothing opened beneath
// them. Opening `ninja` turns one cut for a whole build phase into one cut
// per command.
//
// With Granularity::Fine a program needs no naming when the trace shows
// that the program only ran other commands. A program that also writes
// files of its own, as ninja does its log and cargo its fingerprints, is
// still opened by name alone.
//
// `closed` lists images that must not be opened on the evidence of the
// trace. The recorder closes a driver that fed a command through a pipe:
// what a shell builtin wrote into a pipe is in no file and in no command
// line, so the command can only be replayed as part of the shell.
Analysis analyze(const std::filesystem::path &events,
                 const std::filesystem::path &trace, const std::string &open,
                 Granularity granularity = Granularity::Fine,
                 const std::set<Image> &closed = {});
} // namespace splice_ir

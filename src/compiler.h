#pragma once

#include "mlir/IR/BuiltinOps.h"
#include "recording.h"
#include <filesystem>
#include <map>
#include <set>

namespace splice_ir {
// How a process group's inputs are chosen, from least to most recorded
// state. Replay moves a group down the list when the group fails.
enum class Rung {
  // Give the group the entries the recording saw the group use.
  Narrow,

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
// `open` names the programs to treat as driver code, as trace.h describes.
void recordRun(const std::string &run, const std::filesystem::path &drv,
               const std::filesystem::path &directory, const std::string &open);

// Builds the splice module for a recording: one splice.exec per process
// group, splice.literal for what the builder wrote itself and for whatever
// the verdicts take from the recording, and one splice.output per
// derivation output.
mlir::OwningOpRef<mlir::ModuleOp> importRecording(mlir::MLIRContext &context,
                                                  const Recording &recording,
                                                  const Verdicts &verdicts);

// Removes every operation the outputs do not depend on.
void optimize(mlir::ModuleOp module);

// Replays each live process group alone against recorded inputs and widens
// or demotes the groups that do not reproduce their recorded writes, until
// every remaining splice.exec has been reproduced. Saves the verdicts.
void checkRecording(const Recording &recording);

// Writes a Bazel and Buck2 workspace with one genrule per operation.
void emitWorkspace(mlir::ModuleOp module, const Recording &recording,
                   const std::filesystem::path &destination);

// Prints a JSON description of a graph and of every process group in its
// recording, for tools that show a conversion to people.
void describeGraph(mlir::ModuleOp module, const Recording &recording);

// Writes the NAR of a derivation output as the recording saw it when the
// builder exited.
void writeReference(const Recording &recording, const std::string &output,
                    const std::filesystem::path &archive);

// Requires two archives to have the same SHA-256 and prints it.
void verifyArchives(const std::filesystem::path &reference,
                    const std::filesystem::path &archive);
} // namespace splice_ir

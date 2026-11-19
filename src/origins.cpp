#include "absl/strings/str_cat.h"
#include "compiler.h"
#include "dialect.h"
#include <filesystem>
#include <set>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
// Why a file is carried as recorded bytes, as a report says it: the
// builder wrote the file itself, the file's command is not run, or the
// command runs and this one file of it is taken from the recording.
constexpr std::string_view kBuilderReason = "written by the builder";
constexpr std::string_view kNotRunReason = "from a command that is not run";
constexpr std::string_view kPinnedReason =
    "pinned to the recording while its command runs";

// The heading over the recorded files of a project.
constexpr std::string_view kFrozenHeading =
    "recorded bytes that an edit to the project will not change:\n";

// Whether a literal holds a project's source tree.
bool isSourceTree(LiteralOp literal) {
  return literal.getName().str() == kSourceName && !literal.getDemoted() &&
         !literal.getFolded();
}

// The steps at which a command of the graph runs.
std::set<uint64_t> stepsThatRun(mlir::ModuleOp module) {
  std::set<uint64_t> steps;
  for (auto &op : module.getBody()->getOperations()) {
    if (auto exec = llvm::dyn_cast<ExecOp>(op)) {
      steps.insert(exec.getStep());
    }
  }
  return steps;
}

// Why the files of a literal are recorded bytes.
std::string_view reasonFor(LiteralOp literal,
                           const std::set<uint64_t> &running) {
  if (!literal.getDemoted()) {
    return kBuilderReason;
  }
  return running.contains(literal.getStep()) ? kPinnedReason : kNotRunReason;
}

// Who uses a value: how many commands read it, and which outputs it is
// in. A copy passes its readers on to the file it copies.
struct Users {
  std::set<mlir::Operation *> commands;
  std::set<std::string> outputs;
};

void collectUsers(mlir::Value value, Users &users) {
  for (auto *user : value.getUsers()) {
    if (llvm::isa<ExecOp>(user)) {
      users.commands.insert(user);
    } else if (auto output = llvm::dyn_cast<OutputOp>(user)) {
      users.outputs.insert(output.getName().str());
    } else if (auto copy = llvm::dyn_cast<CopyOp>(user)) {
      collectUsers(copy.getOutput(), users);
    }
  }
}

// The size of a file's bytes, which the recording keeps under their
// digest. A graph written by hand may name bytes the recording has not.
uint64_t sizeOf(const Recording &recording, const Entry &entry) {
  const auto blob = recording.directory / kBlobsDirectory / entry.content;
  std::error_code missing;
  const auto size = fs::file_size(blob, missing);
  return missing ? 0 : size;
}

// "1 file" and "2 files".
std::string counted(size_t count, std::string_view one, std::string_view many) {
  return absl::StrCat(count, " ", count == 1 ? one : many);
}

std::string filesAndBytes(const FileCount &files) {
  return absl::StrCat(counted(files.files, "file", "files"), " (", files.bytes,
                      " bytes)");
}
} // namespace

std::vector<OutputOrigins> outputOrigins(mlir::ModuleOp module,
                                         const Recording &recording) {
  std::vector<OutputOrigins> outputs;
  const auto running = stepsThatRun(module);
  for (auto &op : module.getBody()->getOperations()) {
    auto output = llvm::dyn_cast<OutputOp>(op);
    if (!output) {
      continue;
    }
    OutputOrigins origins;
    origins.name = output.getName().str();
    origins.path = output.getPath().str();
    for (const auto value : output.getEntries()) {
      const auto entry = *entryOf(value);

      // A directory, a symlink and an empty file hold nothing that had to
      // be computed or remembered.
      if (isConstant(entry)) {
        ++origins.withoutBytes;
        continue;
      }

      // A copy has the bytes of the file it copies, so what counts is who
      // defined that file: a command that runs, or a literal.
      const FileCount file{1, sizeOf(recording, entry)};
      auto literal =
          llvm::dyn_cast<LiteralOp>(contentSource(value).getDefiningOp());
      if (!literal) {
        origins.computed += file;
        continue;
      }
      if (isSourceTree(literal)) {
        origins.source += file;
        continue;
      }
      origins.recorded += file;
      origins.recordedFiles.push_back(
          {entry.path, std::string(reasonFor(literal, running))});
    }
    outputs.push_back(std::move(origins));
  }
  return outputs;
}

std::vector<FrozenFile> frozenFiles(mlir::ModuleOp module,
                                    const Recording &recording) {
  // Only a project is built again after its sources change. The recording
  // says whether the build is one, and so does a source tree in the
  // graph. The tree alone would not do: elimination removes it when no
  // command that matters read a source.
  bool project = !recording.source.empty();
  for (auto &op : module.getBody()->getOperations()) {
    auto literal = llvm::dyn_cast<LiteralOp>(op);
    project = project || (literal && isSourceTree(literal));
  }
  std::vector<FrozenFile> frozen;
  if (!project) {
    return frozen;
  }

  // Every file with bytes that a literal other than the source tree
  // defines and something still uses.
  const auto running = stepsThatRun(module);
  for (auto &op : module.getBody()->getOperations()) {
    auto literal = llvm::dyn_cast<LiteralOp>(op);
    if (!literal || isSourceTree(literal)) {
      continue;
    }
    for (const auto result : literal->getResults()) {
      const auto entry = *entryOf(result);
      if (entry.kind != EntryKind::File || isConstant(entry)) {
        continue;
      }
      Users users;
      collectUsers(result, users);
      if (users.commands.empty() && users.outputs.empty()) {
        continue;
      }
      frozen.push_back({entry.path, std::string(reasonFor(literal, running)),
                        users.commands.size(),
                        {users.outputs.begin(), users.outputs.end()}});
    }
  }
  return frozen;
}

std::string frozenText(const std::vector<FrozenFile> &frozen) {
  if (frozen.empty()) {
    return "";
  }
  std::string text(kFrozenHeading);
  for (const auto &file : frozen) {
    absl::StrAppend(&text, "  ", file.path, ": ", file.reason);
    if (file.commands != 0) {
      absl::StrAppend(&text, ", read by ",
                      counted(file.commands, "command", "commands"));
    }
    for (const auto &output : file.outputs) {
      absl::StrAppend(&text, ", in output ", output);
    }
    absl::StrAppend(&text, "\n");
  }
  return text;
}

std::string originsText(const std::vector<OutputOrigins> &outputs) {
  std::string text;
  for (const auto &origins : outputs) {
    absl::StrAppend(&text, origins.name, ": ",
                    filesAndBytes(origins.computed),
                    " from commands that run, ");

    // Only a project's graph has files that are the project's own.
    if (origins.source.files != 0) {
      absl::StrAppend(&text, filesAndBytes(origins.source),
                      " from the project's sources, ");
    }
    absl::StrAppend(&text, filesAndBytes(origins.recorded),
                    " from the recording, ",
                    counted(origins.withoutBytes, "entry", "entries"),
                    " without bytes\n");

    // Name what was remembered and why, since that is the part of the
    // output no build system computed.
    for (size_t index = 0; index < origins.recordedFiles.size(); ++index) {
      if (index == kListedOrigins) {
        absl::StrAppend(&text, "  and ",
                        origins.recordedFiles.size() - kListedOrigins,
                        " more\n");
        break;
      }
      const auto &[path, reason] = origins.recordedFiles[index];
      absl::StrAppend(&text, "  ", path, ": ", reason, "\n");
    }
  }
  return text;
}
} // namespace splice_ir

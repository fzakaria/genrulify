#include "absl/strings/str_cat.h"
#include "compiler.h"
#include "dialect.h"
#include <filesystem>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
// What the builder's own writes and a group that is not run are called in
// a report.
constexpr std::string_view kBuilderReason = "written by the builder";
constexpr std::string_view kNotRunReason = "from a command that is not run";

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
      if (literal.getName().str() == kSourceName && !literal.getDemoted()) {
        origins.source += file;
        continue;
      }
      origins.recorded += file;
      origins.recordedFiles.push_back(
          {entry.path, std::string(literal.getDemoted() ? kNotRunReason
                                                        : kBuilderReason)});
    }
    outputs.push_back(std::move(origins));
  }
  return outputs;
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

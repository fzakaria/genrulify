#include "absl/strings/str_cat.h"
#include "compiler.h"
#include "dialect.h"
#include "llvm/Support/raw_ostream.h"
#include "mlir/IR/Verifier.h"
#include "process.h"
#include "tools.h"

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
using Json = nlohmann::json;

// What a conversion leaves in its directory.
constexpr std::string_view kRecordingDirectory = "recording";
constexpr std::string_view kWorkspaceDirectory = "workspace";
constexpr std::string_view kGraphFile = "graph.mlir";

// The suffix of a derivation's store path, and the attribute of a flake
// installable that holds one.
constexpr std::string_view kDerivationSuffix = ".drv";
constexpr std::string_view kDerivationAttribute = ".drvPath";

// What Rewind puts in front of the SHA-256 of an output's NAR.
constexpr std::string_view kDigestPrefix = "sha256:";

// How Rewind says a build ran to the end and succeeded.
constexpr std::string_view kSuccessfulEnding = "exited:0";

// The file an output is packed into to compare its digest with Rewind's.
constexpr std::string_view kReferenceFile = "reference.nar";

// The store path of the derivation a conversion was asked for: the
// argument itself, or what Nix evaluates a flake installable to.
std::string derivationOf(const std::string &installable) {
  if (installable.ends_with(kDerivationSuffix)) {
    return installable;
  }
  return capture({std::string(kNix), "eval", "--raw",
                  absl::StrCat(installable, kDerivationAttribute)});
}

// The digest and the path of each output Rewind built, after checking that
// the recording holds the same output. The archive rule is held to the
// output as the recording saw it. That has to be the output Rewind built
// and hashed, or the workspace would reproduce something else.
std::vector<std::pair<std::string, std::string>> builtOutputs(
    const Recording &recording, const Json &run) {
  std::vector<std::pair<std::string, std::string>> built;
  for (const auto &output : run.at("outputs")) {
    std::string_view digest = output.at("hash").get_ref<const std::string &>();
    if (digest.starts_with(kDigestPrefix)) {
      digest.remove_prefix(kDigestPrefix.size());
    }
    const std::string path = output.at("path");
    for (const auto &[name, recordedPath] : recording.outputs) {
      if (recordedPath != path) {
        continue;
      }
      Scratch scratch;
      const auto reference = scratch.path / kReferenceFile;
      writeReference(recording, name, reference);
      const auto seen = splice_ir::digest(reference);
      check(seen == digest,
            absl::StrCat("the recording has ", path, " as sha256:", seen,
                         ", and Rewind built sha256:", digest));
    }
    built.emplace_back(digest, path);
  }
  return built;
}
} // namespace

void convert(const std::string &installable, const fs::path &directory,
             const std::string &open, Granularity granularity,
             const std::optional<fs::path> &project,
             const std::optional<fs::path> &shared) {
  // Find out now, and not after the build has been recorded, that the
  // project cannot take the build files.
  if (project) {
    checkProjectDirectory(*project);
  }
  check(fs::create_directory(directory),
        directory.string() + " already exists");
  try {
    // Build the derivation in Rewind, which reports the run and the NAR
    // hash of every output.
    const auto drv = derivationOf(installable);
    const auto run =
        Json::parse(capture({"rewind", "nix", drv, "--quiet", "--json"}));
    check(run.at("ending") == kSuccessfulEnding,
          "the build did not succeed in Rewind");

    // Cut the run, snapshot around every command, and replay each command
    // that the passes leave.
    const auto recorded = directory / kRecordingDirectory;
    recordRun(run.at("id"), drv, recorded, open, granularity, project);
    const auto recording = Recording::load(recorded);

    // Nothing is written for a build system until the recording is known
    // to hold the build Rewind hashed. A project's rules replace the ones
    // of its last conversion, and a conversion that is then rejected must
    // not have touched them.
    const auto built = builtOutputs(recording, run);
    checkRecording(recording);

    // Write the optimized graph and the workspace it lowers to.
    mlir::MLIRContext context;
    context.getOrLoadDialect<SpliceDialect>();
    auto module = importRecording(context, recording, loadVerdicts(recording));
    optimize(*module, kAllPasses, &recording);
    check(mlir::succeeded(mlir::verify(*module)), "invalid module");
    std::string text;
    llvm::raw_string_ostream stream(text);
    module->print(stream);
    writeBytes(directory / kGraphFile, text + "\n");

    // The rules of a project go into the project, next to the
    // sources they name. A build that joins a shared workspace becomes a
    // package there. Any other recorded build gets a workspace of its
    // own.
    if (shared) {
      llvm::outs() << emitPackage(*module, recording, *shared) << "\n";
    } else {
      emitWorkspace(*module, recording,
                    project ? *project : directory / kWorkspaceDirectory);
    }

    // Say what the build has to reproduce, as sha256sum would print it:
    // the digest in hex with nothing in front, then the path.
    for (const auto &[digest, path] : built) {
      llvm::outs() << digest << "  " << path << "\n";
    }
  } catch (...) {
    removeTree(directory);
    throw;
  }
}
} // namespace splice_ir

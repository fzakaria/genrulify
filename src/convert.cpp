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

// How Rewind says a build ran to the end and succeeded.
constexpr std::string_view kSuccessfulEnding = "exited:0";

// The store path of the derivation a conversion was asked for: the
// argument itself, or what Nix evaluates a flake installable to.
std::string derivationOf(const std::string &installable) {
  if (installable.ends_with(kDerivationSuffix)) {
    return installable;
  }
  return capture({std::string(kNix), "eval", "--raw",
                  absl::StrCat(installable, kDerivationAttribute)});
}
} // namespace

void convert(const std::string &installable, const fs::path &directory,
             const std::string &open, Granularity granularity) {
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
    recordRun(run.at("id"), drv, recorded, open, granularity);
    const auto recording = Recording::load(recorded);
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
    emitWorkspace(*module, recording, directory / kWorkspaceDirectory);

    // Say what the build has to reproduce, in the form sha256sum prints.
    for (const auto &output : run.at("outputs")) {
      llvm::outs() << output.at("hash").get<std::string>() << "  "
                   << output.at("path").get<std::string>() << "\n";
    }
  } catch (...) {
    removeTree(directory);
    throw;
  }
}
} // namespace splice_ir

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "compiler.h"
#include "layer.h"
#include "lower.h"
#include <set>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
using Json = nlohmann::json;
enum class Backend { Bazel, Buck2 };

// Workspace-relative directories for the files the genrules read.
constexpr std::string_view kPlansDirectory = "plans";
constexpr std::string_view kLiteralsDirectory = "literals";

// The file a single-output derivation's archive rule writes.
constexpr std::string_view kNarExtension = ".nar";

constexpr std::string_view kBuckConfig = R"([cells]
root = .
prelude = prelude
toolchains = toolchains
none = none
[external_cells]
prelude = bundled
[cell_aliases]
config = prelude
fbcode = none
fbsource = none
buck = none
[parser]
target_platform_detector_spec = target:root//...->prelude//platforms:default
)";

constexpr std::string_view kBuckToolchains =
    "load(\"@prelude//toolchains:demo.bzl\", \"system_demo_toolchains\")\n"
    "system_demo_toolchains()\n";

// One genrule: the labels it consumes, the file it writes and the
// genrulify subcommand that turns one into the other.
struct Action {
  std::string name;
  std::string output;
  std::string verb;

  // The plan file first, then every layer the plan names.
  std::string plan;
  std::vector<std::string> layers;
};

struct Workspace {
  // Files that exist in the workspace, by path relative to its root.
  std::set<std::string> sources;
  std::vector<Action> actions;
};

std::string quote(std::string_view value) { return Json(value).dump(); }

// The label of a source file or of a rule in the workspace's one package.
// Both build systems accept the same spelling.
std::string label(std::string_view name) { return absl::StrCat(":", name); }

std::string rule(Backend backend, const Action &action) {
  // Both systems expand $(location label) to the path of a source file or
  // of another rule's output.
  const auto location = [](const std::string &label) {
    return absl::StrCat("\"$(location ", label, ")\"");
  };
  std::vector<std::string> arguments;
  for (const auto &layer : action.layers) {
    arguments.push_back(location(layer));
  }
  const auto runner = fs::canonical("/proc/self/exe").string();
  const auto target = backend == Backend::Bazel ? "$@" : "$OUT";
  const auto command =
      absl::StrCat("'", runner, "' ", action.verb, " ", location(action.plan),
                   " \"", target, "\" ", absl::StrJoin(arguments, " "));

  std::set<std::string> sources(action.layers.begin(), action.layers.end());
  sources.insert(action.plan);
  const auto output = backend == Backend::Bazel
                          ? absl::StrCat("outs = [", quote(action.output), "]")
                          : absl::StrCat("out = ", quote(action.output));

  // The actions read the host's Nix store, which a remote worker may not
  // have.
  const auto policy =
      backend == Backend::Bazel ? "tags = [\"no-remote\"]" : "remote = False";
  return absl::StrCat("genrule(\n    name = ", quote(action.name),
                      ",\n    srcs = ", Json(sources).dump(), ",\n    ", output,
                      ",\n    cmd = ", quote(command), ",\n    ", policy,
                      ",\n)\n\n");
}

std::string buildFile(Backend backend, const Workspace &workspace) {
  std::string text;

  // Buck2 needs a target for every source file a rule names. Bazel takes
  // the file's path as a label.
  if (backend == Backend::Buck2) {
    for (const auto &source : workspace.sources) {
      absl::StrAppend(&text, "export_file(name = ", quote(source),
                      ", src = ", quote(source), ")\n");
    }
    text += "\n";
  }
  for (const auto &action : workspace.actions) {
    text += rule(backend, action);
  }
  return text;
}
} // namespace

void emitWorkspace(mlir::ModuleOp module, const Recording &recording,
                   const fs::path &destination) {
  check(fs::create_directory(destination), "destination already exists");
  try {
    fs::create_directory(destination / kPlansDirectory);
    fs::create_directory(destination / kLiteralsDirectory);
    const auto extension = std::string(kLayerExtension);
    Workspace workspace;

    // Every replay needs the guest's /bin and /etc.
    const auto platform = std::string(kPlatformLayer) + extension;
    std::vector<Entry> platformEntries;
    for (const auto &[path, entry] : recording.platform()) {
      platformEntries.push_back(entry);
    }
    writeRecordedLayer(recording, platformEntries, destination / platform);
    workspace.sources.insert(platform);
    const auto platformLabel = label(platform);

    size_t outputCount = 0;
    for (auto &op : module.getBody()->getOperations()) {
      outputCount += llvm::isa<OutputOp>(op) ? 1 : 0;
    }

    // The label of the layer that holds a value: a source file for a
    // literal, another rule for an exec.
    llvm::DenseMap<mlir::Operation *, std::string> labels;
    const LayerOf layerOf = [&](mlir::Value value) {
      return targetName(value.getDefiningOp(), outputCount);
    };
    const auto layerLabels = [&](mlir::OperandRange values) {
      std::set<std::string> unique;
      for (const auto value : values) {
        unique.insert(labels.lookup(value.getDefiningOp()));
      }
      return std::vector<std::string>(unique.begin(), unique.end());
    };
    const auto addPlan = [&](const Plan &plan, const std::string &name) {
      const auto file = absl::StrCat(kPlansDirectory, "/", name, ".json");
      savePlan(plan, destination / file);
      workspace.sources.insert(file);
      return label(file);
    };

    for (auto &op : module.getBody()->getOperations()) {
      const auto name = targetName(&op, outputCount);
      if (auto literal = llvm::dyn_cast<LiteralOp>(op)) {
        const auto file =
            absl::StrCat(kLiteralsDirectory, "/", name, extension);
        std::vector<Entry> entries;
        for (const auto entry : literal.getWrites().getAsRange<EntryAttr>()) {
          entries.push_back(fromAttribute(entry));
        }
        writeRecordedLayer(recording, entries, destination / file);
        workspace.sources.insert(file);
        labels[&op] = label(file);
        continue;
      }
      if (auto exec = llvm::dyn_cast<ExecOp>(op)) {
        Action action{name, name + extension, "replay",
                      addPlan(execPlan(exec, recording, layerOf), name),
                      layerLabels(exec.getInputs())};
        action.layers.push_back(platformLabel);
        workspace.actions.push_back(std::move(action));
        labels[&op] = label(name);
        continue;
      }
      if (auto output = llvm::dyn_cast<OutputOp>(op)) {
        workspace.actions.push_back(
            {name, absl::StrCat(output.getName().str(), kNarExtension), "pack",
             addPlan(outputPlan(output, layerOf), name),
             layerLabels(output.getEntries())});
        continue;
      }
      throw std::runtime_error("unsupported operation: " +
                               op.getName().getStringRef().str());
    }

    writeBytes(destination / "BUILD.bazel",
               buildFile(Backend::Bazel, workspace));
    writeBytes(destination / "BUCK", buildFile(Backend::Buck2, workspace));
    writeBytes(destination / "WORKSPACE", "");
    writeBytes(destination / "MODULE.bazel", "");
    writeBytes(destination / ".buckconfig", kBuckConfig);
    fs::create_directory(destination / "toolchains");
    writeBytes(destination / "toolchains/BUCK", kBuckToolchains);
  } catch (...) {
    removeTree(destination);
    throw;
  }
}

void writeReference(const Recording &recording, const std::string &output,
                    const fs::path &archive) {
  const auto path = recording.outputs.find(output);
  check(path != recording.outputs.end(), "the derivation has no output " + output);

  // Pack the entries below the output path as the builder left them.
  constexpr std::string_view kFinalLayer = "final";
  Scratch scratch;
  const auto layer =
      scratch.path / (std::string(kFinalLayer) + std::string(kLayerExtension));
  Plan plan;
  plan.name = output;
  plan.output = path->second;
  std::vector<Entry> entries;
  for (const auto &[entryPath, entry] : recording.state(recording.finalStep)) {
    if (isBelow(entryPath, path->second)) {
      entries.push_back(entry);
      plan.inputs.push_back({std::string(kFinalLayer), entryPath});
    }
  }
  writeRecordedLayer(recording, entries, layer);
  packPlan(plan, {layer}, archive);
}
} // namespace splice_ir

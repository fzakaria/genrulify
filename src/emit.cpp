#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "compiler.h"
#include "dialect.h"
#include "nlohmann/json.hpp"
#include "process.h"
#include "tools.h"
#include <fstream>
#include <regex>
#include <set>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
using Json = nlohmann::json;
enum class Backend { Bazel, Buck2 };

constexpr std::string_view buckConfig = R"([cells]
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

void writeFile(const fs::path &path, std::string_view contents) {
  std::ofstream file;
  file.exceptions(std::ios::failbit | std::ios::badbit);
  file.open(path);
  file << contents;
  file.close();
}

struct Action {
  std::string name;
  std::vector<std::string> inputs;
  std::string manifest;
};

struct Graph {
  std::map<std::string, fs::path> sources;
  std::vector<Action> actions;
  std::string output;
  bool archive = false;
};

Graph readGraph(mlir::ModuleOp module, const fs::path &assets) {
  Graph graph;
  llvm::DenseMap<mlir::Value, std::string> labels;
  const std::regex storeFile(
      "/nix/store/[0-9a-z]{32}-[A-Za-z0-9][A-Za-z0-9._+-]*");
  for (auto &op : module.getBody()->getOperations()) {
    if (!graph.output.empty()) {
      throw std::runtime_error("publish must end the module");
    }
    if (auto seed = llvm::dyn_cast<SeedOp>(op)) {
      check(seed.getAsset() == "source.nar", "unknown source archive");
      graph.sources.emplace("inputs/source.nar", assets / "source.nar");
      labels[seed.getTree()] = ":inputs/source.nar";
      continue;
    }
    if (auto exec = llvm::dyn_cast<ExecOp>(op)) {
      const auto name = exec.getAction().str();
      static const std::set<std::string> supported{"compile", "install",
                                                   "patchelf", "strip"};
      check(supported.contains(name), "unknown package action");
      const auto filename = name + ".json";
      const auto label = "inputs/" + filename;
      check(graph.sources.emplace(label, assets / filename).second,
            "duplicate package action");
      const auto manifest = Json::parse(readBytes(assets / filename));
      check(manifest.at("action") == name &&
                manifest.at("step") == exec.getStep(),
            "action manifest differs from IR");
      graph.actions.push_back(
          {name, {labels.lookup(exec.getInput()), ":" + label}, ":" + label});
      labels[exec.getTree()] = ":" + name;
      continue;
    }
    if (auto package = llvm::dyn_cast<PackageOp>(op)) {
      graph.output = labels.lookup(package.getTree());
      graph.archive = true;
      continue;
    }
    if (auto source = llvm::dyn_cast<SourceOp>(op)) {
      fs::path path(source.getPath().str());
      if (!std::regex_match(path.native(), storeFile) ||
          !fs::is_regular_file(fs::symlink_status(path))) {
        throw std::runtime_error("expected a regular store file: " +
                                 path.string());
      }
      auto label = "inputs/" + path.filename().string();
      labels[source.getFile()] = ":" + label;
      graph.sources.emplace(label, path);
      continue;
    }
    if (auto publish = llvm::dyn_cast<PublishOp>(op)) {
      graph.output = labels.lookup(publish.getFile());
      continue;
    }
    if (!llvm::isa<CopyOp, AppendOp>(op)) {
      throw std::runtime_error("unsupported operation: " +
                               op.getName().getStringRef().str());
    }
    Action action{absl::StrCat("a", graph.actions.size() + 1), {}};
    for (auto input : op.getOperands()) {
      action.inputs.push_back(labels.lookup(input));
    }
    labels[op.getResult(0)] = ":" + action.name;
    graph.actions.push_back(std::move(action));
  }
  if (graph.output.empty()) {
    throw std::runtime_error("missing splice.publish");
  }
  return graph;
}

std::string rule(Backend backend, const Action &action,
                 std::string_view filename, std::string_view command) {
  const auto quote = [](std::string_view value) { return Json(value).dump(); };
  const std::set<std::string> deps(action.inputs.begin(), action.inputs.end());
  const auto output = backend == Backend::Bazel
                          ? absl::StrCat("outs = [", quote(filename), "]")
                          : absl::StrCat("out = ", quote(filename));
  const auto policy =
      backend == Backend::Bazel ? "tags = [\"no-remote\"]" : "remote = False";
  return absl::StrCat("genrule(\n    name = ", quote(action.name),
                      ",\n    srcs = ", Json(deps).dump(), ",\n    ", output,
                      ",\n    cmd = ", quote(command), ",\n    ", policy,
                      ",\n)\n\n");
}

std::string buildFile(Backend backend, const Graph &graph) {
  std::string text;
  if (backend == Backend::Buck2) {
    for (const auto &[label, path] : graph.sources) {
      absl::StrAppend(&text, "export_file(name = \"", label, "\", src = \"",
                      label, "\")\n");
    }
    text += "\n";
  }
  const auto output = backend == Backend::Bazel ? "$@" : "$OUT";
  for (const auto &action : graph.actions) {
    if (!action.manifest.empty()) {
      const auto runner = fs::canonical("/proc/self/exe").string();
      const auto cmd = absl::StrCat(
          "'", runner, "' replay-action \"$(location ", action.manifest,
          ")\" \"$(location ", action.inputs.front(), ")\" \"", output, "\"");
      const auto filename =
          graph.output == ":" + action.name ? "out.nar" : action.name + ".nar";
      text += rule(backend, action, filename, cmd);
      continue;
    }
    std::vector<std::string> locations;
    for (const auto &input : action.inputs) {
      locations.push_back(absl::StrCat("\"$(location ", input, ")\""));
    }
    const auto cmd = absl::StrCat(kCat, " ", absl::StrJoin(locations, " "),
                                  " > \"", output, "\"");
    text += rule(backend, action, action.name + ".bin", cmd);
  }

  if (graph.archive) {
    return text + absl::StrCat("alias(name = \"archive\", actual = \"",
                               graph.output, "\")\n");
  }

  // Backend artifacts have backend-defined modes. Pack the regular Nix file
  // from a private copy whose mode is part of the !splice.file contract.
  const auto tmp = absl::StrCat("\"", output, ".regular\"");
  const auto pack =
      absl::StrCat(kCat, " \"$(location ", graph.output, ")\" > ", tmp, " && ",
                   kChmod, " 644 ", tmp, " && ", kNixStore, " --dump ", tmp,
                   " > \"", output, "\" && ", kRm, " ", tmp);
  return text + rule(backend, {"archive", {graph.output}}, "out.nar", pack);
}
} // namespace

void emitWorkspace(mlir::ModuleOp module, const fs::path &assets,
                   const fs::path &destination) {
  auto graph = readGraph(module, assets);
  if (!fs::create_directory(destination)) {
    throw std::runtime_error("destination already exists");
  }
  try {
    fs::create_directory(destination / "inputs");
    for (const auto &[label, path] : graph.sources) {
      fs::copy_file(path, destination / label);
    }
    writeFile(destination / "BUILD.bazel", buildFile(Backend::Bazel, graph));
    writeFile(destination / "BUCK", buildFile(Backend::Buck2, graph));
    writeFile(destination / "WORKSPACE", "");
    writeFile(destination / "MODULE.bazel", "");
    writeFile(destination / ".buckconfig", buckConfig);
    fs::create_directory(destination / "toolchains");
    writeFile(destination / "toolchains/BUCK",
              "load(\"@prelude//toolchains:demo.bzl\", "
              "\"system_demo_toolchains\")\nsystem_demo_toolchains()\n");
  } catch (...) {
    fs::remove_all(destination);
    throw;
  }
}
} // namespace splice_ir

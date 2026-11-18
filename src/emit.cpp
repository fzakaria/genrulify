#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "compiler.h"
#include "dialect.h"
#include "nlohmann/json.hpp"
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
};

struct Graph {
  std::map<std::string, fs::path> sources;
  std::vector<Action> actions;
  std::string output;
};

Graph readGraph(mlir::ModuleOp module) {
  Graph graph;
  llvm::DenseMap<mlir::Value, std::string> labels;
  const std::regex storeFile(
      "/nix/store/[0-9a-z]{32}-[A-Za-z0-9][A-Za-z0-9._+-]*");
  for (auto &op : module.getBody()->getOperations()) {
    if (!graph.output.empty()) {
      throw std::runtime_error("publish must end the module");
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
    std::vector<std::string> locations;
    for (const auto &input : action.inputs) {
      locations.push_back(absl::StrCat("\"$(location ", input, ")\""));
    }
    const auto cmd = absl::StrCat(kCat, " ", absl::StrJoin(locations, " "),
                                  " > \"", output, "\"");
    text += rule(backend, action, action.name + ".bin", cmd);
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

void emitWorkspace(mlir::ModuleOp module, const fs::path &destination) {
  auto graph = readGraph(module);
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

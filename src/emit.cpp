#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "action.h"
#include "compiler.h"
#include <regex>
#include <set>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
using Json = nlohmann::json;
enum class Backend { Bazel, Buck2 };

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

// The names Buck2 gives a rule's stamp and its archive among its outputs.
constexpr std::string_view kBuckStampName = "ok";
constexpr std::string_view kBuckArchiveName = "nar";

// The indent of a rule's attributes, and of the items of an attribute
// that spans several lines.
constexpr std::string_view kAttributeIndent = "    ";
constexpr std::string_view kItemIndent = "        ";

// A file a rule reads that another rule wrote.
struct Generated {
  std::string target;
  std::string artifact;
  auto operator<=>(const Generated &) const = default;
};

// One genrule: the files it reads, the files it writes, and the recorded
// command lines that turn one into the other.
struct Rule {
  std::string name;

  // Files that exist in the workspace: the sandbox script and its shared
  // inputs, the rule's manifest, and bytes taken from the recording.
  std::set<std::string> sources;

  // File versions written by other rules, and the rules whose stamp this
  // rule waits for because it uses a directory or a symlink they made.
  std::set<Generated> generated;
  std::set<std::string> stamps;

  // The files this rule writes, and whether its last output is a NAR.
  std::vector<std::string> outputs;
  bool archive = false;

  std::vector<Arguments> arguments;
};

std::string quote(std::string_view value) { return Json(value).dump(); }

// A Starlark list of strings, one item per line, as an attribute's value.
std::string list(const Arguments &items) {
  if (items.empty()) {
    return "[]";
  }
  std::string text = "[\n";
  for (const auto &item : items) {
    absl::StrAppend(&text, kItemIndent, quote(item), ",\n");
  }
  return absl::StrCat(text, kAttributeIndent, "]");
}

// Quotes an argument the way a shell needs it typed.
std::string shellQuote(const std::string &argument) {
  static const std::regex plain("[A-Za-z0-9_@%+=:,./-]+");
  if (std::regex_match(argument, plain)) {
    return argument;
  }
  return absl::StrCat("'", absl::StrReplaceAll(argument, {{"'", "'\\''"}}),
                      "'");
}

// One line of a rule's command: recorded arguments, quoted for the shell
// and then for the build system, which gives `$` a meaning of its own.
std::string commandLine(Backend backend, const Arguments &arguments) {
  Arguments words;
  for (const auto &argument : arguments) {
    words.push_back(shellQuote(argument));
  }
  const auto line = absl::StrJoin(words, " ");
  if (backend == Backend::Bazel) {
    return absl::StrReplaceAll(line, {{"$", "$$"}});
  }
  return absl::StrReplaceAll(line, {{"$(", "\\$("}});
}

std::string stampFile(std::string_view target) {
  return absl::StrCat(target, kStampExtension);
}

std::string renderRule(Backend backend, const Rule &rule) {
  const bool bazel = backend == Backend::Bazel;

  // Inputs. Both systems find a workspace file by its path. A file from
  // another rule is that rule's output in Bazel and a named output of the
  // rule in Buck2.
  Arguments sources;
  for (const auto &file : rule.sources) {
    sources.push_back(bazel ? absl::StrCat(":", file) : file);
  }
  for (const auto &input : rule.generated) {
    sources.push_back(
        bazel ? absl::StrCat(":", input.artifact)
              : absl::StrCat(":", input.target, "[",
                             buckOutputName(input.target, input.artifact),
                             "]"));
  }
  for (const auto &target : rule.stamps) {
    sources.push_back(bazel ? absl::StrCat(":", stampFile(target))
                            : absl::StrCat(":", target, "[", kBuckStampName,
                                           "]"));
  }

  // Outputs. A replay rule ends with its stamp and an archive rule with
  // its NAR.
  const auto last = rule.archive ? absl::StrCat(rule.name, kArchiveExtension)
                                 : stampFile(rule.name);
  std::vector<std::string> attributes{
      absl::StrCat("name = ", quote(rule.name)),
      absl::StrCat("srcs = ", list(sources))};
  if (bazel) {
    Arguments files = rule.outputs;
    files.push_back(last);
    attributes.push_back(absl::StrCat("outs = ", list(files)));
  } else {
    // Buck2 names every output, and builds the default one when a rule is
    // asked for without a name.
    std::string named = "{\n";
    for (const auto &artifact : rule.outputs) {
      absl::StrAppend(&named, kItemIndent,
                      quote(buckOutputName(rule.name, artifact)), ": [",
                      quote(artifact), "],\n");
    }
    absl::StrAppend(&named, kItemIndent,
                    quote(rule.archive ? kBuckArchiveName : kBuckStampName),
                    ": [", quote(last), "],\n", kAttributeIndent, "}");
    attributes.push_back(absl::StrCat("outs = ", named));
    attributes.push_back(absl::StrCat("default_outs = [", quote(last), "]"));
  }

  // The command: where the sandbox script finds its inputs and puts its
  // outputs, then the recorded command lines, one per line.
  Arguments lines{bazel ? "./sandbox.sh --in . --in $(RULEDIR) --out $(RULEDIR)"
                        : "$SRCDIR/sandbox.sh --in $SRCDIR --out $OUT"};
  for (const auto &arguments : rule.arguments) {
    lines.push_back(commandLine(backend, arguments));
  }
  attributes.push_back(absl::StrCat("cmd = \" \".join(", list(lines), ")"));

  // The steps read the host's Nix store, which a remote worker may not
  // have.
  attributes.push_back(bazel ? "tags = [\"no-remote\"]" : "remote = False");

  std::string text = "genrule(\n";
  for (const auto &attribute : attributes) {
    absl::StrAppend(&text, kAttributeIndent, attribute, ",\n");
  }
  return text + ")\n\n";
}

std::string buildFile(Backend backend, const std::vector<Rule> &rules) {
  std::string text;
  for (const auto &rule : rules) {
    text += renderRule(backend, rule);
  }
  return text;
}

// Copies a recorded file's bytes into the workspace under an artifact name.
void writeArtifact(const Recording &recording, const Entry &entry,
                   const fs::path &file) {
  fs::create_directories(file.parent_path());
  fs::copy_file(recording.blob(entry.content), file,
                fs::copy_options::overwrite_existing);
}
} // namespace

void emitWorkspace(mlir::ModuleOp module, const Recording &recording,
                   const fs::path &destination) {
  check(fs::create_directory(destination), "destination already exists");
  try {
    writeSharedFiles(recording, destination);

    // The guest's /bin and /etc, as files every replay rule reads.
    const ArtifactOf platformArtifact = [](const Entry &entry) {
      return artifactName(kPlatformTarget, entry.path);
    };
    std::set<std::string> platformFiles;
    for (const auto &[path, entry] : recording.platform()) {
      if (entry.kind == EntryKind::File) {
        const auto artifact = platformArtifact(entry);
        writeArtifact(recording, entry, destination / artifact);
        platformFiles.insert(artifact);
      }
    }

    size_t outputCount = 0;
    for (auto &op : module.getBody()->getOperations()) {
      outputCount += llvm::isa<OutputOp>(op) ? 1 : 0;
    }
    const auto artifactOf = [&](mlir::Value value) {
      return artifactName(targetName(value.getDefiningOp(), outputCount),
                          entryOf(value)->path);
    };

    // Sorts a rule's operands into workspace files and other rules' files.
    const auto addOperands = [&](Rule &rule, mlir::OperandRange operands) {
      for (const auto value : operands) {
        auto *producer = value.getDefiningOp();
        const auto target = targetName(producer, outputCount);
        const bool file = entryOf(value)->kind == EntryKind::File;
        if (llvm::isa<LiteralOp>(producer)) {
          if (file) {
            rule.sources.insert(artifactOf(value));
          }
          continue;
        }
        rule.stamps.insert(target);
        if (file) {
          rule.generated.insert({target, artifactOf(value)});
        }
      }
    };

    std::vector<Rule> rules;
    for (auto &op : module.getBody()->getOperations()) {
      if (auto literal = llvm::dyn_cast<LiteralOp>(op)) {
        // Recorded bytes are files checked into the workspace.
        const auto target = targetName(&op, outputCount);
        for (const auto attribute :
             literal.getWrites().getAsRange<EntryAttr>()) {
          const auto entry = fromAttribute(attribute);
          if (entry.kind == EntryKind::File) {
            writeArtifact(recording, entry,
                          destination / artifactName(target, entry.path));
          }
        }
        continue;
      }

      Rule rule;
      Action action;
      if (auto exec = llvm::dyn_cast<ExecOp>(op)) {
        action = execAction(exec, recording, artifactOf, platformArtifact);
        rule.sources.insert(platformFiles.begin(), platformFiles.end());
        addOperands(rule, exec.getInputs());
        for (const auto &result : action.results) {
          if (!result.artifact.empty()) {
            rule.outputs.push_back(result.artifact);
          }
        }
      } else if (auto output = llvm::dyn_cast<OutputOp>(op)) {
        action = outputAction(output, outputCount, artifactOf);
        rule.archive = true;
        addOperands(rule, output.getEntries());
      } else {
        throw std::runtime_error("unsupported operation: " +
                                 op.getName().getStringRef().str());
      }
      rule.name = action.name;
      for (const auto &file : sharedFiles(action)) {
        rule.sources.insert(file);
      }
      for (const auto &file : writeActionFiles(action, destination)) {
        rule.sources.insert(file);
      }
      rule.arguments = actionArguments(action, recording);
      rules.push_back(std::move(rule));
    }

    writeBytes(destination / "BUILD.bazel", buildFile(Backend::Bazel, rules));
    writeBytes(destination / "BUCK", buildFile(Backend::Buck2, rules));
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
  check(path != recording.outputs.end(),
        "the derivation has no output " + output);

  // Pack the entries below the output path as the builder left them, with
  // each file's bytes read straight from the recording's blobs.
  Action action;
  action.name = output;
  action.output = path->second;
  for (const auto &[entryPath, entry] : recording.state(recording.finalStep)) {
    if (isBelow(entryPath, path->second)) {
      action.inputs.push_back(
          {entry, entry.kind == EntryKind::File
                      ? absl::StrCat(kBlobsDirectory, "/", entry.content)
                      : ""});
    }
  }

  Scratch scratch;
  writeSharedFiles(recording, scratch.path);
  writeActionFiles(action, scratch.path);
  Arguments command{(scratch.path / kSandboxFile).string(),
                    "--in",
                    scratch.path.string(),
                    "--in",
                    recording.directory.string(),
                    "--out",
                    (scratch.path / "out").string()};
  for (const auto &line : actionArguments(action, recording)) {
    command.insert(command.end(), line.begin(), line.end());
  }
  execute(command);
  fs::copy_file(scratch.path / "out" / absl::StrCat(output, kArchiveExtension),
                archive, fs::copy_options::overwrite_existing);
}
} // namespace splice_ir

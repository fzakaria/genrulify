#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "action.h"
#include "compiler.h"
#include "rules.h"
#include "rules_text.h"
#include <algorithm>
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

// How a rule calls the sandbox script. Bazel runs a command in the
// workspace's root with other rules' outputs under RULEDIR. Buck2 links
// every source under SRCDIR and expects the output at OUT, which is named
// after the rule, so the script is pointed at the directory OUT is in.
constexpr std::string_view kBazelInvocation =
    "./sandbox.sh --in . --in $(RULEDIR) --out $(RULEDIR)";
constexpr std::string_view kBuckInvocation =
    "$SRCDIR/sandbox.sh --in $SRCDIR --out ${OUT%/*}";

// The files of rule definitions a workspace ships, one for each build
// system.
constexpr std::string_view kBazelRulesFile = "genrulify.bzl";
constexpr std::string_view kBuckRulesFile = "genrulify.buck.bzl";

// The extensions by which a typed rule tells its inputs apart: what a
// link takes as objects and as libraries, and what rustc takes as sources
// and as the crates it depends on.
constexpr std::string_view kObjectExtension = ".o";
constexpr std::string_view kArchiveLibraryExtension = ".a";
constexpr std::string_view kSharedLibraryName = R"(.*\.so(\.[0-9]+)*)";
constexpr std::string_view kRustSourceExtension = ".rs";
constexpr std::string_view kRustCrateExtensions[] = {".rlib", ".rmeta"};

// The indent of a rule's attributes, and of the items of an attribute
// that spans several lines.
constexpr std::string_view kAttributeIndent = "    ";
constexpr std::string_view kItemIndent = "        ";

// A file a rule reads: the target the file belongs to, which is the rule
// that wrote the file or the name recorded bytes are stored under, the
// file in the build system's tree, and the path the command sees the file
// at.
struct Input {
  std::string target;
  std::string artifact;
  std::string path;
  auto operator<=>(const Input &) const = default;
};

// One rule: the files it reads, the files it writes, and the recorded
// command lines that turn one into the other.
struct Rule {
  std::string name;

  // The kind of rule the command was raised to, or empty, and the paths
  // the command names as its sources when it is a compile.
  std::string kind;
  std::set<std::string> sourcePaths;

  // Files the compiler wrote for the rule: the sandbox script and what it
  // shares between rules, and the rule's manifest.
  std::set<std::string> sources;

  // Bytes taken from the recording, which are files in the workspace.
  std::set<Input> recorded;

  // File versions written by other rules, and the rules whose stamp this
  // rule waits for because it uses a directory or a symlink they made.
  std::set<Input> generated;
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

// One attribute of a rule: `name = value`.
std::string attribute(std::string_view name, std::string_view value) {
  return absl::StrCat(name, " = ", value);
}

// Whether a guest path has the name of a shared library.
bool isSharedLibrary(const std::string &path) {
  static const std::regex name{std::string(kSharedLibraryName)};
  return std::regex_match(fs::path(path).filename().string(), name);
}

// The name of the rule a command is lowered to. A compile is a cc_object
// only when exactly one of its inputs is a source the command names:
// with none or several there is no `src` to write, and the command keeps
// the plain rule. The name is decided once so that both build files call
// a command the same thing.
std::string_view macroOf(const Rule &rule) {
  if (rule.archive) {
    return kArchiveRule;
  }
  if (rule.kind.empty()) {
    return kRecordedRule;
  }
  if (rule.kind != kCcObject) {
    return rule.kind;
  }
  size_t named = 0;
  for (const auto *inputs : {&rule.recorded, &rule.generated}) {
    for (const auto &input : *inputs) {
      named += rule.sourcePaths.contains(input.path) ? 1 : 0;
    }
  }
  return named == 1 ? kCcObject : kRecordedRule;
}

// The command of a rule as the lines of a list: where the sandbox script
// finds its inputs and puts its outputs, then the recorded command lines.
// The rule joins the lines with spaces.
std::string commandLines(Backend backend, const Rule &rule) {
  Arguments lines{std::string(backend == Backend::Bazel ? kBazelInvocation
                                                        : kBuckInvocation)};
  for (const auto &arguments : rule.arguments) {
    lines.push_back(commandLine(backend, arguments));
  }
  return list(lines);
}

// The attributes of a Bazel rule. Bazel is given each file: a workspace
// file by its path, and a file from another rule as that rule's output.
// A typed rule names the inputs its kind is about, and puts the rest in
// `data`: the manifest, the environment, the closure, the sandbox script,
// the guest's /bin and /etc, and the stamps of the rules whose
// directories the command uses.
std::vector<std::string> bazelAttributes(const Rule &rule,
                                         std::string_view macro) {
  // The files the command was given, as labels with the paths it sees
  // them at, and what the command needs besides.
  std::vector<std::pair<std::string, std::string>> given;
  Arguments data;
  for (const auto &file : rule.sources) {
    data.push_back(absl::StrCat(":", file));
  }
  for (const auto &input : rule.recorded) {
    const auto label = absl::StrCat(":", input.artifact);
    if (input.target == kPlatformTarget) {
      data.push_back(label);
    } else {
      given.emplace_back(label, input.path);
    }
  }
  std::ranges::sort(data);
  for (const auto &input : rule.generated) {
    given.emplace_back(absl::StrCat(":", input.artifact), input.path);
  }
  Arguments stamps;
  for (const auto &target : rule.stamps) {
    stamps.push_back(absl::StrCat(":", stampFile(target)));
  }

  // Takes out of `given` the inputs a predicate picks.
  const auto take = [&](const auto &picks) {
    Arguments taken;
    std::erase_if(given, [&](const auto &input) {
      if (!picks(input.second)) {
        return false;
      }
      taken.push_back(input.first);
      return true;
    });
    return taken;
  };
  const auto extensionIs = [](std::string_view extension) {
    return [extension](const std::string &path) {
      return fs::path(path).extension() == extension;
    };
  };
  const auto isLibrary = [](const std::string &path) {
    return fs::path(path).extension() == kArchiveLibraryExtension ||
           isSharedLibrary(path);
  };

  // Whatever is left of `given` once a kind has taken its inputs has no
  // attribute of its own and goes with the data, ahead of the stamps.
  const auto rest = [&] {
    Arguments all = data;
    for (const auto &input : given) {
      all.push_back(input.first);
    }
    all.insert(all.end(), stamps.begin(), stamps.end());
    return all;
  };

  // A rule that writes one file names the file, and one that writes
  // several lists them.
  const auto outputs = [&] {
    if (rule.outputs.size() == 1) {
      return attribute("out", quote(rule.outputs.front()));
    }
    return attribute("outs", list(rule.outputs));
  };

  std::vector<std::string> attributes{attribute("name", quote(rule.name))};
  if (macro == kArchiveRule) {
    attributes.push_back(attribute("srcs", list(rest())));
    attributes.push_back(attribute(
        "out", quote(absl::StrCat(rule.name, kArchiveExtension))));
  } else if (macro == kCcObject) {
    const auto source = take([&](const std::string &path) {
      return rule.sourcePaths.contains(path);
    });
    attributes.push_back(attribute("src", quote(source.front())));

    // Every other file a compile reads is one it included.
    attributes.push_back(attribute("hdrs", list(take([](const auto &) {
      return true;
    }))));
    attributes.push_back(outputs());
    attributes.push_back(attribute("data", list(rest())));
  } else if (macro == kCcLibrary) {
    attributes.push_back(
        attribute("objs", list(take(extensionIs(kObjectExtension)))));
    attributes.push_back(outputs());
    attributes.push_back(attribute("data", list(rest())));
  } else if (macro == kCcBinary || macro == kCcSharedLibrary) {
    attributes.push_back(
        attribute("objs", list(take(extensionIs(kObjectExtension)))));
    attributes.push_back(attribute("deps", list(take(isLibrary))));
    attributes.push_back(outputs());
    attributes.push_back(attribute("data", list(rest())));
  } else if (macro == kRustLibrary || macro == kRustBinary ||
             macro == kRustProcMacro) {
    attributes.push_back(
        attribute("srcs", list(take(extensionIs(kRustSourceExtension)))));
    attributes.push_back(
        attribute("deps", list(take([&](const std::string &path) {
          const auto extension = fs::path(path).extension().string();
          return std::ranges::find(kRustCrateExtensions, extension) !=
                     std::end(kRustCrateExtensions) ||
                 isLibrary(path);
        }))));
    attributes.push_back(attribute("outs", list(rule.outputs)));
    attributes.push_back(attribute("data", list(rest())));
  } else {
    attributes.push_back(attribute("srcs", list(rest())));
    attributes.push_back(attribute("outs", list(rule.outputs)));
  }
  attributes.push_back(attribute("cmd", commandLines(Backend::Bazel, rule)));
  return attributes;
}

// The attributes of a Buck2 rule. Buck2 hands a command all of its
// sources in one environment variable, which the kernel caps at 128 KiB,
// and a rule that reads a few thousand files does not fit. So a Buck2
// rule depends on whole directories: the one each rule writes, and the
// one each set of recorded bytes is stored in. The manifest names the
// files either way. With directories for inputs every kind of rule has
// the same attributes.
std::vector<std::string> buckAttributes(const Rule &rule) {
  std::set<std::string> listed(rule.sources.begin(), rule.sources.end());
  for (const auto &input : rule.recorded) {
    listed.insert(input.target);
  }
  Arguments sources(listed.begin(), listed.end());
  for (const auto &target : rule.stamps) {
    sources.push_back(absl::StrCat(":", target));
  }

  // A replay rule writes the directory that holds its files, and an
  // archive rule its NAR.
  const auto output = rule.archive ? absl::StrCat(rule.name, kArchiveExtension)
                                   : rule.name;
  return {attribute("name", quote(rule.name)),
          attribute("srcs", list(sources)), attribute("out", quote(output)),
          attribute("cmd", commandLines(Backend::Buck2, rule))};
}

std::string renderRule(Backend backend, const Rule &rule) {
  const auto macro = macroOf(rule);
  std::string text = absl::StrCat(macro, "(\n");
  for (const auto &line : backend == Backend::Bazel
                              ? bazelAttributes(rule, macro)
                              : buckAttributes(rule)) {
    absl::StrAppend(&text, kAttributeIndent, line, ",\n");
  }
  return text + ")\n\n";
}

// A build file: the rules it uses, loaded from the file of definitions
// the workspace ships, then one call per operation.
std::string buildFile(Backend backend, const std::vector<Rule> &rules) {
  std::set<std::string> macros;
  for (const auto &rule : rules) {
    macros.insert(std::string(macroOf(rule)));
  }
  Arguments loaded{quote(absl::StrCat(
      ":", backend == Backend::Bazel ? kBazelRulesFile : kBuckRulesFile))};
  for (const auto &macro : macros) {
    loaded.push_back(quote(macro));
  }
  std::string text = absl::StrCat("load(", absl::StrJoin(loaded, ", "), ")\n\n");
  for (const auto &rule : rules) {
    text += renderRule(backend, rule);
  }
  return text;
}

// The process groups a recording's report lists without a replay, or none
// when the recording has not been checked.
std::set<uint64_t> groupsNotReplayed(const Recording &recording) {
  std::set<uint64_t> steps;
  const auto report = recording.directory / kReportFile;
  if (!fs::exists(report)) {
    return steps;
  }
  static const std::set<std::string> replayed{
      std::string(rungName(Rung::Narrow)), std::string(rungName(Rung::Near)),
      std::string(rungName(Rung::Traced)), std::string(rungName(Rung::Wide))};
  for (const auto &group : Json::parse(readBytes(report)).at("groups")) {
    if (!replayed.contains(group.at("outcome").get<std::string>())) {
      steps.insert(group.at("step").get<uint64_t>());
    }
  }
  return steps;
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
    std::set<Input> platformFiles;
    for (const auto &[path, entry] : recording.platform()) {
      if (entry.kind == EntryKind::File) {
        const auto artifact = platformArtifact(entry);
        writeArtifact(recording, entry, destination / artifact);
        platformFiles.insert({std::string(kPlatformTarget), artifact, path});
      }
    }

    size_t outputCount = 0;
    for (auto &op : module.getBody()->getOperations()) {
      outputCount += llvm::isa<OutputOp>(op) ? 1 : 0;
    }

    // A rule is only as good as the replay that checked its group. When
    // the recording has a report, a group the report says was not replayed
    // must not become a rule: the report was made with other passes, which
    // removed the group before the checker saw it.
    const auto unchecked = groupsNotReplayed(recording);

    // A file is read under the name of the version that first had its
    // bytes. A copy has no file of its own: whoever reads the copy lists
    // the original and stages it at the copy's path.
    const auto artifactOf = [&](mlir::Value value) {
      const auto original = contentSource(value);
      return artifactName(targetName(original.getDefiningOp(), outputCount),
                          entryOf(original)->path);
    };

    // Sorts a rule's operands into workspace files and other rules' files.
    const auto addOperands = [&](Rule &rule, mlir::OperandRange operands) {
      for (const auto value : operands) {
        auto *producer = contentSource(value).getDefiningOp();
        const auto target = targetName(producer, outputCount);
        const bool file = entryOf(value)->kind == EntryKind::File;
        const auto path = entryOf(value)->path;
        if (llvm::isa<LiteralOp>(producer)) {
          if (file) {
            rule.recorded.insert({target, artifactOf(value), path});
          }
          continue;
        }
        rule.stamps.insert(target);
        if (file) {
          rule.generated.insert({target, artifactOf(value), path});
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


      // A copy needs no rule.
      if (llvm::isa<CopyOp>(op)) {
        continue;
      }

      Rule rule;
      Action action;
      if (auto exec = llvm::dyn_cast<ExecOp>(op)) {
        check(!unchecked.contains(exec.getStep()),
              absl::StrCat("the group at step ", exec.getStep(),
                           " was not replayed by `check`: check the recording "
                           "with the passes the graph was optimized with"));
        action = execAction(exec, recording, artifactOf, platformArtifact);
        rule.recorded.insert(platformFiles.begin(), platformFiles.end());

        // A group raised to a kind of rule is one command, and a compile
        // says which of its inputs is the source.
        if (const auto kind = exec.getRule()) {
          rule.kind = kind->str();
          for (const auto &process : action.processes) {
            const auto paths = sourcePaths(process);
            rule.sourcePaths.insert(paths.begin(), paths.end());
          }
        }
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
    writeBytes(destination / kBazelRulesFile, kBazelRules);
    writeBytes(destination / kBuckRulesFile, kBuckRules);
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
  execute(sandboxCommand(action, recording, scratch.path, scratch.path));
  fs::copy_file(outputDirectory(scratch.path) /
                    absl::StrCat(output, kArchiveExtension),
                archive, fs::copy_options::overwrite_existing);
}
} // namespace splice_ir

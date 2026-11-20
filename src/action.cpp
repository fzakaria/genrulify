#include "action.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "sandbox_script.h"
#include "tools.h"
#include <fcntl.h>
#include <format>
#include <regex>
#include <set>
#include <sys/stat.h>
#include <sys/wait.h>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
constexpr std::string_view kPipePrefix = "pipe:";
constexpr std::string_view kDevicePrefix = "/dev/";

// A label names at most this many processes before it counts the rest.
constexpr size_t kLabelNames = 4;

// The name of a splice.literal for the single output of a derivation.
constexpr std::string_view kArchiveName = "archive";

// A replay runs natively and the recording ran in a VM, so a group that
// needs many times the whole build's recorded wall time is stuck. The
// floor covers recordings too short to scale from.
constexpr uint64_t kTimeoutFactor = 10;
constexpr uint64_t kMinimumTimeoutSeconds = 300;
constexpr uint64_t kMillisecondsPerSecond = 1000;

// A shell exits with this value plus the signal number when a child is
// killed by a signal, and so does bubblewrap.
constexpr int kSignalExitBase = 128;

// The separator of a manifest line's fields, which no path may contain.
constexpr char kFieldSeparator = '\x1f';

constexpr int kStandardInput = 0;

// The tags of a manifest line: an entry to stage, a result to expect, and
// a path the recorded processes wrote that nothing reads.
constexpr std::string_view kStageTag = "in";
constexpr std::string_view kExpectTag = "out";
constexpr std::string_view kUnusedTag = "unused";
constexpr std::string_view kRemovedTag = "gone";

// What sandbox.sh can put at a descriptor, as its --fd option spells them.
constexpr std::string_view kNullSpec = "null";
constexpr std::string_view kStandardErrorSpec = "err";
constexpr std::string_view kPipeReadSpec = "pipe-r:";
constexpr std::string_view kPipeWriteSpec = "pipe-w:";
constexpr std::string_view kDataSpec = "data:";
constexpr std::string_view kStoreSpec = "store:";
constexpr std::string_view kFileReadSpec = "file:r:";
constexpr std::string_view kFileReadAtSpec = "file:r@";
constexpr std::string_view kFileWriteSpec = "file:w:";
constexpr std::string_view kFileAppendSpec = "file:a:";

// The options and commands of sandbox.sh.
constexpr std::string_view kNameOption = "--name";
constexpr std::string_view kInputOption = "--in";
constexpr std::string_view kOutputOption = "--out";
constexpr std::string_view kTimeoutOption = "--timeout";
constexpr std::string_view kTrustOption = "--trust";
constexpr std::string_view kPipeOption = "--pipe";
constexpr std::string_view kRunCommand = "run";
constexpr std::string_view kPackCommand = "pack";
constexpr std::string_view kDirectoryOption = "--cwd";
constexpr std::string_view kEnvironmentOption = "--env";
constexpr std::string_view kUmaskOption = "--umask";
constexpr std::string_view kNameZeroOption = "--argv0";
constexpr std::string_view kDescriptorOption = "--fd";
constexpr std::string_view kAfterOption = "--after";
constexpr std::string_view kStatusOption = "--status";
constexpr std::string_view kCountOption = "--argc";
constexpr std::string_view kEndOfOptions = "--";

// Where a step run outside a build system leaves its outputs, below its
// scratch directory.
constexpr std::string_view kOutputDirectory = "out";

// The environment variables that make sandbox.sh trace a step: where the
// trace goes and which tracer writes it.
constexpr std::string_view kTraceVariable = "GENRULIFY_TRACE=";
constexpr std::string_view kTracerVariable = "GENRULIFY_STRACE=";

// The file a step's standard error is kept in, below its scratch directory.
constexpr std::string_view kReportFile = "report";

// A list of strings from views, for building argument lists.
Arguments words(std::initializer_list<std::string_view> items) {
  return Arguments(items.begin(), items.end());
}

// Appends to an argument list.
void append(Arguments &arguments, std::initializer_list<std::string_view> items) {
  arguments.insert(arguments.end(), items.begin(), items.end());
}

// The file names Bazel and Buck2 read as the definition of a package, a
// repository or a cell.
constexpr std::string_view kBuildFileNames[] = {
    "BUILD",          "BUILD.bazel", "WORKSPACE", "WORKSPACE.bazel",
    "MODULE.bazel",   "REPO.bazel",  "BUCK",      "BUCK.v2",
    "PACKAGE",        ".buckconfig",
};

// The characters an artifact name keeps. Everything else becomes `=XX`,
// which is safe in a Bazel label, a Buck2 path and a file name.
bool keptInArtifact(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' ||
         c == '.' || c == '+' || c == '/' || c == '-';
}

std::string escaped(char c) {
  return std::format("={:02X}", static_cast<unsigned char>(c));
}

// Replaces every character that is not safe in a target name.
std::string sanitize(std::string_view text) {
  std::string safe;
  for (const char c : text) {
    const bool keep = std::isalnum(static_cast<unsigned char>(c)) != 0 ||
                      c == '_' || c == '.' || c == '-';
    safe.push_back(keep ? c : '_');
  }
  return safe;
}

// The number in a pipe's /proc link target, `pipe:[501]`.
std::string pipeId(const Descriptor &descriptor) {
  static const std::regex digits("[0-9]+");
  std::smatch match;
  check(std::regex_search(descriptor.target, match, digits),
        "unexpected pipe target " + descriptor.target);
  return match.str();
}

// The exit status a sandbox reports for a recorded wait status.
int expectedExit(int status) {
  if (WIFSIGNALED(status)) {
    return kSignalExitBase + WTERMSIG(status);
  }
  return WEXITSTATUS(status);
}

// Which processes hold each end of a pipe that has both ends in a step.
struct PipeEnds {
  std::set<size_t> readers;
  std::set<size_t> writers;

  // Whether the recording saw a writer die of SIGPIPE, which means the
  // read side went away while the writer still had output.
  bool broke = false;
};

std::map<std::string, PipeEnds> sharedPipes(const Action &action) {
  std::map<std::string, PipeEnds> pipes;
  for (size_t index = 0; index < action.processes.size(); ++index) {
    const auto &process = action.processes[index];
    for (const auto &descriptor : process.fds) {
      if (!isPipe(descriptor)) {
        continue;
      }
      auto &pipe = pipes[pipeId(descriptor)];
      (readable(descriptor) ? pipe.readers : pipe.writers).insert(index);
      pipe.broke |= writable(descriptor) && WIFSIGNALED(process.status) &&
                    WTERMSIG(process.status) == SIGPIPE;
    }
  }

  // A pipe with one end in the step had the builder on the other end.
  std::erase_if(pipes, [](const auto &named) {
    return named.second.readers.empty() || named.second.writers.empty();
  });
  return pipes;
}

std::string environmentFile(size_t index) {
  return absl::StrCat(kEnvironmentsDirectory, "/", index, ".env");
}

// The file holding the bytes the builder had written into a pipe. The
// file is named after the pipe, so that every descriptor on the pipe,
// in whichever process, reads from the one file the way they shared the
// one pipe.
std::string dataFile(const Action &action, const Descriptor &descriptor) {
  return absl::StrCat(kActionsDirectory, "/", action.name, ".pipe",
                      pipeId(descriptor), ".data");
}

// What the sandbox script should put at one descriptor of one process.
std::string descriptorSpec(const Action &action, const Recording &recording,
                           const std::map<std::string, PipeEnds> &pipes,
                           const Descriptor &descriptor) {
  if (isPipe(descriptor)) {
    const auto id = pipeId(descriptor);
    if (pipes.contains(id)) {
      return absl::StrCat(readable(descriptor) ? kPipeReadSpec : kPipeWriteSpec,
                          id);
    }

    // The other end was the builder's. What the builder wrote into the
    // pipe before starting the process is fed back in. What the builder
    // read out of it, as for a command substitution, goes nowhere, because
    // nothing replays the builder.
    if (readable(descriptor) && descriptor.input) {
      return absl::StrCat(kDataSpec, dataFile(action, descriptor));
    }
    return std::string(kNullSpec);
  }

  // The guest's console devices do not exist outside the VM, and no build
  // output depends on what was written to them.
  if (descriptor.target.starts_with(kDevicePrefix)) {
    return std::string(descriptor.fd == kStandardInput ? kNullSpec
                                                       : kStandardErrorSpec);
  }

  // A store path the build could only read is not staged. The sandbox
  // mounts the host's copy at the same path. A project's source tree is
  // the exception: it is staged, even where it lies inside a store path
  // that is mounted.
  const bool staged = !recording.source.empty() &&
                      isBelow(descriptor.target, recording.source);
  const bool inClosure = !staged &&
                         std::ranges::any_of(
                             recording.closure, [&](const std::string &path) {
                               return isBelow(descriptor.target, path);
                             });
  if (inClosure) {
    return absl::StrCat(kStoreSpec, descriptor.target);
  }

  // A file the shell opened for the process. An offset or the append flag
  // means the redirection added to the file, and otherwise it replaced it.
  const int access = descriptor.flags & O_ACCMODE;
  if (access == O_RDONLY) {
    // A shell loop that reads a file line by line hands its commands the
    // file already read up to some offset.
    if (descriptor.position != 0) {
      return absl::StrCat(kFileReadAtSpec, descriptor.position, ":",
                          descriptor.target);
    }
    return absl::StrCat(kFileReadSpec, descriptor.target);
  }
  const bool adds = (descriptor.flags & O_APPEND) != 0 || descriptor.position != 0;
  return absl::StrCat(adds ? kFileAppendSpec : kFileWriteSpec,
                      descriptor.target);
}

std::string manifestLine(std::string_view tag, const ActionEntry &item) {
  for (const std::string_view field :
       {std::string_view(item.entry.path), std::string_view(item.entry.content),
        std::string_view(item.artifact)}) {
    check(field.find(kFieldSeparator) == std::string_view::npos &&
              field.find('\n') == std::string_view::npos,
          "a path with a control character cannot be staged: " +
              item.entry.path);
  }
  const std::string separator(1, kFieldSeparator);

  // The origin is the last field, and is left off a line that has none.
  return absl::StrCat(tag, separator, kindName(item.entry.kind), separator,
                      std::format("{:o}", item.entry.mode), separator,
                      item.entry.mtime, separator, item.entry.path, separator,
                      item.entry.content, separator, item.artifact,
                      item.origin.empty() ? "" : separator, item.origin, "\n");
}

// Reads a failed step's output back into what failed.
ReplayFailure parseFailure(const std::string &output) {
  const auto headline = output.find(kNotReproduced);
  if (headline == std::string::npos) {
    // The step did not get as far as comparing results.
    constexpr size_t kTailBytes = 600;
    ReplayFailure failure("the step could not run: " +
                          output.substr(output.size() > kTailBytes
                                            ? output.size() - kTailBytes
                                            : 0));
    failure.wrongStatus = true;
    return failure;
  }
  const auto lineStart = output.rfind('\n', headline);
  const auto report =
      output.substr(lineStart == std::string::npos ? 0 : lineStart + 1);
  ReplayFailure failure(report);
  for (const std::string_view line : absl::StrSplit(report, '\n')) {
    const auto problem = absl::StripLeadingAsciiWhitespace(line);
    if (problem.starts_with(kDiffersPrefix)) {
      const auto end = problem.find(kDiffersSeparator);
      failure.differing.emplace_back(problem.substr(
          kDiffersPrefix.size(), end - kDiffersPrefix.size()));
    } else if (problem.starts_with(kMissingPrefix)) {
      failure.differing.emplace_back(problem.substr(kMissingPrefix.size()));
    } else if (problem.find(kExitedWith) != std::string_view::npos) {
      failure.wrongStatus = true;
    }
  }
  return failure;
}
} // namespace

bool readable(const Descriptor &descriptor) {
  return (descriptor.mode & S_IRUSR) != 0;
}

bool writable(const Descriptor &descriptor) {
  return (descriptor.mode & S_IWUSR) != 0;
}

bool isPipe(const Descriptor &descriptor) {
  return descriptor.target.starts_with(kPipePrefix);
}

std::optional<std::pair<const Process *, int>> readerOfDriverPipe(
    const Group &group) {
  // Find the pipes some process of the group writes.
  std::set<std::string> written;
  for (const auto &process : group.processes) {
    for (const auto &descriptor : process.fds) {
      if (isPipe(descriptor) && writable(descriptor)) {
        written.insert(descriptor.target);
      }
    }
  }
  for (const auto &process : group.processes) {
    for (const auto &descriptor : process.fds) {
      if (isPipe(descriptor) && readable(descriptor) &&
          !written.contains(descriptor.target) && !descriptor.input) {
        return std::pair{&process, descriptor.fd};
      }
    }
  }
  return std::nullopt;
}

std::optional<std::string> unreplayable(const Group &group) {
  // The bytes of a pipe that only the builder wrote, as a here-document
  // is, were never in a file. The group can be replayed only if the
  // recording caught them waiting in the pipe.
  if (const auto reader = readerOfDriverPipe(group)) {
    return absl::StrCat(reader->first->name, " reads descriptor ",
                        reader->second, " from a pipe the builder wrote");
  }

  // A target that is not a path, such as a socket or an eventfd, holds
  // state the recording did not capture.
  for (const auto &process : group.processes) {
    for (const auto &descriptor : process.fds) {
      if (!isPipe(descriptor) && !descriptor.target.starts_with("/")) {
        return absl::StrCat(process.name, " holds ", descriptor.target);
      }
    }
  }
  return std::nullopt;
}

std::string label(const Group &group) {
  Arguments names;
  for (const auto &process : group.processes) {
    if (names.size() == kLabelNames) {
      names.push_back(absl::StrCat("+", group.processes.size() - kLabelNames));
      break;
    }
    names.push_back(process.name);
  }
  return absl::StrJoin(names, "|");
}

std::string targetName(mlir::Operation *op, size_t outputCount) {
  if (auto exec = llvm::dyn_cast<ExecOp>(op)) {
    return absl::StrCat("s", exec.getStep(), "_", sanitize(exec.getName()));
  }
  if (auto literal = llvm::dyn_cast<LiteralOp>(op)) {
    // A demoted group and the builder's writes just before the group share
    // a step, so the two kinds of literal get different prefixes.
    if (literal.getDemoted()) {
      return absl::StrCat("lit_", literal.getStep(), "_",
                          sanitize(literal.getName()));
    }

    // The constants folded out of a group keep the group's step as well.
    if (literal.getFolded()) {
      return absl::StrCat("const_", literal.getStep(), "_",
                          sanitize(literal.getName()));
    }
    return absl::StrCat(sanitize(literal.getName()), "_", literal.getStep());
  }

  // Every copy raised from one group has the group's name. No rule is
  // generated for a copy, so the name is only ever shown to people.
  if (auto copy = llvm::dyn_cast<CopyOp>(op)) {
    return absl::StrCat("copy_", copy.getStep(), "_",
                        sanitize(copy.getName()));
  }
  auto output = llvm::cast<OutputOp>(op);

  // A derivation with one output gets the short name.
  if (outputCount == 1) {
    return std::string(kArchiveName);
  }
  return absl::StrCat(kArchiveName, "_", sanitize(output.getName()));
}

std::string artifactName(std::string_view target, std::string_view guestPath) {
  std::string name(target);

  // A file named like a build file would turn its directory into a package
  // or a repository of its own, and no label could reach into it. Such a
  // file gets its first letter escaped.
  const auto slash = guestPath.rfind('/');
  const auto base = slash == std::string_view::npos ? 0 : slash + 1;
  const bool reserved = std::ranges::find(kBuildFileNames,
                                          guestPath.substr(base)) !=
                        std::end(kBuildFileNames);
  for (size_t index = 0; index < guestPath.size(); ++index) {
    const char c = guestPath[index];
    const bool keep = keptInArtifact(c) && !(reserved && index == base);
    name += keep ? std::string(1, c) : escaped(c);
  }
  return name;
}

Action execAction(ExecOp op, const Recording &recording,
                  const OperandName &operandArtifact,
                  const ArtifactOf &platformArtifact,
                  const OperandName &operandOrigin) {
  Action action;
  action.name = targetName(op, 0);
  action.processes = recording.group(op.getStep()).processes;
  action.timeoutSeconds =
      std::max(kMinimumTimeoutSeconds,
               kTimeoutFactor * recording.wallMilliseconds /
                   kMillisecondsPerSecond);

  // The guest's /bin and /etc are part of what a process can observe.
  for (const auto &[path, entry] : recording.platform()) {
    action.inputs.push_back(
        {entry, entry.kind == EntryKind::File ? platformArtifact(entry) : ""});
  }
  for (const auto input : op.getInputs()) {
    const auto entry = *entryOf(input);
    action.inputs.push_back(
        {entry, entry.kind == EntryKind::File ? operandArtifact(input) : "",
         operandOrigin ? operandOrigin(input) : ""});
  }
  for (const auto result : op.getResults()) {
    const auto entry = *entryOf(result);
    if (result.use_empty()) {
      action.unused.push_back(entry);
      continue;
    }
    action.results.push_back({entry, entry.kind == EntryKind::File
                                         ? artifactName(action.name, entry.path)
                                         : ""});
  }
  return action;
}

Action outputAction(OutputOp op, size_t outputCount,
                    const OperandName &operandArtifact,
                    const OperandName &operandOrigin) {
  Action action;
  action.name = targetName(op, outputCount);
  action.output = op.getPath().str();
  for (const auto value : op.getEntries()) {
    const auto entry = *entryOf(value);
    action.inputs.push_back(
        {entry, entry.kind == EntryKind::File ? operandArtifact(value) : "",
         operandOrigin ? operandOrigin(value) : ""});
  }
  return action;
}

std::vector<Arguments> actionArguments(const Action &action,
                                       const Recording &recording) {
  std::vector<Arguments> lines;
  if (!action.output.empty()) {
    auto pack = words({kNameOption, action.name, kPackCommand, action.output});
    if (!action.archiveDigest.empty()) {
      pack.push_back(action.archiveDigest);
    }
    lines.push_back(std::move(pack));
    return lines;
  }

  // The step's own options: its name, its deadline and the pipes between
  // its processes.
  auto options = words({kNameOption, action.name, kTimeoutOption,
                        std::to_string(action.timeoutSeconds)});
  if (action.trusted) {
    options.emplace_back(kTrustOption);
  }
  const auto pipes = sharedPipes(action);
  for (const auto &[id, pipe] : pipes) {
    append(options, {kPipeOption,
                     absl::StrCat(id, ":", *pipe.writers.rbegin(), ":",
                                  absl::StrJoin(pipe.readers, ","), ":",
                                  pipe.broke ? 1 : 0)});
  }
  lines.push_back(std::move(options));

  for (size_t index = 0; index < action.processes.size(); ++index) {
    const auto &process = action.processes[index];
    check(!process.argv.empty(), "missing executable arguments");
    auto wiring = words({kRunCommand, kDirectoryOption, process.cwd,
                         kEnvironmentOption,
                         std::to_string(process.environment)});

    // The script gives a process the mask a build starts with unless it
    // is told another, in octal as the shell's `umask` takes it.
    if (process.umask != kBuilderUmask) {
      append(wiring, {kUmaskOption, std::format("{:o}", process.umask)});
    }

    // The kernel loads `exe`. For a script that is the interpreter, and
    // argv already names the script. The program is started by the path
    // that reads as the command that was typed, which the recording
    // holds. A name that differs from the program shown is passed along
    // as argv[0].
    const auto program = replayedProgram(process);
    if (process.argv.front() != program) {
      append(wiring, {kNameZeroOption, process.argv.front()});
    }
    for (const auto &descriptor : process.fds) {
      append(wiring, {kDescriptorOption,
                      absl::StrCat(descriptor.fd, "=",
                                   descriptorSpec(action, recording, pipes,
                                                  descriptor))});
    }

    // Keep the recorded order: wait for every process that had already
    // exited when this one called execve.
    Arguments earlier;
    for (size_t other = 0; other < index; ++other) {
      if (action.processes[other].exit < process.step) {
        earlier.push_back(std::to_string(other));
      }
    }
    if (!earlier.empty()) {
      append(wiring, {kAfterOption, absl::StrJoin(earlier, ",")});
    }
    append(wiring, {kStatusOption, std::to_string(expectedExit(process.status)),
                    kCountOption, std::to_string(process.argv.size()),
                    kEndOfOptions});
    lines.push_back(std::move(wiring));

    Arguments command{program};
    command.insert(command.end(), process.argv.begin() + 1, process.argv.end());
    lines.push_back(std::move(command));
  }
  return lines;
}

Arguments removedPaths(const Recording &recording, uint64_t step) {
  const auto &group = recording.group(step);
  return difference(recording.state(group.start), recording.state(group.end))
      .removed;
}

Arguments writeActionFiles(const Action &action, const fs::path &directory) {
  Arguments written;
  fs::create_directories(directory / kActionsDirectory);

  // The manifest: everything to stage, then everything to expect.
  std::string manifest;
  for (const auto &input : action.inputs) {
    manifest += manifestLine(kStageTag, input);
  }
  for (const auto &result : action.results) {
    manifest += manifestLine(kExpectTag, result);
  }

  // Only a trusted step is asked what else it left, so only its manifest
  // says what the recorded processes wrote besides the results.
  if (action.trusted) {
    for (const auto &entry : action.unused) {
      manifest += manifestLine(kUnusedTag, {entry, "", ""});
    }
    for (const auto &path : action.removed) {
      Entry entry;
      entry.path = path;
      manifest += manifestLine(kRemovedTag, {entry, "", ""});
    }
  }
  const auto name =
      absl::StrCat(kActionsDirectory, "/", action.name, ".manifest");
  writeBytes(directory / name, manifest);
  written.push_back(name);

  // The bytes the builder had waiting in a pipe. The first process to
  // hold the pipe saw all of them. A later one saw what was left, which
  // sharing the file reproduces.
  const auto pipes = sharedPipes(action);
  std::set<std::string> fed;
  for (const auto &process : action.processes) {
    for (const auto &descriptor : process.fds) {
      const bool feeds = isPipe(descriptor) && readable(descriptor) &&
                         descriptor.input &&
                         !pipes.contains(pipeId(descriptor));
      if (!feeds) {
        continue;
      }
      const auto file = dataFile(action, descriptor);
      if (!fed.insert(file).second) {
        continue;
      }
      writeBytes(directory / file, fromHex(*descriptor.input));
      written.push_back(file);
    }
  }
  return written;
}

void writeSharedFiles(const Recording &recording, const fs::path &directory) {
  writeBytes(directory / kSandboxFile, kSandboxScript);
  fs::permissions(directory / kSandboxFile,
                  fs::perms::owner_all | fs::perms::group_read |
                      fs::perms::group_exec | fs::perms::others_read |
                      fs::perms::others_exec);
  writeBytes(directory / kClosureFile,
             absl::StrJoin(recording.closure, "\n") + "\n");

  // A project's source tree is staged from the rule's inputs and mounted
  // over whatever the store has at that path, so the script has to know
  // the path.
  if (!recording.source.empty()) {
    writeBytes(directory / kSourceFile, recording.source + "\n");

    // A trusted step may leave nothing the recording did not see in a
    // directory the build can write, so the script has to know those
    // directories too.
    writeBytes(directory / kRootsFile,
               absl::StrJoin(recording.roots, "\n") + "\n");
  }

  // An environment is NAME=VALUE entries, each ended by a NUL, because a
  // value can hold any other byte.
  fs::create_directories(directory / kEnvironmentsDirectory);
  for (size_t index = 0; index < recording.environments.size(); ++index) {
    std::string bytes;
    for (const auto &variable : recording.environments[index]) {
      bytes += variable;
      bytes.push_back('\0');
    }
    writeBytes(directory / environmentFile(index), bytes);
  }
}

Arguments sharedFiles(const Action &action, const Recording &recording) {
  std::set<std::string> files{std::string(kSandboxFile)};
  if (!action.processes.empty()) {
    files.insert(std::string(kClosureFile));
    if (!recording.source.empty()) {
      files.insert(std::string(kSourceFile));
    }
    if (action.trusted) {
      files.insert(std::string(kRootsFile));
    }
  }
  for (const auto &process : action.processes) {
    files.insert(environmentFile(process.environment));
  }
  return Arguments(files.begin(), files.end());
}

Arguments sandboxCommand(const Action &action, const Recording &recording,
                         const fs::path &shared, const fs::path &scratch) {
  auto command =
      words({(shared / kSandboxFile).string(), kInputOption, shared.string(),
             kInputOption, scratch.string(), kInputOption,
             recording.directory.string(), kOutputOption,
             outputDirectory(scratch).string()});
  for (const auto &line : actionArguments(action, recording)) {
    command.insert(command.end(), line.begin(), line.end());
  }
  return command;
}

fs::path outputDirectory(const fs::path &scratch) {
  return scratch / kOutputDirectory;
}

std::optional<ReplayFailure> runAction(const Action &action,
                                       const Recording &recording,
                                       const fs::path &shared,
                                       const fs::path &trace) {
  Scratch scratch;
  writeActionFiles(action, scratch.path);

  // A traced run tells the script where to write and which tracer to use.
  Arguments command;
  if (!trace.empty()) {
    command = {std::string(kEnv), absl::StrCat(kTraceVariable, trace.string()),
               absl::StrCat(kTracerVariable, kStrace)};
  }
  const auto step = sandboxCommand(action, recording, shared, scratch.path);
  command.insert(command.end(), step.begin(), step.end());

  // The script reports on standard error, after whatever the step's
  // processes printed there. Its standard output carries nothing.
  const auto output = scratch.path / kReportFile;
  const int status = run(command, "/dev/null", "/dev/null", output);
  if (status == 0) {
    return std::nullopt;
  }
  return parseFailure(readBytes(output));
}
} // namespace splice_ir

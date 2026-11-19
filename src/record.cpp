#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "compiler.h"
#include "action.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "recording.h"
#include "tools.h"
#include "trace.h"
#include <map>
#include <mutex>
#include <sys/stat.h>
#include <optional>
#include <set>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
using Json = nlohmann::json;

const fs::path kStore = "/nix/store";

// The Nix sandbox builds in this directory and points TMPDIR at it.
constexpr std::string_view kBuildRoot = "/build";

// The variable of a derivation that names its source.
constexpr std::string_view kSourceVariable = "src";

// `rewind cat` exits with this status when the path did not exist at the
// step, which is how a snapshot learns that an output is not there yet.
constexpr int kRewindMissing = 3;

// How long to wait for Rewind to read a pipe before giving up on it.
constexpr std::string_view kPipeReadSeconds = "30";

// The link target prefix of a pipe in /proc/PID/fd.
constexpr std::string_view kPipePrefix = "pipe:";

// The fdinfo lines that carry a descriptor's offset and open flags. The
// flags are printed in octal.
constexpr std::string_view kPositionField = "pos:";
constexpr std::string_view kFlagsField = "flags:";
constexpr int kOctalBase = 8;

// Asks Rewind about one run. Every query forks the run at a step and reads
// the guest, so queries are independent and safe to issue in parallel.
class Rewind {
public:
  explicit Rewind(std::string run) : run(std::move(run)) {}

  // The bytes of a guest file at a step.
  std::string cat(uint64_t step, const std::string &path) const {
    return capture(command({"cat", run, std::to_string(step), path}));
  }

  // Writes a tar of a guest path at a step. Returns false when the path
  // did not exist then.
  bool archive(uint64_t step, const std::string &path,
               const fs::path &output) const {
    const int status =
        splice_ir::run(command({"cat", "--archive", "tar", run,
                                std::to_string(step), path}),
                       {}, output, fs::path("/dev/null"));
    check(status == 0 || status == kRewindMissing,
          "rewind could not archive " + path);
    return status == 0;
  }

  // The entries of a guest directory at a step, by relative path.
  std::map<std::string, Json> files(uint64_t step,
                                    const std::string &path) const {
    const auto text = capture(
        command({"files", "--json", run, std::to_string(step), path}));
    std::map<std::string, Json> entries;
    for (const std::string_view line :
         absl::StrSplit(text, '\n', absl::SkipEmpty())) {
      auto entry = Json::parse(line);
      const std::string name = entry.at("path");
      entries.emplace(name, std::move(entry));
    }
    return entries;
  }

  // The bytes waiting in a pipe that a process holds, read without
  // disturbing the run. Unset when Rewind could not produce them in time:
  // a pipe whose writer is still running has no end to read up to.
  std::optional<std::string> pipeContents(uint64_t step,
                                          const std::string &path) const {
    llvm::SmallString<128> file;
    if (llvm::sys::fs::createTemporaryFile("genrulify", "pipe", file)) {
      return std::nullopt;
    }
    const fs::path output = file.str().str();
    Arguments args{std::string(kTimeout), std::string(kPipeReadSeconds)};
    for (const auto &argument :
         command({"cat", run, std::to_string(step), path})) {
      args.push_back(argument);
    }
    const int status =
        splice_ir::run(args, {}, output, fs::path("/dev/null"));
    std::optional<std::string> bytes;
    if (status == 0) {
      bytes = readBytes(output);
    }
    fs::remove(output);
    return bytes;
  }

  std::string events() const {
    return capture(command({"events", "--json", run}));
  }

  // The directory Rewind keeps the run in.
  fs::path directory() const {
    const auto text = capture({"rewind", "ls", "--json"});
    for (const std::string_view line :
         absl::StrSplit(text, '\n', absl::SkipEmpty())) {
      const auto listed = Json::parse(line);
      if (listed.at("id") == run) {
        return listed.at("dir").get<std::string>();
      }
    }
    throw std::runtime_error("rewind has no run " + run);
  }

private:
  static Arguments command(Arguments rest) {
    Arguments args{"rewind", "--progress", "off"};
    args.insert(args.end(), rest.begin(), rest.end());
    return args;
  }

  std::string run;
};

// How many times a run is cut again to close drivers that fed a command
// through a pipe. Each round closes at least one driver that the round
// before had open, and a build has few such pipelines nested in each
// other.
constexpr size_t kMaxRefinements = 16;

// /proc carries NUL-delimited argv and envp, including empty arguments.
// `source` names the file and step for the message when the data is cut
// short. A process started with no environment has an empty envp, which
// is no fields and not a short read.
Arguments fields(std::string data, const std::string &source) {
  if (data.empty()) {
    return {};
  }
  check(data.back() == '\0', "incomplete process record in " + source);
  data.pop_back();
  return absl::StrSplit(data, '\0');
}

// Every store path the builder could have read: the closure of the
// derivation's input derivations' outputs and of its input sources.
Arguments closure(const Json &drv) {
  const auto &inputs = drv.at("inputs");
  Arguments query{std::string(kNixStore), "--query", "--requisites"};
  for (const auto &[name, dependency] : inputs.at("drvs").items()) {
    for (const std::string output : dependency.at("outputs")) {
      auto root = capture({std::string(kNixStore), "--query", "--binding",
                           output, (kStore / name).string()});
      check(root.ends_with('\n'), "missing input store path");
      root.pop_back();
      query.push_back(root);
    }
  }
  for (const std::string source : inputs.at("srcs")) {
    query.push_back((kStore / source).string());
  }
  return absl::StrSplit(capture(query), '\n', absl::SkipEmpty());
}

// Stores file bytes under their SHA-256. Snapshots are taken on several
// threads and often hold the same file, so a blob is written beside its
// final name and renamed into place.
class BlobStore {
public:
  explicit BlobStore(fs::path directory) : directory(std::move(directory)) {
    fs::create_directories(this->directory);
  }

  void add(const fs::path &file, const std::string &digest) {
    const auto destination = directory / digest;
    std::scoped_lock lock(mutex);
    if (fs::exists(destination)) {
      return;
    }
    const auto partial = directory / (digest + ".partial");
    fs::copy_file(file, partial, fs::copy_options::overwrite_existing);
    fs::permissions(partial, fs::perms::owner_read | fs::perms::owner_write);
    fs::rename(partial, destination);
  }

private:
  fs::path directory;
  std::mutex mutex;
};

// The permission, setuid, setgid and sticky bits of a mode.
constexpr mode_t kModeBits = 07777;

// Reads the entry at a host path the way a snapshot records the entry at
// `guestPath`. Throws for anything other than a regular file, a directory
// or a symlink.
Entry observe(const fs::path &host, const std::string &guestPath) {
  struct stat status {};
  check(lstat(host.c_str(), &status) == 0, "cannot stat " + host.string());
  Entry entry;
  entry.path = guestPath;
  entry.mode = status.st_mode & kModeBits;
  entry.mtime = status.st_mtim.tv_sec;
  if (S_ISDIR(status.st_mode)) {
    entry.kind = EntryKind::Directory;
    return entry;
  }
  if (S_ISLNK(status.st_mode)) {
    entry.kind = EntryKind::Symlink;

    // A symlink's permission bits are not settable on Linux and carry no
    // meaning, so every symlink has mode zero.
    entry.mode = 0;
    entry.content = fs::read_symlink(host).string();
    return entry;
  }
  check(S_ISREG(status.st_mode),
        "not a file, directory or symlink: " + host.string());
  entry.kind = EntryKind::File;
  entry.content = digest(host);
  return entry;
}

// Records the entry at a host path and everything below it. The walk
// needs owner access to hash a file or list a directory that the build left
// unreadable, so the recorded mode is read before access is granted.
void walk(const fs::path &host, const std::string &guest, State &state,
          BlobStore &blobs) {
  const auto status = fs::symlink_status(host);
  const bool symlink = status.type() == fs::file_type::symlink;
  if (!symlink) {
    fs::permissions(host, fs::perms::owner_all, fs::perm_options::add);
  }
  auto entry = observe(host, guest);
  if (!symlink) {
    entry.mode = static_cast<uint32_t>(status.permissions() & fs::perms::mask);
  }
  if (entry.kind == EntryKind::File) {
    blobs.add(host, entry.content);
  }
  state.emplace(guest, entry);
  if (entry.kind != EntryKind::Directory) {
    return;
  }
  for (const auto &child : fs::directory_iterator(host)) {
    walk(child.path(), guest + "/" + child.path().filename().string(), state,
         blobs);
  }
}

// Records a tree of the host's store, which is read-only and belongs to
// someone else, so nothing about it is changed on the way.
void observeTree(const fs::path &host, State &state, BlobStore &blobs) {
  const auto entry = observe(host, host.string());
  if (entry.kind == EntryKind::File) {
    blobs.add(host, entry.content);
  }
  state.emplace(entry.path, entry);
  if (entry.kind != EntryKind::Directory) {
    return;
  }
  for (const auto &child : fs::directory_iterator(host)) {
    observeTree(child.path(), state, blobs);
  }
}

// The source tree a derivation is given: its `src`, when that is one of
// the derivation's input sources or lies inside one. A derivation whose
// source is the output of another derivation, such as a fetched tarball,
// has no tree anyone is working in.
std::string sourceTreeOf(const Json &drv) {
  const auto &environment = drv.at("env");
  check(environment.contains(kSourceVariable),
        "the derivation has no src to convert in place");
  const std::string source = environment.at(kSourceVariable);
  for (const std::string input : drv.at("inputs").at("srcs")) {
    if (isBelow(source, (kStore / input).string())) {
      return source;
    }
  }
  throw std::runtime_error(
      "the derivation's src, " + source +
      ", is built by another derivation: only a derivation that is given a "
      "directory can be converted in place");
}

// The entries below the given guest paths at one step.
State snapshot(const Rewind &rewind, uint64_t step, const Arguments &roots,
               BlobStore &blobs) {
  State state;
  Scratch scratch;
  for (size_t index = 0; index < roots.size(); ++index) {
    const fs::path root = roots[index];
    const auto tar = scratch.path / (std::to_string(index) + ".tar");
    if (!rewind.archive(step, root.string(), tar)) {
      continue;
    }

    // The archive holds the tree under the root's last name.
    const auto extracted = scratch.path / std::to_string(index);
    fs::create_directory(extracted);
    execute({std::string(kTar), "--extract", "--preserve-permissions",
             "--no-same-owner", "--file", tar.string(), "--directory",
             extracted.string()});
    walk(extracted / root.filename(), root.string(), state, blobs);
  }
  return state;
}

// A descriptor's offset and flags from the text of /proc/PID/fdinfo/N.
void readDescriptorInfo(const std::string &text, Descriptor &descriptor) {
  for (const std::string_view line :
       absl::StrSplit(text, '\n', absl::SkipEmpty())) {
    const std::pair<std::string_view, std::string_view> field =
        absl::StrSplit(line, absl::MaxSplits('\t', 1));
    if (field.first == kPositionField) {
      descriptor.position = std::stoull(std::string(field.second));
    }
    if (field.first == kFlagsField) {
      descriptor.flags =
          std::stoul(std::string(field.second), nullptr, kOctalBase);
    }
  }
}

bool belowRoots(const std::string &path, const Arguments &roots) {
  return std::ranges::any_of(
      roots, [&](const std::string &root) { return isBelow(path, root); });
}

// Reads what a process looked like at the step of its execve: the program
// the kernel loaded, argv, environment, working directory and descriptors.
Process inspect(const Rewind &rewind, const Cut &cut, const Arguments &roots,
                Arguments &environment) {
  const std::string proc = "/proc/" + std::to_string(cut.pid);
  Process process;
  process.pid = cut.pid;
  process.step = cut.step;
  process.exit = cut.end;
  process.status = cut.status;
  process.name = cut.name;
  process.filename = cut.filename;
  const auto at = " at step " + std::to_string(cut.step);
  process.argv = fields(rewind.cat(cut.step, proc + "/cmdline"),
                        proc + "/cmdline" + at);
  check(!process.argv.empty(), "no arguments in " + proc + "/cmdline" + at);
  environment = fields(rewind.cat(cut.step, proc + "/environ"),
                       proc + "/environ" + at);

  // The exe link names the interpreter when execve was given a script.
  const auto links = rewind.files(cut.step, proc);
  process.exe = links.at("exe").at("target");
  process.cwd = links.at("cwd").at("target");

  for (const auto &[name, link] : rewind.files(cut.step, proc + "/fd")) {
    Descriptor descriptor;
    descriptor.fd = std::stoi(name);
    descriptor.target = link.at("target");
    descriptor.mode = link.at("mode");

    // Only a descriptor on a file the build can change is reopened at
    // replay, and reopening needs the offset and the append flag.
    if (belowRoots(descriptor.target, roots)) {
      readDescriptorInfo(rewind.cat(cut.step, proc + "/fdinfo/" + name),
                         descriptor);
    }
    process.fds.push_back(std::move(descriptor));
  }
  return process;
}

// Joins cuts that cannot be replayed apart. Cuts that hold the same pipe
// exchange bytes no snapshot sees. Cuts that overlap in time interleave
// their writes between two snapshots, and a FIFO or a file passed between
// concurrent processes looks the same way.
//
// One kind of pipe joins only the cuts that run at the same time: a pipe
// of which every cut that holds it holds both ends. No cut is upstream of
// another on such a pipe. A driver made the pipe and handed both ends to
// each command it started, as cargo and make do with the pipe of their
// job server. Commands that ran one after another are replayed apart,
// each with a pipe of its own, and the replay check decides whether a
// command needed anything an earlier command left in the pipe.
std::vector<Group> group(std::vector<Process> processes,
                         const std::vector<Cut> &cuts, const Arguments &roots) {
  std::vector<size_t> leader(processes.size());
  for (size_t index = 0; index < leader.size(); ++index) {
    leader[index] = index;
  }
  const auto find = [&](size_t index) {
    while (leader[index] != index) {
      index = leader[index];
    }
    return index;
  };

  // For each pipe, the cuts that hold an end of the pipe, in the order
  // the cuts began, and whether a cut can read the pipe and write it.
  struct Ends {
    bool read = false;
    bool write = false;
  };
  std::map<std::string, std::map<size_t, Ends>> holders;
  for (size_t index = 0; index < processes.size(); ++index) {
    for (const auto &descriptor : processes[index].fds) {
      if (!isPipe(descriptor)) {
        continue;
      }
      auto &ends = holders[descriptor.target][index];
      ends.read = ends.read || readable(descriptor);
      ends.write = ends.write || writable(descriptor);
    }
  }

  for (const auto &[pipe, held] : holders) {
    const bool shared = std::ranges::all_of(held, [](const auto &holder) {
      return holder.second.read && holder.second.write;
    });

    // Union the cuts that name the pipe: all of them, or for a pipe every
    // cut holds both ends of, those whose lifetimes overlap. The cuts are
    // in the order they began, so a cut overlaps an earlier one when the
    // cut began before every earlier one had ended.
    std::optional<size_t> previous;
    uint64_t running = 0;
    for (const auto &[index, ends] : held) {
      if (previous && (!shared || cuts[index].step <= running)) {
        leader[find(index)] = find(*previous);
      }
      previous = index;
      running = std::max(running, cuts[index].end);
    }
  }

  // Build one group per pipe component.
  std::map<size_t, Group> components;
  for (size_t index = 0; index < processes.size(); ++index) {
    auto &component = components[find(index)];
    const bool first = component.processes.empty();
    component.start = first ? cuts[index].step
                            : std::min(component.start, cuts[index].step);
    component.end = std::max(component.end, cuts[index].end);
    component.processes.push_back(std::move(processes[index]));
    for (const auto &path : cuts[index].reads) {
      if (belowRoots(path, roots)) {
        component.reads.push_back(path);
      }
    }
  }
  std::vector<Group> ordered;
  for (auto &[index, component] : components) {
    ordered.push_back(std::move(component));
  }
  std::ranges::sort(ordered, {}, &Group::start);

  // Merge groups whose lifetimes overlap.
  std::vector<Group> groups;
  for (auto &next : ordered) {
    if (groups.empty() || next.start > groups.back().end) {
      groups.push_back(std::move(next));
      continue;
    }
    auto &previous = groups.back();
    previous.end = std::max(previous.end, next.end);
    std::ranges::move(next.processes, std::back_inserter(previous.processes));
    std::ranges::move(next.reads, std::back_inserter(previous.reads));
  }
  for (auto &merged : groups) {
    std::ranges::sort(merged.processes, {}, &Process::step);
    std::ranges::sort(merged.reads);
    const auto duplicates = std::ranges::unique(merged.reads);
    merged.reads.erase(duplicates.begin(), duplicates.end());
  }
  return groups;
}
// Captures the input of every process that reads a pipe no process of its
// group writes. The builder wrote such a pipe, typically a here-string or a
// short here-document, and wrote it before starting the reader, so at the
// reader's execve the pipe holds everything the reader will ever get from
// it. Whether that holds for a given pipe is not assumed: the replay check
// rejects a group whose captured input does not reproduce its writes.
void captureBuilderPipes(const Rewind &rewind, std::vector<Group> &groups) {
  struct Wanted {
    Process *process;
    Descriptor *descriptor;
  };
  std::vector<Wanted> wanted;
  for (auto &group : groups) {
    std::set<std::string> written;
    for (const auto &process : group.processes) {
      for (const auto &descriptor : process.fds) {
        if (isPipe(descriptor) && writable(descriptor)) {
          written.insert(descriptor.target);
        }
      }
    }
    for (auto &process : group.processes) {
      for (auto &descriptor : process.fds) {
        if (isPipe(descriptor) && readable(descriptor) &&
            !written.contains(descriptor.target)) {
          wanted.push_back({&process, &descriptor});
        }
      }
    }
  }
  parallelFor(wanted.size(), [&](size_t index) {
    const auto &[process, descriptor] = wanted[index];
    const auto path = "/proc/" + std::to_string(process->pid) + "/fd/" +
                      std::to_string(descriptor->fd);
    if (const auto bytes = rewind.pipeContents(process->step, path)) {
      descriptor->input = toHex(*bytes);
    }
  });
}
} // namespace

std::vector<Group> regroup(const Recording &recording) {
  // A cut for each process, in the order the processes began, which is
  // the order the grouping takes them in.
  std::vector<Process> processes;
  for (const auto &group : recording.groups) {
    std::ranges::copy(group.processes, std::back_inserter(processes));
  }
  std::ranges::sort(processes, {}, &Process::step);
  std::vector<Cut> cuts;
  for (const auto &process : processes) {
    Cut cut;
    cut.pid = process.pid;
    cut.step = process.step;
    cut.end = process.exit;
    cuts.push_back(std::move(cut));
  }
  return group(std::move(processes), cuts, recording.roots);
}

void checkSameSource(const fs::path &tree, const fs::path &directory) {
  const auto differs = [&](const fs::path &path, std::string_view how) {
    return std::runtime_error(absl::StrCat(
        path.lexically_relative(tree).string(), " ", how, ": ",
        directory.string(), " no longer holds the source the derivation was "
        "evaluated with, ", tree.string()));
  };
  for (const auto &entry : fs::recursive_directory_iterator(tree)) {
    const auto ours = directory / entry.path().lexically_relative(tree);
    const auto status = fs::symlink_status(ours);
    if (!fs::exists(status)) {
      throw differs(entry.path(), "is missing");
    }
    if (entry.is_symlink()) {
      if (!fs::is_symlink(status) ||
          fs::read_symlink(ours) != fs::read_symlink(entry.path())) {
        throw differs(entry.path(), "differs");
      }
      continue;
    }
    if (entry.is_directory()) {
      if (!fs::is_directory(status)) {
        throw differs(entry.path(), "differs");
      }
      continue;
    }
    if (!fs::is_regular_file(status) ||
        fs::file_size(ours) != entry.file_size() ||
        digest(ours) != digest(entry.path())) {
      throw differs(entry.path(), "differs");
    }
  }
}

void recordRun(const std::string &run, const fs::path &drvPath,
               const fs::path &directory, const std::string &open,
               Granularity granularity,
               const std::optional<fs::path> &project) {
  const Rewind rewind(run);
  check(fs::create_directory(directory), "destination already exists");
  try {
    Recording recording;
    recording.directory = directory;
    recording.run = run;

    // Read the derivation: its outputs name the writable store paths, and
    // its inputs bound what the builder could read.
    const auto drv =
        Json::parse(capture({std::string(kNix), "derivation", "show",
                             drvPath.string()}))
            .at("derivations")
            .at(drvPath.filename().string());
    recording.name = drv.at("name");
    recording.roots = {std::string(kBuildRoot)};
    for (const auto &[name, output] : drv.at("outputs").items()) {
      const std::string path = drv.at("env").at(name);
      recording.outputs[name] = path;
      recording.roots.push_back(path);
    }
    recording.closure = closure(drv);

    // A project converted in place: its source tree becomes part of the
    // build, and is no longer something the build reads from the store.
    if (project) {
      recording.source = sourceTreeOf(drv);
      checkSameSource(recording.source, *project);
      std::erase(recording.closure, recording.source);
    }

    // Find the builder and the cuts in the event stream.
    Scratch scratch;
    const auto events = scratch.path / "events.jsonl";
    writeBytes(events, rewind.events());
    const auto runDirectory = rewind.directory();
    auto analysis =
        analyze(events, runDirectory / "trace.bin", open, granularity);

    // Rewind's manifest holds how long the run took, which bounds how long
    // a replay of any part of it may take.
    recording.wallMilliseconds =
        Json::parse(readBytes(runDirectory / "manifest.json"))
            .at("outcome")
            .at("wall_ms");
    recording.open = open;
    recording.fine = granularity == Granularity::Fine;
    check(analysis.builder == drv.at("builder").get<std::string>(),
          "the run's builder is not the derivation's");
    recording.initialStep = analysis.initialStep;
    recording.finalStep = analysis.finalStep;
    recording.lost = analysis.lost;

    // What the trace shows of a process at its execve, kept across rounds
    // of the loop below: asking Rewind is the slow part.
    std::map<Image, std::pair<Process, Arguments>> inspected;
    std::set<Image> closed;
    for (size_t round = 0;; ++round) {
      // Inspect every cut process the earlier rounds have not seen.
      std::vector<const Cut *> unseen;
      for (const auto &cut : analysis.cuts) {
        if (inspected.try_emplace({cut.pid, cut.step}).second) {
          unseen.push_back(&cut);
        }
      }
      std::vector<std::pair<Process, Arguments>> fresh(unseen.size());
      parallelFor(unseen.size(), [&](size_t index) {
        fresh[index].first = inspect(rewind, *unseen[index], recording.roots,
                                     fresh[index].second);
      });
      for (size_t index = 0; index < unseen.size(); ++index) {
        inspected[{unseen[index]->pid, unseen[index]->step}] =
            std::move(fresh[index]);
      }

      // Most processes inherit the same environment. Store each one once.
      std::vector<Process> processes;
      std::map<Arguments, size_t> known;
      recording.environments.clear();
      for (const auto &cut : analysis.cuts) {
        const auto &[process, environment] = inspected.at({cut.pid, cut.step});
        const auto [slot, added] =
            known.emplace(environment, recording.environments.size());
        if (added) {
          recording.environments.push_back(environment);
        }
        processes.push_back(process);
        processes.back().environment = slot->second;
      }

      // A command's reads are kept where the build can write, and in a
      // project's source tree, whose files are inputs like any other.
      auto readable = recording.roots;
      if (!recording.source.empty()) {
        readable.push_back(recording.source);
      }
      recording.groups = group(std::move(processes), analysis.cuts, readable);
      captureBuilderPipes(rewind, recording.groups);

      // A command that reads a pipe a driver wrote into while the command
      // ran cannot be replayed alone: the bytes came from a shell builtin
      // and are in no file. When the driver is one the trace opened, close
      // it, so that the driver is replayed whole with the command inside,
      // and cut the run again.
      std::map<Image, const Cut *> cutOf;
      for (const auto &cut : analysis.cuts) {
        cutOf[{cut.pid, cut.step}] = &cut;
      }
      std::set<Image> more;
      for (const auto &group : recording.groups) {
        const auto reader = readerOfDriverPipe(group);
        if (!reader) {
          continue;
        }
        const auto *cut = cutOf.at({reader->first->pid, reader->first->step});
        if (cut->opener && !closed.contains(*cut->opener)) {
          more.insert(*cut->opener);
        }
      }
      if (more.empty() || round == kMaxRefinements) {
        break;
      }
      closed.insert(more.begin(), more.end());
      analysis =
          analyze(events, runDirectory / "trace.bin", open, granularity, closed);
    }

    // Snapshot the writable roots where the graph needs a state: before
    // and after each group, when the builder starts and when it exits.
    std::set<uint64_t> wanted{recording.initialStep, recording.finalStep};
    for (const auto &group : recording.groups) {
      wanted.insert(group.start);
      wanted.insert(group.end);
    }
    const std::vector<uint64_t> steps(wanted.begin(), wanted.end());
    BlobStore blobs(directory / kBlobsDirectory);
    fs::create_directory(directory / kStatesDirectory);
    parallelFor(steps.size(), [&](size_t index) {
      const auto state =
          snapshot(rewind, steps[index], recording.roots, blobs);
      writeState(directory / kStatesDirectory /
                     (std::to_string(steps[index]) + ".json"),
                 state);
    });

    // The source tree is the same at every step, and the host has it.
    if (!recording.source.empty()) {
      State tree;
      observeTree(recording.source, tree, blobs);
      writeState(directory / kStatesDirectory /
                     (std::string(kSourceState) + ".json"),
                 tree);
    }

    // The guest's /bin and /etc are part of what a process can observe.
    const Arguments platformRoots(std::begin(kPlatformRoots),
                                  std::end(kPlatformRoots));
    writeState(directory / kStatesDirectory /
                   (std::string(kPlatformState) + ".json"),
               snapshot(rewind, recording.initialStep, platformRoots, blobs));
    recording.save();
  } catch (...) {
    removeTree(directory);
    throw;
  }
}
} // namespace splice_ir

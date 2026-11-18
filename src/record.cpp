#include "absl/strings/str_split.h"
#include "compiler.h"
#include "layer.h"
#include "plan.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "recording.h"
#include "tools.h"
#include "trace.h"
#include <map>
#include <mutex>
#include <optional>
#include <set>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
using Json = nlohmann::json;

const fs::path kStore = "/nix/store";

// The Nix sandbox builds in this directory and points TMPDIR at it.
constexpr std::string_view kBuildRoot = "/build";

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
    if (llvm::sys::fs::createTemporaryFile("nixsplice", "pipe", file)) {
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

// /proc carries NUL-delimited argv and envp, including empty arguments.
Arguments fields(std::string data) {
  check(!data.empty() && data.back() == '\0', "incomplete process record");
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
  process.argv = fields(rewind.cat(cut.step, proc + "/cmdline"));
  environment = fields(rewind.cat(cut.step, proc + "/environ"));

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
std::vector<Group> group(std::vector<Process> processes,
                         const std::vector<Cut> &cuts, const Arguments &roots) {
  // Union cuts that name the same pipe.
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
  std::map<std::string, size_t> pipeHolder;
  for (size_t index = 0; index < processes.size(); ++index) {
    for (const auto &descriptor : processes[index].fds) {
      if (!descriptor.target.starts_with(kPipePrefix)) {
        continue;
      }
      const auto [holder, fresh] =
          pipeHolder.emplace(descriptor.target, index);
      if (!fresh) {
        leader[find(index)] = find(holder->second);
      }
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

void recordRun(const std::string &run, const fs::path &drvPath,
               const fs::path &directory, const std::string &open) {
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

    // Find the builder and the cuts in the event stream.
    Scratch scratch;
    const auto events = scratch.path / "events.jsonl";
    writeBytes(events, rewind.events());
    const auto runDirectory = rewind.directory();
    const auto analysis = analyze(events, runDirectory / "trace.bin", open);

    // Rewind's manifest holds how long the run took, which bounds how long
    // a replay of any part of it may take.
    recording.wallMilliseconds =
        Json::parse(readBytes(runDirectory / "manifest.json"))
            .at("outcome")
            .at("wall_ms");
    recording.open = open;
    check(analysis.builder == drv.at("builder").get<std::string>(),
          "the run's builder is not the derivation's");
    recording.initialStep = analysis.initialStep;
    recording.finalStep = analysis.finalStep;
    recording.lost = analysis.lost;

    // Inspect every cut process at its execve.
    std::vector<Process> processes(analysis.cuts.size());
    std::vector<Arguments> environments(analysis.cuts.size());
    parallelFor(analysis.cuts.size(), [&](size_t index) {
      processes[index] = inspect(rewind, analysis.cuts[index], recording.roots,
                                 environments[index]);
    });

    // Most processes inherit the same environment. Store each one once.
    std::map<Arguments, size_t> known;
    for (size_t index = 0; index < processes.size(); ++index) {
      const auto [slot, fresh] =
          known.emplace(environments[index], recording.environments.size());
      if (fresh) {
        recording.environments.push_back(environments[index]);
      }
      processes[index].environment = slot->second;
    }
    recording.groups =
        group(std::move(processes), analysis.cuts, recording.roots);
    captureBuilderPipes(rewind, recording.groups);

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

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "layer.h"
#include "plan.h"
#include "tools.h"
#include <chrono>
#include <climits>
#include <condition_variable>
#include <csignal>
#include <fcntl.h>
#include <format>
#include <map>
#include <mutex>
#include <ranges>
#include <set>
#include <sys/ioctl.h>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
// The identity the Nix sandbox gives a builder, which the Rewind guest
// reproduces: the first build user, the build group and a fixed hostname.
constexpr std::string_view kBuildUser = "1000";
constexpr std::string_view kBuildGroup = "100";
constexpr std::string_view kHostName = "localhost";

// Guest directories that are backed by the staged tree.
constexpr std::string_view kBuildDirectory = "/build";
constexpr std::string_view kStoreDirectory = "/nix/store";
constexpr std::string_view kTemporaryDirectory = "/tmp";

// Descriptor targets under this prefix are devices. The guest's console
// devices do not exist outside the VM, and no build output depends on what
// was written to them.
constexpr std::string_view kDevicePrefix = "/dev/";

// A shell exits with this value plus the signal number when a child is
// killed by a signal, and so does bubblewrap.
constexpr int kSignalExitBase = 128;

// The first descriptor number a child parks its redirection sources at,
// above every number a recorded process had open.
constexpr int kParkedDescriptor = 1000;

// The status a child exits with when it cannot become the sandbox.
constexpr int kSpawnFailure = 127;

// How often a pipe's supervisor looks for a writer the builder would have
// unblocked, and the most bytes it drops per read.
constexpr std::chrono::milliseconds kPipePoll{20};
constexpr size_t kCopyBuffer = 64 * 1024;

// The mode of a file a redirection creates, before the umask.
constexpr mode_t kCreatedFileMode = 0666;

constexpr int kStandardInput = 0;
constexpr int kStandardError = 2;

using Layers = std::map<std::string, Layer>;

Layers openLayers(const std::vector<fs::path> &files) {
  Layers layers;
  for (const auto &file : files) {
    Layer layer(file);
    const auto name = layer.name();
    check(layers.emplace(name, std::move(layer)).second,
          "two layers are named " + name);
  }
  return layers;
}

// The host path that backs a guest path. Refuses a path that could leave
// the staged tree.
fs::path hostPath(const fs::path &root, const std::string &guest) {
  const fs::path path(guest);
  check(path.is_absolute() && path == path.lexically_normal(),
        "unexpected guest path: " + guest);
  return root / path.relative_path();
}

// Creates the plan's input entries below `root`. Paths sort parents before
// children. Directories get their recorded mode and mtime last, because a
// recorded mode can deny the writes that create the children.
void stage(const Layers &layers, const std::vector<PlanInput> &inputs,
           const fs::path &root) {
  struct Item {
    const Layer *layer;
    Entry entry;
  };
  std::map<std::string, Item> items;
  for (const auto &input : inputs) {
    const auto layer = layers.find(input.layer);
    check(layer != layers.end(), "missing layer " + input.layer);
    const auto entry = layer->second.entries().find(input.path);
    check(entry != layer->second.entries().end(),
          "layer " + input.layer + " has no " + input.path);
    items[input.path] = {&layer->second, entry->second};
  }

  std::vector<const Entry *> directories;
  for (const auto &[path, item] : items) {
    const auto destination = hostPath(root, path);
    fs::create_directories(destination.parent_path());
    item.layer->materialize(item.entry, destination);
    if (item.entry.kind == EntryKind::Directory) {
      directories.push_back(&item.entry);
    }
  }
  for (const auto *directory : directories | std::views::reverse) {
    sealDirectory(*directory, hostPath(root, directory->path));
  }
}

// The bubblewrap command that starts one process the way the recording saw
// it start: same program, argv, environment and working directory, with the
// store closure at its original paths and the staged tree as the only
// writable state.
Arguments sandbox(const Plan &plan, const Process &process,
                  const fs::path &root) {
  const auto backing = [&](std::string_view guest) {
    return hostPath(root, std::string(guest)).string();
  };
  Arguments command{std::string(kBwrap),
                    "--unshare-all",
                    "--die-with-parent",
                    "--clearenv",
                    "--uid",
                    std::string(kBuildUser),
                    "--gid",
                    std::string(kBuildGroup),
                    "--hostname",
                    std::string(kHostName),
                    "--dev",
                    "/dev",
                    "--proc",
                    "/proc",
                    "--bind",
                    backing(kTemporaryDirectory),
                    std::string(kTemporaryDirectory),
                    "--bind",
                    backing(kStoreDirectory),
                    std::string(kStoreDirectory)};
  for (const std::string_view directory : kPlatformRoots) {
    command.insert(command.end(), {"--ro-bind", backing(directory),
                                   std::string(directory)});
  }
  for (const auto &path : plan.closure) {
    command.insert(command.end(), {"--ro-bind", path, path});
  }
  command.insert(command.end(), {"--bind", backing(kBuildDirectory),
                                 std::string(kBuildDirectory)});
  for (const auto &variable : plan.environments.at(process.environment)) {
    const auto equals = variable.find('=');
    check(equals != std::string::npos && equals != 0,
          "invalid environment entry");
    command.insert(command.end(), {"--setenv", variable.substr(0, equals),
                                   variable.substr(equals + 1)});
  }
  check(!process.argv.empty(), "missing executable arguments");
  command.insert(command.end(), {"--chdir", process.cwd, "--argv0",
                                 process.argv.front(), "--", process.exe});
  command.insert(command.end(), process.argv.begin() + 1, process.argv.end());
  return command;
}

// Starts one sandboxed process with the given descriptor table. Returns
// the child's pid.
pid_t spawn(const Arguments &command,
            const std::vector<std::pair<int, int>> &redirections) {
  std::vector<char *> argv;
  for (const auto &argument : command) {
    argv.push_back(const_cast<char *>(argument.c_str()));
  }
  argv.push_back(nullptr);

  // The checker replays groups on several threads. After fork() in a
  // threaded process the child may only make async-signal-safe calls, and
  // allocating is not one, so the child's scratch space is reserved here.
  std::vector<int> parked(redirections.size());

  const pid_t pid = fork();
  check(pid >= 0, "cannot fork");
  if (pid != 0) {
    return pid;
  }

  // In the child. A source may sit at a number another redirection
  // targets, so park every source out of the way before placing any.
  for (size_t index = 0; index < redirections.size(); ++index) {
    parked[index] =
        fcntl(redirections[index].second, F_DUPFD_CLOEXEC, kParkedDescriptor);
    if (parked[index] < 0) {
      _exit(kSpawnFailure);
    }
  }
  for (size_t index = 0; index < redirections.size(); ++index) {
    if (dup2(parked[index], redirections[index].first) < 0) {
      _exit(kSpawnFailure);
    }
  }
  execv(argv.front(), argv.data());
  _exit(kSpawnFailure);
}

// Runs a plan's processes the way the recording ordered them and plays the
// builder's part between them.
//
// The builder is not replayed, yet the builder shaped how its children ran:
// the builder started one command after another finished, and the builder
// read the pipes of command substitutions and `while read` loops. The
// supervisor reproduces both from the recording. A process starts once
// every process that had exited before its execve has exited here. A pipe
// stays open for reading until the processes that hold its read end are
// gone, and its leftover bytes are then discarded, as the builder's read
// loop consumed them.
class Supervisor {
public:
  Supervisor(const Plan &plan, const fs::path &root)
      : plan(plan), root(root), children(plan.processes.size()) {
    devNull = open("/dev/null", O_RDWR | O_CLOEXEC);
    check(devNull >= 0, "cannot open /dev/null");

    // Find which processes hold each end of each recorded pipe.
    for (size_t index = 0; index < plan.processes.size(); ++index) {
      const auto &process = plan.processes[index];
      for (const auto &descriptor : process.fds) {
        if (!isPipe(descriptor)) {
          continue;
        }
        auto &pipe = pipes[descriptor.target];
        (readable(descriptor) ? pipe.readers : pipe.writers).insert(index);
        pipe.broke |= writable(descriptor) && WIFSIGNALED(process.status) &&
                      WTERMSIG(process.status) == SIGPIPE;
      }
    }

    // A pipe with one end in the plan had the builder on the other end.
    // Writers to such a pipe are given /dev/null, and readers of one make
    // the group unreplayable before a plan is ever written.
    std::erase_if(pipes, [](const auto &named) {
      return named.second.readers.empty() || named.second.writers.empty();
    });
    for (auto &[target, pipe] : pipes) {
      std::array<int, 2> ends{};
      check(pipe2(ends.data(), O_CLOEXEC) == 0, "cannot create a pipe");
      pipe.readEnd = ends[0];
      pipe.writeEnd = ends[1];
    }
  }

  ~Supervisor() { close(devNull); }

  // Runs every process to completion and returns their wait statuses in
  // plan order.
  std::vector<int> run() {
    std::vector<std::jthread> threads;
    for (auto &[target, pipe] : pipes) {
      threads.emplace_back([this, &pipe] { superviseReadEnd(pipe); });
    }
    threads.emplace_back([this] { enforceDeadline(); });

    // A failure to start a process must not leave the other threads
    // waiting for processes that will now never run.
    try {
      launch(threads);
    } catch (...) {
      abort();
      threads.clear();
      throw;
    }

    // Joining waits for every child and for every pipe to be released.
    threads.clear();
    std::vector<int> statuses;
    for (const auto &child : children) {
      statuses.push_back(child.status);
    }
    return statuses;
  }

private:
  // Starts each process in recorded order and a thread that reaps it.
  void launch(std::vector<std::jthread> &threads) {
    for (size_t index = 0; index < plan.processes.size(); ++index) {
      const auto &process = plan.processes[index];

      // Keep the recorded order: wait for every process that had already
      // exited when this one called execve.
      {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] {
          if (aborted) {
            return true;
          }
          for (size_t other = 0; other < index; ++other) {
            if (plan.processes[other].exit < process.step &&
                !children[other].exited) {
              return false;
            }
          }
          return true;
        });
        if (aborted) {
          return;
        }
      }

      // Open the process's descriptors now, after its predecessors wrote
      // the files a redirection may point at.
      std::vector<int> opened;
      std::vector<std::pair<int, int>> redirections;
      for (const auto &descriptor : process.fds) {
        redirections.emplace_back(descriptor.fd, source(descriptor, opened));
      }
      const pid_t pid = spawn(sandbox(plan, process, root), redirections);
      for (const int fd : opened) {
        close(fd);
      }
      {
        std::scoped_lock lock(mutex);
        children[index].pid = pid;
        children[index].started = true;
        closeSpawnedWriteEnds(index);
      }
      changed.notify_all();

      threads.emplace_back([this, index, pid] {
        int status = 0;
        waitpid(pid, &status, 0);
        {
          std::scoped_lock lock(mutex);
          children[index].status = status;
          children[index].exited = true;
        }
        changed.notify_all();
      });
    }
  }

  // Gives up on the plan: kills what is running and releases every pipe,
  // so that each thread of the supervisor can finish.
  void abort() {
    {
      std::scoped_lock lock(mutex);
      aborted = true;
      for (const auto &child : children) {
        if (child.started && !child.exited) {
          kill(child.pid, SIGKILL);
        }
      }
      for (auto &[target, pipe] : pipes) {
        if (pipe.writeEnd >= 0) {
          close(pipe.writeEnd);
          pipe.writeEnd = -1;
        }
      }
    }
    changed.notify_all();
  }

  // A process that waits for input the recording had and the replay does
  // not would otherwise wait forever. Abort the plan when it has run for
  // longer than the plan allows.
  void enforceDeadline() {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(plan.timeoutSeconds);
    std::unique_lock lock(mutex);
    const bool finished = changed.wait_until(lock, deadline, [&] {
      return aborted || std::ranges::all_of(children, &Child::exited);
    });
    lock.unlock();
    if (!finished) {
      abort();
    }
  }

  struct Child {
    pid_t pid = -1;
    bool started = false;
    bool exited = false;
    int status = 0;
  };

  struct Pipe {
    int readEnd = -1;
    int writeEnd = -1;

    // Indices of the plan's processes that hold each end.
    std::set<size_t> readers;
    std::set<size_t> writers;

    // Whether the recording saw a writer die of SIGPIPE, which means the
    // read side went away while the writer still had output.
    bool broke = false;
  };

  // The descriptor of the runner that stands in for a recorded one. Files
  // opened for this one process are added to `opened`.
  int source(const Descriptor &descriptor, std::vector<int> &opened) {
    if (isPipe(descriptor)) {
      const auto pipe = pipes.find(descriptor.target);

      // The other end was the builder's, as for a command substitution.
      // Nothing replays the builder, so the bytes go nowhere.
      if (pipe == pipes.end()) {
        return devNull;
      }
      return readable(descriptor) ? pipe->second.readEnd
                                  : pipe->second.writeEnd;
    }
    if (descriptor.target.starts_with(kDevicePrefix)) {
      return descriptor.fd == kStandardInput ? devNull : kStandardError;
    }

    // A file the shell opened for the process. A truncating redirection of
    // a later process in a group runs after the group's snapshot, and the
    // offset shows whether one took place.
    const int access = descriptor.flags & O_ACCMODE;
    const bool append = (descriptor.flags & O_APPEND) != 0;
    int flags = access | O_CLOEXEC | (append ? O_APPEND : 0);
    if (access != O_RDONLY) {
      flags |= O_CREAT;
      if (!append && descriptor.position == 0) {
        flags |= O_TRUNC;
      }
    }

    // A store path the build could only read is not staged. The sandbox
    // mounts the host's copy at the same path, so open that one.
    const bool inClosure = std::ranges::any_of(
        plan.closure, [&](const std::string &path) {
          return isBelow(descriptor.target, path);
        });
    const auto host = inClosure ? fs::path(descriptor.target)
                                : hostPath(root, descriptor.target);
    const int fd = open(host.c_str(), flags, kCreatedFileMode);
    check(fd >= 0, "cannot open " + descriptor.target);
    opened.push_back(fd);
    if (!append && descriptor.position != 0) {
      check(lseek(fd, descriptor.position, SEEK_SET) >= 0,
            "cannot seek in " + descriptor.target);
    }
    return fd;
  }

  // Closes the runner's copy of a pipe's write end once every writer has
  // its own, so that readers see end of file when the writers exit.
  void closeSpawnedWriteEnds(size_t spawned) {
    for (auto &[target, pipe] : pipes) {
      const bool allStarted = std::ranges::all_of(
          pipe.writers, [&](size_t index) { return children[index].started; });
      if (pipe.writeEnd >= 0 && pipe.writers.contains(spawned) && allStarted) {
        close(pipe.writeEnd);
        pipe.writeEnd = -1;
      }
    }
  }

  // Holds a pipe's read end the way the builder did.
  void superviseReadEnd(Pipe &pipe) {
    const auto all = [&](auto member) {
      return std::ranges::all_of(pipe.readers, [&](size_t index) {
        return children[index].*member;
      });
    };
    const auto anyRunning = [&] {
      return std::ranges::any_of(pipe.readers, [&](size_t index) {
        return children[index].started && !children[index].exited;
      });
    };

    bool release = false;
    {
      std::unique_lock lock(mutex);
      while (!aborted && !all(&Child::exited)) {
        // A writer that fills the pipe while no reader runs was unblocked
        // by the builder in the recording. Discard what the pipe holds.
        if (!anyRunning() && full(pipe.readEnd)) {
          lock.unlock();
          discard(pipe.readEnd, available(pipe.readEnd));
          lock.lock();
          continue;
        }
        changed.wait_for(lock, kPipePoll);
      }
      release = aborted;
    }

    // Every reader is gone. Close the read end if the recording saw a
    // writer killed for writing on, and otherwise let the writers finish.
    if (!release && !pipe.broke) {
      while (discard(pipe.readEnd, kCopyBuffer) > 0) {
      }
    }
    close(pipe.readEnd);
  }

  static size_t available(int fd) {
    int count = 0;
    return ioctl(fd, FIONREAD, &count) == 0 ? count : 0;
  }

  // Whether a write of up to PIPE_BUF bytes would block.
  static bool full(int fd) {
    const int capacity = fcntl(fd, F_GETPIPE_SZ);
    return capacity > 0 &&
           available(fd) + PIPE_BUF > static_cast<size_t>(capacity);
  }

  // Reads and drops up to `count` bytes. Returns the bytes read, and zero
  // at end of file.
  static ssize_t discard(int fd, size_t count) {
    std::vector<char> buffer(std::min(count, kCopyBuffer));
    return buffer.empty() ? 0 : read(fd, buffer.data(), buffer.size());
  }

  const Plan &plan;
  fs::path root;
  int devNull = -1;
  std::map<std::string, Pipe> pipes;

  // Guards `children`, `aborted` and the pipes' write ends.
  std::mutex mutex;
  bool aborted = false;
  std::condition_variable changed;
  std::vector<Child> children;
};

// The exit status a sandbox reports for a recorded wait status.
int expectedExit(int status) {
  if (WIFSIGNALED(status)) {
    return kSignalExitBase + WTERMSIG(status);
  }
  return WEXITSTATUS(status);
}

std::string describe(const Entry &entry) {
  return std::format("{} {:o} {}", kindName(entry.kind), entry.mode,
                     entry.content);
}

// Creates the directories every sandbox mounts, whether or not an input
// entry populates them.
void prepareRoot(const fs::path &root) {
  for (const std::string_view directory :
       {kBuildDirectory, kStoreDirectory, kTemporaryDirectory}) {
    fs::create_directories(hostPath(root, std::string(directory)));
  }
  for (const std::string_view directory : kPlatformRoots) {
    fs::create_directories(hostPath(root, std::string(directory)));
  }
}
} // namespace

void replayPlan(const Plan &plan, const std::vector<fs::path> &layerFiles,
                const fs::path &output) {
  const auto layers = openLayers(layerFiles);
  Scratch scratch;
  const auto root = scratch.path / "root";
  prepareRoot(root);

  // Stage the platform's /bin and /etc, then the declared inputs.
  const auto platform = layers.find(plan.platform);
  check(platform != layers.end(), "missing layer " + plan.platform);
  std::vector<PlanInput> platformInputs;
  for (const auto &[path, entry] : platform->second.entries()) {
    platformInputs.push_back({plan.platform, path});
  }
  stage(layers, platformInputs, root);
  stage(layers, plan.inputs, root);

  const auto statuses = Supervisor(plan, root).run();

  // A process must end the way the recording saw it end.
  Arguments problems;
  bool wrongStatus = false;
  Arguments differing;
  for (size_t index = 0; index < plan.processes.size(); ++index) {
    const auto &process = plan.processes[index];
    const int actual = expectedExit(statuses[index]);
    const int expected = expectedExit(process.status);
    if (actual != expected) {
      problems.push_back(absl::StrCat(process.name, " exited with ", actual,
                                      ", recorded ", expected));
      wrongStatus = true;
    }
  }

  // Every result must be the version the recording saw. The recorded mtime
  // is stamped on rather than compared: the replay ran on another clock.
  std::vector<LayerSource> sources;
  for (const auto &expected : plan.results) {
    const auto host = hostPath(root, expected.path);
    if (!fs::exists(fs::symlink_status(host))) {
      problems.push_back("missing " + expected.path);
      differing.push_back(expected.path);
      continue;
    }
    auto observed = observe(host, expected.path);
    observed.mtime = expected.mtime;
    if (!sameVersion(observed, expected)) {
      problems.push_back(absl::StrCat("differs ", expected.path, ": got ",
                                      describe(observed), ", recorded ",
                                      describe(expected)));
      differing.push_back(expected.path);
      continue;
    }
    sources.push_back({expected, host});
  }
  if (!problems.empty()) {
    ReplayFailure failure(plan.name + " did not reproduce the recording:\n  " +
                          absl::StrJoin(problems, "\n  "));
    failure.wrongStatus = wrongStatus;
    failure.differing = std::move(differing);
    throw failure;
  }
  writeLayer(output, sources);
}

void packPlan(const Plan &plan, const std::vector<fs::path> &layerFiles,
              const fs::path &output) {
  const auto layers = openLayers(layerFiles);
  Scratch scratch;
  const auto root = scratch.path / "root";
  stage(layers, plan.inputs, root);
  const auto tree = hostPath(root, plan.output);
  check(fs::exists(fs::symlink_status(tree)),
        "no entry was published at " + plan.output);
  execute({std::string(kNixStore), "--dump", tree.string()}, {}, output);
}
} // namespace splice_ir

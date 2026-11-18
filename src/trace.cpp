#include "trace.h"
#include "nlohmann/json.hpp"
#include "process.h"
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
using Json = nlohmann::json;

// The pid of the guest's init, whose execs and opens belong to the VM.
constexpr int kInitPid = 1;

// Programs whose only job is to run other programs. One of these is opened
// when it leads to an opened program, so that `sh -c make` does not hide
// the make from the caller who asked to open it.
constexpr std::string_view kShellNames = "sh|bash|dash|ksh|zsh";

// The layout of trace.bin, from rewind-trace 0.16: an 8-byte magic and a
// little-endian u32 version, then records. Each record is a u64 step and a
// body that starts with its own u32 length. Byte 4 of the body holds a u16
// kind, and the payload starts after a 20-byte header.
constexpr std::string_view kTraceMagic{"rwtrace\0", 8};
constexpr uint32_t kTraceVersion = 1;
constexpr size_t kStepBytes = 8;
constexpr size_t kLengthBytes = 4;
constexpr size_t kKindOffset = 4;
constexpr size_t kFormatOffset = 16;
constexpr size_t kRecordHeader = 20;

// The record kind that carries read and write records, and the version of
// their encoding.
constexpr uint16_t kIoKind = 13;
constexpr uint32_t kIoFormat = 1;

// The first byte of an encoded I/O record: three bits of kind, and a bit
// that says the thread is the same as the previous record's.
constexpr uint8_t kIoKindMask = 0x07;
constexpr uint8_t kSameThread = 0x08;
enum class IoKind : uint8_t { Write = 1, ReadOpen = 2, WriteOpen = 3, Lost = 4 };

// LEB128: seven bits per byte, low bits first, top bit set on all but the
// last byte.
constexpr uint8_t kVarintMore = 0x80;
constexpr uint8_t kVarintBits = 7;

struct ReadOpen {
  uint64_t step = 0;
  int tid = 0;
  std::string path;
};

struct IoLog {
  std::vector<ReadOpen> reads;
  uint64_t lost = 0;
};

// Where the kernel keeps devices and its own view of processes. Opening
// one of those for writing changes no file of the build.
constexpr std::string_view kDevices = "/dev/";
constexpr std::string_view kProcesses = "/proc/";

template <typename Integer> Integer littleEndian(std::string_view bytes) {
  check(bytes.size() >= sizeof(Integer), "truncated trace");
  Integer value = 0;
  std::memcpy(&value, bytes.data(), sizeof(Integer));
  return value;
}

// A cursor over the payload of one I/O trace record.
class Payload {
public:
  explicit Payload(std::string_view bytes) : bytes(bytes) {}
  bool empty() const { return bytes.empty(); }

  uint8_t byte() {
    check(!bytes.empty(), "truncated read and write record");
    const auto value = static_cast<uint8_t>(bytes.front());
    bytes.remove_prefix(1);
    return value;
  }

  uint64_t varint() {
    uint64_t value = 0;
    for (unsigned shift = 0;; shift += kVarintBits) {
      const uint8_t next = byte();
      if (shift < 64) {
        value |= static_cast<uint64_t>(next & ~kVarintMore) << shift;
      }
      if ((next & kVarintMore) == 0) {
        return value;
      }
    }
  }

  int64_t zigzag() {
    const uint64_t value = varint();
    return static_cast<int64_t>(value >> 1) ^ -static_cast<int64_t>(value & 1);
  }

  std::string_view take(size_t count) {
    check(bytes.size() >= count, "truncated read and write record");
    const auto prefix = bytes.substr(0, count);
    bytes.remove_prefix(count);
    return prefix;
  }

private:
  std::string_view bytes;
};

// Decodes the read-opens from a trace file. Records are encoded against the
// tables the earlier records built, so every record is decoded even though
// only the read-opens and the lost counts are kept.
IoLog readIoLog(const fs::path &file) {
  const std::string bytes = readBytes(file);
  std::string_view rest(bytes);
  check(rest.starts_with(kTraceMagic), "not a Rewind trace: " + file.string());
  rest.remove_prefix(kTraceMagic.size());
  check(littleEndian<uint32_t>(rest) == kTraceVersion,
        "unsupported Rewind trace version");
  rest.remove_prefix(sizeof(uint32_t));

  IoLog log;
  int64_t tid = 0;
  size_t files = 0;
  std::vector<std::string> paths;
  std::string lastPath;

  // A file is defined the first time a record names the next unused id.
  const auto readFile = [&](Payload &payload) {
    if (payload.varint() == files) {
      payload.varint();
      payload.varint();
      ++files;
    }
  };

  // A path is defined as the bytes shared with the path defined before it,
  // followed by a suffix.
  const auto readPath = [&](Payload &payload) -> const std::string & {
    const uint64_t id = payload.varint();
    if (id == paths.size()) {
      const size_t shared = payload.varint();
      const size_t length = payload.varint();
      check(shared <= lastPath.size(), "corrupt path in read and write record");
      lastPath = lastPath.substr(0, shared) + std::string(payload.take(length));
      paths.push_back(lastPath);
    }
    check(id < paths.size(), "undefined path in read and write record");
    return paths[id];
  };

  while (rest.size() >= kStepBytes + kLengthBytes) {
    const auto step = littleEndian<uint64_t>(rest);
    const auto length = littleEndian<uint32_t>(rest.substr(kStepBytes));

    // A trace cut off mid-record ends at its last whole record.
    if (length < kRecordHeader || rest.size() < kStepBytes + length) {
      break;
    }
    const auto record = rest.substr(kStepBytes, length);
    rest.remove_prefix(kStepBytes + length);
    if (littleEndian<uint16_t>(record.substr(kKindOffset)) != kIoKind) {
      continue;
    }
    check(littleEndian<uint32_t>(record.substr(kFormatOffset)) == kIoFormat,
          "unsupported read and write record format");

    Payload payload(record.substr(kRecordHeader));
    while (!payload.empty()) {
      const uint8_t first = payload.byte();
      const auto kind = static_cast<IoKind>(first & kIoKindMask);
      if (kind == IoKind::Lost) {
        log.lost += payload.varint();
        continue;
      }
      if ((first & kSameThread) == 0) {
        tid += payload.zigzag();
      }
      switch (kind) {
      case IoKind::Write:
        readFile(payload);
        payload.zigzag();
        payload.varint();
        break;
      case IoKind::ReadOpen:
        log.reads.push_back({step, static_cast<int>(tid), readPath(payload)});
        break;
      case IoKind::WriteOpen:
        readFile(payload);
        readPath(payload);
        break;
      default:
        throw std::runtime_error("unknown read and write record kind");
      }
    }
  }
  return log;
}

struct Exec {
  uint64_t step = 0;
  std::string filename;
};

struct TraceProcess {
  std::optional<int> parent;
  uint64_t forked = 0;
  std::vector<Exec> execs;
  std::optional<uint64_t> exited;
  int status = 0;
};

struct Events {
  std::map<int, TraceProcess> processes;

  // Matches the names of programs whose processes are driver code.
  std::optional<std::regex> open;

  // Whether a process is driver code on the evidence of what it did, and
  // the images that are not to be, whatever the evidence.
  Granularity granularity = Granularity::Fine;
  std::set<Image> closed;

  // The steps at which each process opened a file of the build for
  // writing or renamed one, in order. The event stream reports both at
  // the step they happened, which the batched records of trace.bin do not.
  std::map<int, std::vector<uint64_t>> writes;

  // The processes that, before their first execve, opened a file of the
  // build for writing and closed it again. A redirection set up for a
  // command stays open across the execve, so such a file was written by a
  // shell builtin.
  std::set<int> wroteBeforeExec;

  // Thread id to the process the thread belongs to.
  std::map<int, int> threads;
  std::optional<int> builder;

  // The process a thread belongs to, which is the thread's own id for the
  // first thread of a process.
  int processOf(int tid) const {
    const auto thread = threads.find(tid);
    return thread == threads.end() ? tid : thread->second;
  }
};

Events readEvents(const fs::path &file) {
  std::ifstream stream(file);
  check(stream.is_open(), "cannot open " + file.string());
  Events events;

  // The step and thread of each write, kept until every thread is known,
  // and for each thread the files it has open for writing.
  std::vector<std::pair<uint64_t, int>> writers;
  std::map<int, std::set<std::string>> openFiles;
  std::vector<std::pair<uint64_t, int>> closedWrites;
  for (std::string line; std::getline(stream, line);) {
    const auto event = Json::parse(line);
    const auto &kind = event.at("kind");
    const std::string type = kind.at("type");
    const int pid = event.at("pid");
    const uint64_t step = event.at("step");
    if (type == "fork") {
      const int child = kind.at("child");
      if (kind.value("thread", false)) {
        events.threads[child] = pid;
        continue;
      }
      // Everything below takes a process id for one process. The guest
      // would have to start more processes than it has ids for one to
      // come round again, and a run that does is refused.
      const auto [created, fresh] = events.processes.try_emplace(child);
      check(fresh, "process id " + std::to_string(child) +
                       " is used twice, which the analysis does not handle");
      created->second.parent = pid;
      created->second.forked = step;
      continue;
    }
    if (type == "exec") {
      events.processes[pid].execs.push_back({step, kind.at("filename")});
      if (!events.builder && pid != kInitPid) {
        events.builder = pid;
      }
      continue;
    }
    if (type == "open" || type == "rename") {
      // Rewind reports the opens that can change a file. Opening a device
      // or the kernel's view of a process changes no file of the build.
      const std::string path = kind.value("path", kind.value("to", ""));
      if (!path.starts_with(kDevices) && !path.starts_with(kProcesses)) {
        const int tid = event.at("tid");
        writers.emplace_back(step, tid);
        if (type == "open") {
          openFiles[tid].insert(path);
        }
      }
      continue;
    }
    if (type == "close") {
      // Rewind reports a close when a file is let go of for good, which
      // for a redirection is when the command exits.
      const int tid = event.at("tid");
      if (openFiles[tid].erase(kind.value("path", "")) != 0) {
        closedWrites.emplace_back(step, tid);
      }
      continue;
    }
    if (type == "exit" && !kind.value("thread", false)) {
      auto &process = events.processes[pid];
      process.exited = step;
      process.status = kind.at("status");
    }
  }
  for (const auto &[step, tid] : writers) {
    events.writes[events.processOf(tid)].push_back(step);
  }
  for (const auto &[step, tid] : closedWrites) {
    const int pid = events.processOf(tid);
    const auto process = events.processes.find(pid);
    if (process != events.processes.end() && !process->second.execs.empty() &&
        step < process->second.execs.front().step) {
      events.wroteBeforeExec.insert(pid);
    }
  }
  return events;
}

std::string programName(const Exec &exec) {
  return fs::path(exec.filename).filename().string();
}

// Decides which execve calls keep a process in the driver.
//
// An execve is opened when the program's name matches the pattern. An
// execve of a shell is also opened when the shell goes on to perform an
// opened execve or has one below it. That is what makes opening `make`
// useful under recursive make: the shell that runs `cd po && make`, or
// that replaces itself with `make all-recursive`, has to be driver code
// for the inner make to be reached. A shell that only runs a compiler has
// nothing opened after or below it and stays a single, replayable cut.
// Programs other than shells are never opened this way: a configure
// script runs make to test it and is still one unit of work.
//
// With fine cuts an execve is also opened when the image it started only
// ran other commands. The evidence is in the trace: the image went on to
// fork a command or to become one, and neither the image nor a subshell of
// it opened a file of the build for writing. Such a process has no effect
// of its own to lose. What it did is what its children did, and each child
// is replayed with the arguments, environment and descriptors the process
// gave it. A process that does write, as configure and cargo do, stays a
// cut, because whatever it wrote would otherwise be recorded bytes.
class Opened {
public:
  explicit Opened(const Events &events) : events(events) {
    for (const auto &[pid, process] : events.processes) {
      if (process.parent) {
        children[*process.parent].push_back(pid);
      }
    }
  }

  // Whether the process's execve number `index` is opened: by name, or
  // on the evidence of the trace unless the image has been closed.
  bool exec(int pid, size_t index) {
    const auto key = std::make_pair(pid, index);
    if (const auto known = execs.find(key); known != execs.end()) {
      return known->second;
    }
    bool opened = named(pid, index);
    const auto &process = events.processes.at(pid);
    if (!opened && events.granularity == Granularity::Fine &&
        !events.closed.contains({pid, process.execs[index].step}) &&
        onlyRunsCommands(pid, index)) {
      opened = true;
      evident.insert(key);
    }
    return execs[key] = opened;
  }

  // The nearest image above a process's execve number `index` that is
  // opened only on the evidence of the trace: an earlier program of the
  // same process, or the program an ancestor was running when it forked
  // the next process down.
  std::optional<Image> opener(int pid, size_t index) {
    const auto found = [&](int process, size_t exec) -> std::optional<Image> {
      this->exec(process, exec);
      if (!evident.contains({process, exec})) {
        return std::nullopt;
      }
      return Image{process, events.processes.at(process).execs[exec].step};
    };
    const int first = pid == *events.builder ? 1 : 0;
    for (int earlier = static_cast<int>(index) - 1; earlier >= first; --earlier) {
      if (const auto image = found(pid, earlier)) {
        return image;
      }
    }
    for (int child = pid; child != *events.builder;) {
      const auto &process = events.processes.at(child);
      if (!process.parent || !events.processes.contains(*process.parent)) {
        return std::nullopt;
      }
      const int parent = *process.parent;
      const auto &execs = events.processes.at(parent).execs;

      // The program the parent was running at the fork, if it had called
      // execve by then. A parent that had not is a subshell.
      for (int running = static_cast<int>(execs.size()) - 1; running >= 0;
           --running) {
        if (execs[running].step > process.forked) {
          continue;
        }
        const int lowest = parent == *events.builder ? 1 : 0;
        if (running < lowest) {
          break;
        }
        if (const auto image = found(parent, running)) {
          return image;
        }
      }
      child = parent;
    }
    return std::nullopt;
  }

private:
  // Whether the process's execve number `index` is opened by name: the
  // program matches the pattern, or is a shell that goes on to become a
  // matching program or has one below it. What the trace shows of the
  // image plays no part, so closing an image cannot be undone here.
  bool named(int pid, size_t index) {
    const auto key = std::make_pair(pid, index);
    if (const auto known = names.find(key); known != names.end()) {
      return known->second;
    }
    const auto &process = events.processes.at(pid);
    const auto name = programName(process.execs[index]);
    bool opened = events.open && std::regex_match(name, *events.open);

    // Remember the answer for anything that is not a shell before looking
    // further, since only a shell can be opened by what it runs.
    static const std::regex shells{std::string(kShellNames)};
    if (opened || !events.open || !std::regex_match(name, shells)) {
      return names[key] = opened;
    }

    // Look below the image this execve started: the children forked before
    // the process's next execve.
    const auto [from, until] = span(pid, index);
    opened = anyChildBetween(pid, from, until);

    // A shell that execs its last command becomes that command.
    if (!opened && index + 1 < process.execs.size()) {
      opened = named(pid, index + 1);
    }
    return names[key] = opened;
  }

  // The steps between which the image a process's execve number `index`
  // started was running.
  std::pair<uint64_t, uint64_t> span(int pid, size_t index) const {
    const auto &execs = events.processes.at(pid).execs;
    return {execs[index].step, index + 1 < execs.size()
                                   ? execs[index + 1].step
                                   : std::numeric_limits<uint64_t>::max()};
  }

  // Whether the image ran other commands and wrote no file itself.
  bool onlyRunsCommands(int pid, size_t index) const {
    const auto [from, until] = span(pid, index);
    const bool becomesCommand =
        index + 1 < events.processes.at(pid).execs.size();
    return (becomesCommand || startsCommand(pid, from, until)) &&
           !wroteBetween(pid, from, until);
  }

  // Whether a process, between two steps, forked a child that called
  // execve, directly or through subshells.
  bool startsCommand(int pid, uint64_t from, uint64_t until) const {
    const auto known = children.find(pid);
    if (known == children.end()) {
      return false;
    }
    for (const int child : known->second) {
      const auto &process = events.processes.at(child);
      if (process.forked < from || process.forked >= until) {
        continue;
      }
      if (!process.execs.empty() ||
          startsCommand(child, 0, std::numeric_limits<uint64_t>::max())) {
        return true;
      }
    }
    return false;
  }

  // Whether a process, between two steps, opened a file for writing, or a
  // subshell of it did. What a child opens before the child's own execve
  // and keeps open is a redirection set up for that command and belongs
  // to the command.
  bool wroteBetween(int pid, uint64_t from, uint64_t until) const {
    if (const auto steps = events.writes.find(pid);
        steps != events.writes.end()) {
      const auto first = std::ranges::lower_bound(steps->second, from);
      if (first != steps->second.end() && *first < until) {
        return true;
      }
    }
    const auto known = children.find(pid);
    if (known == children.end()) {
      return false;
    }
    for (const int child : known->second) {
      const auto &process = events.processes.at(child);
      if (process.forked < from || process.forked >= until) {
        continue;
      }

      // A child that became a command counts only for what a builtin of
      // it wrote before that.
      if (!process.execs.empty()) {
        if (events.wroteBeforeExec.contains(child)) {
          return true;
        }
        continue;
      }
      if (wroteBetween(child, 0, std::numeric_limits<uint64_t>::max())) {
        return true;
      }
    }
    return false;
  }

  // Whether an execve opened by name happens in the process or below it.
  bool contains(int pid) {
    const auto &process = events.processes.at(pid);
    for (size_t index = 0; index < process.execs.size(); ++index) {
      if (named(pid, index)) {
        return true;
      }
    }

    // A subshell has no execve of its own but can still lead to one.
    const uint64_t firstExec = process.execs.empty()
                                   ? std::numeric_limits<uint64_t>::max()
                                   : process.execs.front().step;
    return anyChildBetween(pid, 0, firstExec);
  }

  bool anyChildBetween(int pid, uint64_t from, uint64_t until) {
    for (const int child : children[pid]) {
      const uint64_t forked = events.processes.at(child).forked;
      if (forked >= from && forked < until && contains(child)) {
        return true;
      }
    }
    return false;
  }

  const Events &events;
  std::map<int, std::vector<int>> children;
  std::map<std::pair<int, size_t>, bool> execs;
  std::map<std::pair<int, size_t>, bool> names;

  // The execve calls opened on the evidence of the trace alone.
  std::set<std::pair<int, size_t>> evident;
};

// The execve at which a process stopped being driver code, if the process
// ever did. The builder's first execve made the process the builder, so the
// builder can only leave the driver at its second. An opened execve keeps
// the process in the driver.
const Exec *cutExec(const Events &events, Opened &opened, int pid) {
  const auto &execs = events.processes.at(pid).execs;
  for (size_t index = pid == *events.builder ? 1 : 0; index < execs.size();
       ++index) {
    if (!opened.exec(pid, index)) {
      return &execs[index];
    }
  }
  return nullptr;
}

// The cut that an event of `pid` at `step` belongs to, or nothing for
// driver code and for processes the builder did not start.
std::optional<int> owner(const Events &events, Opened &opened, int pid,
                         uint64_t step) {
  std::optional<int> candidate;

  // A child's opens before its own execve are the shell setting up the
  // command's redirections, so they belong to the command. The builder's
  // activity before the builder replaced itself is driver code.
  if (const auto *exec = cutExec(events, opened, pid)) {
    if (pid != *events.builder || step >= exec->step) {
      candidate = pid;
    }
  }

  // An ancestor that had already called execve when the next process down
  // was forked contains everything below.
  for (int child = pid; child != *events.builder;) {
    const auto &process = events.processes.at(child);
    if (!process.parent || !events.processes.contains(*process.parent)) {
      return std::nullopt;
    }
    const int parent = *process.parent;
    const auto *exec = cutExec(events, opened, parent);
    if (exec != nullptr && exec->step < process.forked) {
      candidate = parent;
    }
    child = parent;
  }
  return candidate;
}
} // namespace

Analysis analyze(const fs::path &eventFile, const fs::path &traceFile,
                 const std::string &open, Granularity granularity,
                 const std::set<Image> &closed) {
  auto events = readEvents(eventFile);
  events.granularity = granularity;
  events.closed = closed;
  if (!open.empty()) {
    events.open = std::regex(open);
  }

  check(events.builder.has_value(), "the run has no builder process");
  const auto &builder = events.processes.at(*events.builder);
  check(builder.exited.has_value(), "the builder did not exit");

  Opened opened(events);
  Analysis analysis;
  analysis.initialStep = builder.execs.front().step;
  analysis.finalStep = *builder.exited;
  analysis.builder = builder.execs.front().filename;

  // Start a cut for each process that leaves the driver with nothing above
  // the process already cut.
  std::map<int, Cut> cuts;
  for (const auto &[pid, process] : events.processes) {
    const auto *exec = cutExec(events, opened, pid);
    if (exec == nullptr || owner(events, opened, pid, exec->step) != pid) {
      continue;
    }
    Cut cut;
    cut.pid = pid;
    cut.step = exec->step;
    cut.end = process.exited.value_or(analysis.finalStep);
    cut.status = process.status;
    cut.filename = exec->filename;
    cut.name = programName(*exec);
    cut.opener = opened.opener(pid, exec - process.execs.data());
    cuts.emplace(pid, std::move(cut));
  }

  // Extend each cut over its descendants and collect the programs they ran.
  std::map<int, std::set<std::string>> seen;
  const auto addRead = [&](int cut, const std::string &path) {
    if (seen[cut].insert(path).second) {
      cuts.at(cut).reads.push_back(path);
    }
  };
  for (const auto &[pid, process] : events.processes) {
    const uint64_t exited = process.exited.value_or(analysis.finalStep);
    if (const auto cut = owner(events, opened, pid, exited)) {
      cuts.at(*cut).end = std::max(cuts.at(*cut).end, exited);
    }
    for (const auto &exec : process.execs) {
      if (const auto cut = owner(events, opened, pid, exec.step)) {
        addRead(*cut, exec.filename);
      }
    }
  }

  // Attribute each read-open to the cut of the thread's process.
  const auto log = readIoLog(traceFile);
  analysis.lost = log.lost;
  for (const auto &read : log.reads) {
    const int pid = events.processOf(read.tid);
    if (!events.processes.contains(pid)) {
      continue;
    }
    if (const auto cut = owner(events, opened, pid, read.step)) {
      addRead(*cut, read.path);
    }
  }

  for (auto &[pid, cut] : cuts) {
    analysis.cuts.push_back(std::move(cut));
  }
  std::ranges::sort(analysis.cuts, {}, &Cut::step);
  return analysis;
}
} // namespace splice_ir

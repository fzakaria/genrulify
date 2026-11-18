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

  // Thread id to the process the thread belongs to.
  std::map<int, int> threads;
  std::optional<int> builder;
};

Events readEvents(const fs::path &file) {
  std::ifstream stream(file);
  check(stream.is_open(), "cannot open " + file.string());
  Events events;
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
      auto &process = events.processes[child];
      process.parent = pid;
      process.forked = step;
      continue;
    }
    if (type == "exec") {
      events.processes[pid].execs.push_back({step, kind.at("filename")});
      if (!events.builder && pid != kInitPid) {
        events.builder = pid;
      }
      continue;
    }
    if (type == "exit" && !kind.value("thread", false)) {
      auto &process = events.processes[pid];
      process.exited = step;
      process.status = kind.at("status");
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
class Opened {
public:
  explicit Opened(const Events &events) : events(events) {
    for (const auto &[pid, process] : events.processes) {
      if (process.parent) {
        children[*process.parent].push_back(pid);
      }
    }
  }

  // Whether the process's execve number `index` is opened.
  bool exec(int pid, size_t index) {
    const auto key = std::make_pair(pid, index);
    if (const auto known = execs.find(key); known != execs.end()) {
      return known->second;
    }
    const auto &process = events.processes.at(pid);
    const auto name = programName(process.execs[index]);
    bool opened = events.open && std::regex_match(name, *events.open);

    // Remember the answer for anything that is not a shell before looking
    // further, since only a shell can be opened by what it runs.
    static const std::regex shells{std::string(kShellNames)};
    if (opened || !events.open || !std::regex_match(name, shells)) {
      return execs[key] = opened;
    }

    // Look below the image this execve started: the children forked before
    // the process's next execve.
    const uint64_t from = process.execs[index].step;
    const uint64_t until = index + 1 < process.execs.size()
                               ? process.execs[index + 1].step
                               : std::numeric_limits<uint64_t>::max();
    opened = anyChildBetween(pid, from, until);

    // A shell that execs its last command becomes that command.
    if (!opened && index + 1 < process.execs.size()) {
      opened = exec(pid, index + 1);
    }
    return execs[key] = opened;
  }

private:
  // Whether an opened execve happens in the process or below it.
  bool contains(int pid) {
    const auto &process = events.processes.at(pid);
    for (size_t index = 0; index < process.execs.size(); ++index) {
      if (exec(pid, index)) {
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
                 const std::string &open) {
  auto events = readEvents(eventFile);
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
    const auto thread = events.threads.find(read.tid);
    const int pid = thread == events.threads.end() ? read.tid : thread->second;
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

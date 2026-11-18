#pragma once

#include "nlohmann/json.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace splice_ir {
// The kinds of filesystem object a build output can contain. Rewind leaves
// devices, sockets and FIFOs out of its archives, and a NAR cannot hold them.
enum class EntryKind { File, Directory, Symlink };

// What one path held at one step of the recording.
struct Entry {
  std::string path;
  EntryKind kind = EntryKind::File;

  // Permission bits, including setuid, setgid and sticky.
  uint32_t mode = 0;

  // Seconds, as the guest clock had them.
  int64_t mtime = 0;

  // The SHA-256 of a regular file's bytes in lowercase hex, the target of a
  // symlink, and empty for a directory.
  std::string content;
};

// Every entry below the writable roots at one step, by guest path.
using State = std::map<std::string, Entry>;

// Two entries are the same version when a process could not tell them apart
// by reading them. A directory's mtime moves whenever a child is added, so
// directories compare without their mtime.
bool sameVersion(const Entry &left, const Entry &right);

// The short names the recording and the IR use for each kind.
std::string_view kindName(EntryKind kind);
EntryKind kindFromName(std::string_view name);

void to_json(nlohmann::json &json, const Entry &entry);
void from_json(const nlohmann::json &json, Entry &entry);

// Whether `path` is `root` or lies below `root`.
bool isBelow(std::string_view path, std::string_view root);
} // namespace splice_ir

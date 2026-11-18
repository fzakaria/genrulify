#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace splice_ir {
// What a replayed group's processes did with paths, read from a trace of
// the replay itself. Rewind's log has opens. A trace of the replay also has
// every stat, access, readlink and directory listing, which is what a group
// that fails with narrow inputs was missing.
struct Accessed {
  // Paths a system call named and found.
  std::set<std::string> paths;

  // Directories opened for listing. Everything directly inside one of them
  // was visible to the group.
  std::set<std::string> listed;
};

// Reads every file strace wrote under `directory` with one file per traced
// process. A path is taken from a successful call only: a lookup that
// failed names something the group did not have.
Accessed readAccesses(const std::filesystem::path &directory);
} // namespace splice_ir

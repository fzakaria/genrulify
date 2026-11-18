#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace splice_ir {
using Arguments = std::vector<std::string>;

// The holder owns a private directory for redirections, staged trees and
// archives, and removes the directory even when staged entries are read-only.
class Scratch {
public:
  Scratch();
  ~Scratch();
  Scratch(const Scratch &) = delete;
  Scratch &operator=(const Scratch &) = delete;
  std::filesystem::path path;
};

// Runs a program found on PATH and returns the program's exit status. An
// empty redirection path leaves that stream attached to the caller's.
int run(const Arguments &args, const std::filesystem::path &input = {},
        const std::filesystem::path &output = {},
        const std::filesystem::path &error = {});

// Runs a program and throws unless the program exits with status zero.
void execute(const Arguments &args, const std::filesystem::path &input = {},
             const std::filesystem::path &output = {});

// Runs a program and returns everything the program wrote to standard output.
std::string capture(const Arguments &args);

std::string readBytes(const std::filesystem::path &path);
void writeBytes(const std::filesystem::path &path, std::string_view bytes);

// The SHA-256 of a file's bytes in lowercase hex.
std::string digest(const std::filesystem::path &path);

// Throws a runtime error carrying `message` unless `condition` holds.
void check(bool condition, std::string_view message);

// Calls `body(i)` for every i below `count` on a small pool of threads and
// rethrows the first exception a call raised.
void parallelFor(size_t count, const std::function<void(size_t)> &body);

// Removes a tree that may contain directories without write permission.
void removeTree(const std::filesystem::path &path);
} // namespace splice_ir

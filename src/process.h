#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace splice_ir {
using Arguments = std::vector<std::string>;

// The holder owns a private directory for command redirections and NAR staging.
class Scratch {
public:
  Scratch();
  ~Scratch();
  Scratch(const Scratch &) = delete;
  Scratch &operator=(const Scratch &) = delete;
  std::filesystem::path path;
};

void execute(const Arguments &args, const std::filesystem::path &input = {},
             const std::filesystem::path &output = {});
std::string readBytes(const std::filesystem::path &path);
std::string fileHash(const std::filesystem::path &path);
void check(bool condition, std::string_view message);
} // namespace splice_ir

#include "process.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Program.h"
#include <fstream>

namespace splice_ir {
void check(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

Scratch::Scratch() {
  llvm::SmallString<128> directory;
  if (auto ec = llvm::sys::fs::createUniqueDirectory("nixsplice", directory)) {
    throw std::system_error(ec);
  }
  path = directory.str().str();
}
Scratch::~Scratch() {
  std::error_code ignored;
  std::filesystem::remove_all(path, ignored);
}

std::string readBytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  check(input.is_open(), "cannot read " + path.string());
  return std::string(std::istreambuf_iterator<char>(input), {});
}

void execute(const Arguments &args, const std::filesystem::path &input,
             const std::filesystem::path &output) {
  const auto program = llvm::sys::findProgramByName(args.at(0));
  check(static_cast<bool>(program), "cannot locate " + args.front());
  std::vector<llvm::StringRef> argv(args.begin(), args.end());
  std::array<std::optional<llvm::StringRef>, 3> redirect{};
  if (!input.empty()) {
    redirect[0] = input.native();
  }
  if (!output.empty()) {
    redirect[1] = output.native();
  }
  const int status =
      llvm::sys::ExecuteAndWait(*program, argv, std::nullopt, redirect);
  check(status == 0,
        args.front() + " exited with status " + std::to_string(status));
}
} // namespace splice_ir

#include "process.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/SHA256.h"
#include <atomic>
#include <fstream>
#include <mutex>
#include <thread>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
// Rewind replays a run to answer each query, so queries are CPU-bound and a
// pool the size of the machine keeps every core busy.
constexpr unsigned kFallbackThreads = 4;

// The bytes read per call while hashing a file.
constexpr size_t kHashChunk = 64 * 1024;

constexpr std::string_view kScratchPrefix = "genrulify";
} // namespace

void check(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

Scratch::Scratch() {
  llvm::SmallString<128> directory;
  if (auto ec = llvm::sys::fs::createUniqueDirectory(kScratchPrefix, directory)) {
    throw std::system_error(ec);
  }
  path = directory.str().str();
}

Scratch::~Scratch() { removeTree(path); }

void removeTree(const fs::path &path) {
  std::error_code ignored;

  // A staged directory keeps its recorded mode, which may deny the owner
  // write or search permission. Restore both before unlinking children.
  if (fs::is_directory(fs::symlink_status(path, ignored))) {
    fs::permissions(path, fs::perms::owner_all, fs::perm_options::add, ignored);
    for (fs::recursive_directory_iterator it(path, ignored), end; it != end;
         it.increment(ignored)) {
      if (it->is_directory(ignored) && !it->is_symlink(ignored)) {
        fs::permissions(it->path(), fs::perms::owner_all,
                        fs::perm_options::add, ignored);
      }
    }
  }
  fs::remove_all(path, ignored);
}

std::string readBytes(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  check(input.is_open(), "cannot read " + path.string());
  return std::string(std::istreambuf_iterator<char>(input), {});
}

void writeBytes(const fs::path &path, std::string_view bytes) {
  std::ofstream file;
  file.exceptions(std::ios::failbit | std::ios::badbit);
  file.open(path, std::ios::binary | std::ios::trunc);
  file << bytes;
  file.close();
}

std::string digest(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  check(file.is_open(), "cannot open " + path.string());
  llvm::SHA256 hash;
  std::vector<char> bytes(kHashChunk);
  while (file.read(bytes.data(), bytes.size()) || file.gcount() != 0) {
    hash.update(llvm::StringRef(bytes.data(), file.gcount()));
  }
  check(file.eof(), "cannot read " + path.string());
  return llvm::toHex(hash.final(), /*LowerCase=*/true);
}

std::string toHex(std::string_view bytes) {
  return llvm::toHex(llvm::StringRef(bytes), /*LowerCase=*/true);
}

std::string fromHex(std::string_view hex) {
  std::string bytes;
  check(llvm::tryGetFromHex(llvm::StringRef(hex), bytes), "invalid hex string");
  return bytes;
}

int run(const Arguments &args, const fs::path &input, const fs::path &output,
        const fs::path &error) {
  const auto program = llvm::sys::findProgramByName(args.at(0));
  check(static_cast<bool>(program), "cannot locate " + args.front());
  std::vector<llvm::StringRef> argv(args.begin(), args.end());
  std::array<std::optional<llvm::StringRef>, 3> redirect{};
  if (!input.empty()) {
    redirect[0] = input.native();
  }
  if (!output.empty()) {
    // LLVM redirects overwrite in place. Discard bytes from an earlier, longer
    // command.
    std::ofstream fresh(output, std::ios::binary | std::ios::trunc);
    check(fresh.good(), "cannot open " + output.string());
    redirect[1] = output.native();
  }
  if (!error.empty()) {
    redirect[2] = error.native();
  }
  return llvm::sys::ExecuteAndWait(*program, argv, std::nullopt, redirect);
}

void execute(const Arguments &args, const fs::path &input,
             const fs::path &output) {
  const int status = run(args, input, output);
  check(status == 0,
        args.front() + " exited with status " + std::to_string(status));
}

std::string capture(const Arguments &args) {
  llvm::SmallString<128> file;
  if (auto ec = llvm::sys::fs::createTemporaryFile(kScratchPrefix, "out", file)) {
    throw std::system_error(ec);
  }
  const fs::path output = file.str().str();
  try {
    execute(args, {}, output);
    auto bytes = readBytes(output);
    fs::remove(output);
    return bytes;
  } catch (...) {
    fs::remove(output);
    throw;
  }
}

void parallelFor(size_t count, const std::function<void(size_t)> &body) {
  const unsigned hardware = std::thread::hardware_concurrency();
  const size_t workers =
      std::min<size_t>(count, hardware == 0 ? kFallbackThreads : hardware);
  std::atomic<size_t> next = 0;
  std::mutex mutex;
  std::exception_ptr failure;

  // Each worker claims the next index until none remain or a call failed.
  const auto work = [&] {
    for (size_t index = next++; index < count; index = next++) {
      try {
        body(index);
      } catch (...) {
        std::scoped_lock lock(mutex);
        if (!failure) {
          failure = std::current_exception();
        }
        next = count;
        return;
      }
    }
  };
  {
    std::vector<std::jthread> threads;
    for (size_t worker = 0; worker < workers; ++worker) {
      threads.emplace_back(work);
    }
  }
  if (failure) {
    std::rethrow_exception(failure);
  }
}
} // namespace splice_ir

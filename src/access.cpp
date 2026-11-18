#include "access.h"
#include <fstream>
#include <regex>
#include <vector>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
// The marker of a call that failed, and the open flag of a listing.
constexpr std::string_view kFailed = ") = -1 ";
constexpr std::string_view kDirectoryFlag = "O_DIRECTORY";

constexpr int kOctalBase = 8;
constexpr size_t kOctalDigits = 3;

// Undoes the C escapes strace applies to a string argument.
std::string unescape(std::string_view text) {
  std::string plain;
  for (size_t index = 0; index < text.size(); ++index) {
    if (text[index] != '\\' || index + 1 == text.size()) {
      plain.push_back(text[index]);
      continue;
    }
    const char next = text[++index];
    if (next >= '0' && next <= '7') {
      // Up to three octal digits name one byte.
      size_t length = 1;
      while (length < kOctalDigits && index + length < text.size() &&
             text[index + length] >= '0' && text[index + length] <= '7') {
        ++length;
      }
      plain.push_back(static_cast<char>(std::stoi(
          std::string(text.substr(index, length)), nullptr, kOctalBase)));
      index += length - 1;
      continue;
    }
    plain.push_back(next == 'n' ? '\n' : next == 't' ? '\t' : next);
  }
  return plain;
}

// Lexically normalizes a path and drops a trailing separator.
std::string normalized(const fs::path &path) {
  auto normal = path.lexically_normal().string();
  if (normal.size() > 1 && normal.ends_with('/')) {
    normal.pop_back();
  }
  return normal;
}

// Reads one process's trace. The process's working directory is not
// traced as such. It is learned from the calls that reveal it and used to
// resolve the older calls that name a path with no directory argument.
class ProcessTrace {
public:
  // The absolute path a line's call named, or empty when the line names
  // none or names one that cannot be resolved yet.
  std::string namedPath(const std::string &line) {
    // A call with a directory argument: strace -y prints the directory's
    // path after the descriptor, and the working directory after AT_FDCWD.
    static const std::regex withDirectory(
        R"re((AT_FDCWD|\d+)<([^>]*)>, "((?:[^"\\]|\\.)*)")re");

    // A call whose first argument is the path.
    static const std::regex leading(R"re(^\w+\("((?:[^"\\]|\\.)*)")re");

    // fchdir names the new working directory by descriptor.
    static const std::regex changeByDescriptor(R"re(^fchdir\(\d+<([^>]*)>)re");

    std::smatch match;
    if (std::regex_search(line, match, changeByDescriptor)) {
      cwd = match[1].str();
      return {};
    }
    if (std::regex_search(line, match, withDirectory)) {
      if (match[1] == "AT_FDCWD") {
        cwd = match[2].str();
      }
      const fs::path named = unescape(match[3].str());
      return normalized(named.is_absolute() ? named
                                            : fs::path(match[2].str()) / named);
    }
    if (!std::regex_search(line, match, leading)) {
      return {};
    }
    fs::path path = unescape(match[1].str());
    if (!path.is_absolute()) {
      if (cwd.empty()) {
        return {};
      }
      path = fs::path(cwd) / path;
    }
    const auto resolved = normalized(path);
    if (line.starts_with("chdir(")) {
      cwd = resolved;
    }
    return resolved;
  }

private:
  // The working directory as far as the trace has shown it.
  std::string cwd;
};
} // namespace

Accessed readAccesses(const fs::path &directory) {
  Accessed accessed;
  for (const auto &file : fs::directory_iterator(directory)) {
    std::ifstream stream(file.path());
    ProcessTrace process;
    for (std::string line; std::getline(stream, line);) {
      if (line.find(kFailed) != std::string::npos) {
        continue;
      }
      const auto path = process.namedPath(line);
      if (path.empty()) {
        continue;
      }
      const bool listing = line.starts_with("open") &&
                           line.find(kDirectoryFlag) != std::string::npos;
      (listing ? accessed.listed : accessed.paths).insert(path);
    }
  }
  return accessed;
}
} // namespace splice_ir

#include "entry.h"
#include <stdexcept>

namespace splice_ir {
namespace {
constexpr std::string_view kFileName = "file";
constexpr std::string_view kDirectoryName = "dir";
constexpr std::string_view kSymlinkName = "link";
} // namespace

bool sameVersion(const Entry &left, const Entry &right) {
  if (left.kind != right.kind || left.mode != right.mode ||
      left.content != right.content) {
    return false;
  }
  return left.kind == EntryKind::Directory || left.mtime == right.mtime;
}

std::string_view kindName(EntryKind kind) {
  switch (kind) {
  case EntryKind::File:
    return kFileName;
  case EntryKind::Directory:
    return kDirectoryName;
  case EntryKind::Symlink:
    return kSymlinkName;
  }
  throw std::logic_error("unknown entry kind");
}

EntryKind kindFromName(std::string_view name) {
  if (name == kFileName) {
    return EntryKind::File;
  }
  if (name == kDirectoryName) {
    return EntryKind::Directory;
  }
  if (name == kSymlinkName) {
    return EntryKind::Symlink;
  }
  throw std::runtime_error("unknown entry kind: " + std::string(name));
}

void to_json(nlohmann::json &json, const Entry &entry) {
  json = {{"path", entry.path},
          {"kind", kindName(entry.kind)},
          {"mode", entry.mode},
          {"mtime", entry.mtime},
          {"content", entry.content}};
}

void from_json(const nlohmann::json &json, Entry &entry) {
  entry.path = json.at("path");
  entry.kind = kindFromName(json.at("kind").get<std::string>());
  entry.mode = json.at("mode");
  entry.mtime = json.at("mtime");
  entry.content = json.at("content");
}

bool isBelow(std::string_view path, std::string_view root) {
  if (!path.starts_with(root)) {
    return false;
  }
  return path.size() == root.size() || path[root.size()] == '/';
}
} // namespace splice_ir

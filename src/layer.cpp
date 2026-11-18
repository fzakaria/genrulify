#include "layer.h"
#include "process.h"
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
using Json = nlohmann::json;

constexpr std::string_view kMagic = "genrulify layer 1\n";

// The permission, setuid, setgid and sticky bits of a mode.
constexpr mode_t kModeBits = 07777;

// The bytes copied per call while moving file contents.
constexpr size_t kCopyChunk = 64 * 1024;

// Sets a path's mtime without following a symlink. Access time is given the
// same value so that a layer's content never depends on the wall clock.
void setTime(const fs::path &path, int64_t mtime) {
  const timespec times[2] = {{mtime, 0}, {mtime, 0}};
  check(utimensat(AT_FDCWD, path.c_str(), times, AT_SYMLINK_NOFOLLOW) == 0,
        "cannot set the time of " + path.string());
}

void copyBytes(std::istream &input, std::ostream &output, uint64_t count) {
  std::vector<char> buffer(kCopyChunk);
  while (count != 0) {
    const auto chunk = std::min<uint64_t>(count, buffer.size());
    input.read(buffer.data(), chunk);
    check(static_cast<uint64_t>(input.gcount()) == chunk, "truncated layer");
    output.write(buffer.data(), chunk);
    count -= chunk;
  }
}
} // namespace

Layer::Layer(const fs::path &file) : file(file), stem(file.stem().string()) {
  std::ifstream input(file, std::ios::binary);
  check(input.is_open(), "cannot open layer " + file.string());

  // Read the magic line and the length of the index.
  std::string magic(kMagic.size(), '\0');
  input.read(magic.data(), magic.size());
  check(input.good() && magic == kMagic, "not a layer: " + file.string());
  std::string length;
  check(static_cast<bool>(std::getline(input, length)), "truncated layer");
  std::string text(std::stoull(length), '\0');
  input.read(text.data(), text.size());
  check(input.good(), "truncated layer index");

  // File bytes follow the index in index order.
  uint64_t offset = static_cast<uint64_t>(input.tellg());
  for (const auto &item : Json::parse(text)) {
    const Entry entry = item;
    index.emplace(entry.path, entry);
    if (entry.kind != EntryKind::File) {
      continue;
    }
    const uint64_t size = item.at("size");
    extents[entry.path] = {offset, size};
    offset += size;
  }
}

void Layer::materialize(const Entry &entry, const fs::path &destination) const {
  switch (entry.kind) {
  case EntryKind::Directory:
    fs::create_directory(destination);
    fs::permissions(destination, fs::perms::owner_all);
    return;
  case EntryKind::Symlink:
    fs::create_symlink(entry.content, destination);
    setTime(destination, entry.mtime);
    return;
  case EntryKind::File:
    break;
  }

  // Copy the file's extent out of the layer, then apply mode and mtime.
  const auto [offset, size] = extents.at(entry.path);
  std::ifstream input(file, std::ios::binary);
  input.seekg(offset);
  std::ofstream output;
  output.exceptions(std::ios::failbit | std::ios::badbit);
  output.open(destination, std::ios::binary | std::ios::trunc);
  copyBytes(input, output, size);
  output.close();
  check(chmod(destination.c_str(), entry.mode) == 0,
        "cannot set the mode of " + destination.string());
  setTime(destination, entry.mtime);
}

void sealDirectory(const Entry &entry, const fs::path &directory) {
  check(chmod(directory.c_str(), entry.mode) == 0,
        "cannot set the mode of " + directory.string());
  setTime(directory, entry.mtime);
}

void writeLayer(const fs::path &file, const std::vector<LayerSource> &sources) {
  // Build the index first: the index records each file's size, and the
  // bytes are appended in the same order.
  Json index = Json::array();
  for (const auto &source : sources) {
    Json item = source.entry;
    if (source.entry.kind == EntryKind::File) {
      item["size"] = fs::file_size(source.bytes);
    }
    index.push_back(std::move(item));
  }
  const std::string text = index.dump();

  std::ofstream output;
  output.exceptions(std::ios::failbit | std::ios::badbit);
  output.open(file, std::ios::binary | std::ios::trunc);
  output << kMagic << text.size() << '\n' << text;
  for (const auto &source : sources) {
    if (source.entry.kind != EntryKind::File) {
      continue;
    }
    std::ifstream input(source.bytes, std::ios::binary);
    check(input.is_open(), "cannot read " + source.bytes.string());
    copyBytes(input, output, fs::file_size(source.bytes));
  }
  output.close();
}

Entry observe(const fs::path &host, const std::string &guestPath) {
  struct stat status {};
  check(lstat(host.c_str(), &status) == 0, "cannot stat " + host.string());
  Entry entry;
  entry.path = guestPath;
  entry.mode = status.st_mode & kModeBits;
  entry.mtime = status.st_mtim.tv_sec;
  if (S_ISDIR(status.st_mode)) {
    entry.kind = EntryKind::Directory;
    return entry;
  }
  if (S_ISLNK(status.st_mode)) {
    entry.kind = EntryKind::Symlink;

    // A symlink's permission bits are not settable on Linux and carry no
    // meaning, so every symlink has mode zero.
    entry.mode = 0;
    entry.content = fs::read_symlink(host).string();
    return entry;
  }
  check(S_ISREG(status.st_mode),
        "not a file, directory or symlink: " + host.string());
  entry.kind = EntryKind::File;
  entry.content = digest(host);
  return entry;
}
} // namespace splice_ir

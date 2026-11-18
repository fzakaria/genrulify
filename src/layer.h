#pragma once

#include "entry.h"
#include <filesystem>
#include <vector>

namespace splice_ir {
// A layer is the artifact one build action hands to the next: the entries
// the action wrote, each with its recorded metadata and, for a regular
// file, its bytes. A NAR cannot carry a full mode and a tar needs a second
// program to read, so a layer is a JSON index followed by the file bytes
// in index order.
//
// Layout: the magic line, the index length as decimal digits and a
// newline, the JSON index, then the bytes of every regular file.
class Layer {
public:
  explicit Layer(const std::filesystem::path &file);

  // The layer's file name without its extension. Build actions refer to
  // the layers they consume by this name.
  const std::string &name() const { return stem; }

  const State &entries() const { return index; }

  // Creates the entry at `destination` with the recorded mode and mtime.
  // A directory is created with owner access so that children can follow;
  // call sealDirectory once they have.
  void materialize(const Entry &entry,
                   const std::filesystem::path &destination) const;

private:
  std::filesystem::path file;
  std::string stem;
  State index;

  // Where each regular file's bytes start in the layer, and their length.
  std::map<std::string, std::pair<uint64_t, uint64_t>> extents;
};

// An entry to store in a layer and, for a regular file, where to read the
// bytes from.
struct LayerSource {
  Entry entry;
  std::filesystem::path bytes;
};

void writeLayer(const std::filesystem::path &file,
                const std::vector<LayerSource> &sources);

// Applies a directory's recorded mode and mtime after its children exist.
void sealDirectory(const Entry &entry, const std::filesystem::path &directory);

// Reads the entry at a host path the way a snapshot would have recorded
// the entry at `guestPath`. Throws for anything other than a regular file,
// a directory or a symlink.
Entry observe(const std::filesystem::path &host, const std::string &guestPath);

inline constexpr std::string_view kLayerExtension = ".layer";
} // namespace splice_ir

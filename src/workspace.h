#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace splice_ir {
// A workspace that many recorded builds share. Each build is a package
// below one directory, named after the store path the build makes. A
// second directory holds what every package uses: the script, the rule
// definitions and the guest's /bin and /etc. A third has a package for
// each store path, which `link` writes.
inline constexpr std::string_view kPackagesDirectory = "pkgs";
inline constexpr std::string_view kSharedDirectory = "genrulify";
inline constexpr std::string_view kStoreDirectory = "store";

// The target of a store path's package that a rule is given to read the
// store path: the path's archive and the digest of the archive. The two
// files have the names the sandbox script looks for.
inline constexpr std::string_view kStorePathTarget = "path";
inline constexpr std::string_view kStoreArchiveFile = "nar";
inline constexpr std::string_view kStoreDigestFile = "nar.sha256";

// The targets of a package that stand for the store paths of its closure
// and for the archives of all its outputs, and the file that says what
// the package builds, which `link` reads.
inline constexpr std::string_view kClosureTarget = "closure";
inline constexpr std::string_view kOutputsTarget = "outputs";
inline constexpr std::string_view kPackageFile = "package.json";

// The file of rule definitions in the shared directory.
inline constexpr std::string_view kSharedRulesFile = "genrulify.bzl";

// The last name of a store path.
inline std::string storeName(const std::string &path) {
  return std::filesystem::path(path).filename().string();
}

// Gives every store path of a shared workspace a package below store/:
// the archive a package of the workspace builds when `cache` has the same
// archive, the archive the cache has, or else an archive of the path as
// this machine's store has it, kept in the workspace. What the cache
// says of a path is kept below narinfo/ and asked for once. `attributes`
// names a file of `ATTRIBUTE PACKAGE` lines, each of which becomes an
// alias at the workspace's root, or is empty. Prints where the store
// paths come from. Throws naming every store path that has no source.
void linkWorkspace(const std::filesystem::path &workspace,
                   const std::string &cache,
                   const std::filesystem::path &attributes);

// A digest as Nix writes one, in its own base 32, from the digest in hex.
std::string nixBase32(std::string_view hex);
} // namespace splice_ir

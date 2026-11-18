#include "nlohmann/json.hpp"
#include "package.h"
#include "process.h"
#include "tools.h"
#include <regex>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
const fs::path buildRoot = "/build";
const fs::path storeRoot = "/nix/store";
const std::regex storeName("[0-9a-z]{32}-[A-Za-z0-9+._?=-]+");

bool storeObject(const fs::path &p) {
  return p.parent_path() == storeRoot &&
         std::regex_match(p.filename().string(), storeName);
}

fs::path beneath(const fs::path &path, const fs::path &parent) {
  auto relative = path.lexically_relative(parent);
  check(path.is_absolute() && path == path.lexically_normal() &&
            !relative.empty() && *relative.begin() != "..",
        "unexpected guest path: " + path.string());
  return relative;
}

void restore(const fs::path &archive, const fs::path &destination) {
  fs::create_directories(destination.parent_path());
  execute({std::string(kNixStore), "--restore", destination.string()}, archive);
}
} // namespace

void replayAction(const fs::path &manifest, const fs::path &input,
                  const fs::path &output) {
  const auto spec = nlohmann::json::parse(readBytes(manifest));
  const std::string action = spec.at("action");
  const bool compile = action == "compile";
  const bool install = action == "install";
  check(compile || install || action == "patchelf" || action == "strip",
        "unknown package action");
  const fs::path cwd = spec.at("cwd").get<std::string>();
  const fs::path out = spec.at("out").get<std::string>();
  const fs::path installed = spec.at("installed").get<std::string>();
  const fs::path binary = spec.at("binary").get<std::string>();
  check(storeObject(out) && installed.parent_path() == out / "bin",
        "invalid package output");
  Scratch scratch;
  const auto build = scratch.path / "build";
  const auto tree = scratch.path / "tree";
  const auto localCwd = build / beneath(cwd, buildRoot);
  const auto localBinary = build / beneath(binary, buildRoot);
  check(localBinary.parent_path() == localCwd || (!compile && !install),
        "expected compiler and installer in the source directory");

  // Stage only the predecessor NAR. Each fixup gets a writable copy of its
  // tree.
  fs::create_directories(build);
  if (compile) {
    check(cwd != buildRoot, "expected a source directory below /build");
    restore(input, localCwd);
    check(
        fs::is_directory(fs::symlink_status(localCwd)) &&
            !fs::exists(localBinary),
        "source archive already contains a build output or is not a directory");
  } else if (install) {
    restore(input, localBinary);
    fs::create_directory(tree);
  } else {
    restore(input, tree);
    check(fs::is_directory(fs::symlink_status(tree)),
          "expected an installed tree");
  }
  fs::create_directories(localCwd);

  // Mount the derivation's input closure at its original absolute paths.
  Arguments command{std::string(kBwrap),
                    "--unshare-all",
                    "--die-with-parent",
                    "--clearenv",
                    "--dev",
                    "/dev",
                    "--proc",
                    "/proc",
                    "--tmpfs",
                    "/tmp",
                    "--dir",
                    "/nix/store"};
  for (const fs::path path : spec.at("closure").get<Arguments>()) {
    check(storeObject(path) && path != out, "invalid input store object");
    command.insert(command.end(), {"--ro-bind", path.string(), path.string()});
  }
  command.insert(command.end(), {"--bind", build.string(), "/build"});
  if (!compile) {
    command.insert(command.end(), {"--bind", tree.string(), out.string()});
  }
  for (const auto &entry : spec.at("env").get<Arguments>()) {
    const auto equals = entry.find('=');
    check(equals != std::string::npos && equals != 0,
          "invalid environment entry");
    command.insert(command.end(), {"--setenv", entry.substr(0, equals),
                                   entry.substr(equals + 1)});
  }
  const auto argv = spec.at("argv").get<Arguments>();
  check(!argv.empty(), "missing executable arguments");
  command.insert(command.end(),
                 {"--chdir", cwd.string(), "--argv0", argv.front(), "--",
                  spec.at("program").get<std::string>()});
  command.insert(command.end(), argv.begin() + 1, argv.end());
  execute(command);

  // Check bytes at the recorded process exit. The last NAR also checks tree
  // shape and mode.
  const auto witness = compile ? localBinary : tree / beneath(installed, out);
  check(fs::is_regular_file(fs::symlink_status(witness)) &&
            fs::canonical(witness) == witness.lexically_normal(),
        "missing regular-file output after " + action);
  check(fileHash(witness) == spec.at("fileHash").get<std::string>(),
        action + " differs from the recorded executable");
  const auto result = scratch.path / "result.nar";
  execute({std::string(kNixStore), "--dump",
           (compile ? localBinary : tree).string()},
          {}, result);
  if (action == "strip") {
    check(fileHash(result) == spec.at("narHash").get<std::string>(),
          "installed NAR differs from Rewind");
  }
  fs::copy_file(result, output, fs::copy_options::overwrite_existing);
}
} // namespace splice_ir

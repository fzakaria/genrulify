#include "rules.h"
#include <algorithm>
#include <filesystem>
#include <regex>
#include <set>
#include <span>

namespace splice_ir {
namespace fs = std::filesystem;
namespace {
// The names a C or C++ compiler driver goes by: an optional target prefix,
// as in `x86_64-unknown-linux-gnu-gcc`, and an optional version, as in
// `gcc-13`.
constexpr std::string_view kCompilerDrivers =
    R"((.*-)?(gcc|g\+\+|cc|c\+\+|clang|clang\+\+)(-[0-9.]+)?)";
constexpr std::string_view kArchiver = "ar";
constexpr std::string_view kRustCompiler = "rustc";

// The option that makes a compiler driver stop once it has an object, and
// the ones that make it stop before it has one.
constexpr std::string_view kCompileOnly = "-c";
constexpr std::string_view kStopsEarly[] = {"-E", "-S", "-M", "-MM"};

// How a driver is told where to write and to link a shared library.
constexpr std::string_view kOutputOption = "-o";
constexpr std::string_view kSharedOption = "-shared";

// The name of a shared library: `libx.so`, `libx.so.1.2`.
constexpr std::string_view kSharedLibraryName = R"(.*\.so(\.[0-9]+)*)";

// The extensions of the files a C or C++ compiler takes as sources.
constexpr std::string_view kSourceExtensions[] = {
    ".c", ".cc", ".cpp", ".cxx", ".c++", ".C", ".m",
    ".mm", ".S", ".s",  ".sx",  ".i",   ".ii"};

// The letters of ar's first argument that add members to an archive, and
// the ones that only read one or take members out.
constexpr std::string_view kArchiveAdds = "rq";
constexpr std::string_view kArchiveReads = "txpdm";

// How rustc is told what to build, and what each answer is raised to.
constexpr std::string_view kCrateType = "--crate-type";
constexpr std::string_view kBinaryCrate = "bin";
constexpr std::string_view kProcMacroCrate = "proc-macro";
constexpr std::string_view kLibraryCrates[] = {"lib", "rlib", "dylib", "cdylib",
                                               "staticlib"};

// The programs that copy a tree and change modes, and the argument after
// which every argument is a path.
constexpr std::string_view kCopyProgram = "cp";
constexpr std::string_view kModeProgram = "chmod";
constexpr std::string_view kEndOfOptions = "--";

// Where GNU coreutils keeps a program in the Nix store: a store path
// named `coreutils` or `coreutils-full` and a version, and in its bin
// either the program or the one binary that is every program. A package
// with another name is another implementation, however its programs are
// called.
constexpr std::string_view kCoreutilsDirectory =
    R"(/nix/store/[0-9a-z]{32}-coreutils(-full)?-[0-9][^/]*/bin/)";
constexpr std::string_view kMultiCallBinary = "coreutils";

// The options of cp under which a copy has the executable bits of its
// source: recursion, preservation, force, verbosity and reflinks. An
// option that is not here, such as --no-preserve or -t, means the command
// is not read.
constexpr std::string_view kCopyOptions =
    R"(-[rRpafv]+|--recursive|--archive|--force|--verbose|--reflink(=[a-z]+)?|--preserve(=[a-z,]+)?)";

// The options of chmod, and a mode that adds or removes only the rights
// to read and to write.
constexpr std::string_view kModeOptions = R"(-[Rfvc]+|--recursive|--silent|--quiet|--verbose|--changes)";
constexpr std::string_view kWriteBitMode =
    R"([ugoa]*[-+][rw]+(,[ugoa]*[-+][rw]+)*)";

bool matches(const std::string &text, std::string_view pattern) {
  return std::regex_match(text, std::regex(std::string(pattern)));
}

// The program a process ran, by the last name of the path given to
// execve. A recording made before that path was kept has argv[0].
std::string programName(const Process &process) {
  const auto &path = process.filename.empty() ? process.argv.front()
                                              : process.filename;
  return fs::path(path).filename().string();
}

// The arguments the command was given. For a script the kernel puts the
// interpreter, and then the script's path, in front of them.
std::span<const std::string> arguments(const Process &process) {
  const std::span all(process.argv);
  const auto script = std::ranges::find(all.subspan(1), process.filename);
  if (!process.filename.empty() && script != all.end()) {
    return all.subspan(script - all.begin() + 1);
  }
  return all.subspan(1);
}

bool has(std::span<const std::string> arguments, std::string_view option) {
  return std::ranges::find(arguments, option) != arguments.end();
}

// The value of an option written `-o VALUE` or `-oVALUE`, or of one
// written `--name VALUE` or `--name=VALUE` when `joiner` is `=`.
std::optional<std::string> valueOf(std::span<const std::string> arguments,
                                   std::string_view option,
                                   std::string_view joiner = "") {
  for (size_t index = 0; index < arguments.size(); ++index) {
    const std::string_view argument = arguments[index];
    if (argument == option && index + 1 < arguments.size()) {
      return arguments[index + 1];
    }
    const auto joined = std::string(option) + std::string(joiner);
    if (argument.size() > joined.size() && argument.starts_with(joined)) {
      return std::string(argument.substr(joined.size()));
    }
  }
  return std::nullopt;
}

// What a compiler driver's command line builds.
std::optional<std::string_view> driverKind(
    std::span<const std::string> arguments) {
  // An option that stops the driver before the assembler wins over `-c`:
  // `gcc -E -c a.c` writes preprocessed text and no object.
  for (const auto option : kStopsEarly) {
    if (has(arguments, option)) {
      return std::nullopt;
    }
  }
  if (has(arguments, kCompileOnly)) {
    return kCcObject;
  }

  // A driver that links says where to. One that was only asked for its
  // version, or left to write a.out, is not worth a name.
  const auto output = valueOf(arguments, kOutputOption);
  if (!output) {
    return std::nullopt;
  }
  const bool shared =
      has(arguments, kSharedOption) ||
      matches(fs::path(*output).filename().string(), kSharedLibraryName);
  return shared ? kCcSharedLibrary : kCcBinary;
}

// What ar's command line builds: a library, when its operation adds
// members.
std::optional<std::string_view> archiverKind(
    std::span<const std::string> arguments) {
  if (arguments.empty()) {
    return std::nullopt;
  }
  const std::string_view operation = arguments.front();
  const bool adds = operation.find_first_of(kArchiveAdds) != std::string::npos;
  const bool reads = operation.find_first_of(kArchiveReads) != std::string::npos;
  if (adds && !reads) {
    return kCcLibrary;
  }
  return std::nullopt;
}

// What rustc's command line builds, by the first crate type it names.
std::optional<std::string_view> rustKind(
    std::span<const std::string> arguments) {
  const auto types = valueOf(arguments, kCrateType, "=");
  if (!types) {
    return std::nullopt;
  }
  const auto type = types->substr(0, types->find(','));
  if (type == kBinaryCrate) {
    return kRustBinary;
  }
  if (type == kProcMacroCrate) {
    return kRustProcMacro;
  }
  if (std::ranges::find(kLibraryCrates, type) != std::end(kLibraryCrates)) {
    return kRustLibrary;
  }
  return std::nullopt;
}

// Whether a process ran the GNU coreutils program `tool`. A name alone
// does not say what a program does: anything can be called cp. A command
// may be replaced by what its command line says only when the program is
// known, so the file the kernel loaded has to be that program in a
// coreutils store path, or the one binary of that package that acts by
// the name it is called with. The recording has that file as `exe`, and
// one made by hand without it has the path given to execve.
bool isCoreutils(const Process &process, std::string_view tool) {
  if (process.argv.empty() || programName(process) != tool ||
      fs::path(process.argv.front()).filename() != tool) {
    return false;
  }
  const auto &loaded = process.exe.empty() ? process.filename : process.exe;
  return matches(loaded, std::string(kCoreutilsDirectory) + "(" +
                             std::string(tool) + "|" +
                             std::string(kMultiCallBinary) + ")");
}

// A path a command names, resolved against its working directory, with
// no slash at the end.
std::string resolved(const Process &process, const std::string &argument) {
  auto path = (fs::path(process.cwd) / argument).lexically_normal().string();
  while (path.size() > 1 && path.ends_with("/")) {
    path.pop_back();
  }
  return path;
}
} // namespace

std::optional<CopyCommand> copyCommand(const Process &process) {
  if (!isCoreutils(process, kCopyProgram)) {
    return std::nullopt;
  }

  // Options come first, up to `--` when there is one. Every argument that
  // is not an option is a path.
  std::vector<std::string> paths;
  bool options = true;
  for (const auto &argument : arguments(process)) {
    if (options && argument == kEndOfOptions) {
      options = false;
      continue;
    }
    if (options && argument.starts_with("-")) {
      if (!matches(argument, kCopyOptions)) {
        return std::nullopt;
      }
      continue;
    }
    paths.push_back(resolved(process, argument));
  }

  // The last path is where the others are copied to.
  if (paths.size() < 2) {
    return std::nullopt;
  }
  CopyCommand command;
  command.destination = paths.back();
  paths.pop_back();
  command.sources = std::move(paths);
  return command;
}

std::optional<std::vector<std::string>> writeBitChange(const Process &process) {
  if (!isCoreutils(process, kModeProgram)) {
    return std::nullopt;
  }

  // The first argument that is not an option is the mode, and what
  // follows it are paths, after `--` when there is one.
  std::vector<std::string> paths;
  bool modeSeen = false;
  bool options = true;
  for (const auto &argument : arguments(process)) {
    if (!modeSeen) {
      if (matches(argument, kModeOptions)) {
        continue;
      }
      if (!matches(argument, kWriteBitMode)) {
        return std::nullopt;
      }
      modeSeen = true;
      continue;
    }
    if (options && argument == kEndOfOptions) {
      options = false;
      continue;
    }
    if (options && argument.starts_with("-")) {
      return std::nullopt;
    }
    paths.push_back(resolved(process, argument));
  }
  if (paths.empty()) {
    return std::nullopt;
  }
  return paths;
}

std::optional<std::string_view> ruleKind(const Process &process) {
  if (process.argv.empty()) {
    return std::nullopt;
  }
  const auto program = programName(process);
  const auto given = arguments(process);
  if (matches(program, kCompilerDrivers)) {
    return driverKind(given);
  }
  if (program == kArchiver) {
    return archiverKind(given);
  }
  if (program == kRustCompiler) {
    return rustKind(given);
  }
  return std::nullopt;
}

std::vector<std::string> sourcePaths(const Process &process) {
  std::vector<std::string> paths;
  if (process.argv.empty()) {
    return paths;
  }
  for (const auto &argument : arguments(process)) {
    if (argument.starts_with("-")) {
      continue;
    }
    const auto extension = fs::path(argument).extension().string();
    if (std::ranges::find(kSourceExtensions, extension) ==
        std::end(kSourceExtensions)) {
      continue;
    }
    paths.push_back(
        (fs::path(process.cwd) / argument).lexically_normal().string());
  }
  return paths;
}
} // namespace splice_ir

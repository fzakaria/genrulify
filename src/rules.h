#pragma once

#include "recording.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace splice_ir {
// The kinds of build rule a recorded command can be raised to. Each is the
// name of a rule in the files every workspace ships, genrulify.bzl and
// genrulify.buck.bzl, and all of those rules run the recorded command
// through the sandbox script. So a kind is a label: it decides what a rule
// is called and how its inputs are named, and a wrong one cannot change
// what the rule does.
inline constexpr std::string_view kCcObject = "cc_object";
inline constexpr std::string_view kCcLibrary = "cc_library";
inline constexpr std::string_view kCcBinary = "cc_binary";
inline constexpr std::string_view kCcSharedLibrary = "cc_shared_library";
inline constexpr std::string_view kRustLibrary = "rust_library";
inline constexpr std::string_view kRustBinary = "rust_binary";
inline constexpr std::string_view kRustProcMacro = "rust_proc_macro";

// The rule for a command with no kind, and the rule that archives a
// derivation output.
inline constexpr std::string_view kRecordedRule = "recorded";
inline constexpr std::string_view kArchiveRule = "nix_archive";

// The kind of rule a process stands for, read from the program's name and
// its arguments: a compiler driver that stops after compiling, one that
// links, `ar` adding members, `rustc` by its crate type. Nothing for a
// command that is none of those.
std::optional<std::string_view> ruleKind(const Process &process);

// What a `cp` command line copies: the files or trees it names, and the
// place it copies them to, as paths resolved against the working
// directory of the process.
struct CopyCommand {
  std::vector<std::string> sources;
  std::string destination;
};

// What a process copies when it is a `cp` whose every option is known to
// hand a file's executable bits on from the source: the recursive and
// preserving options, and nothing that names the target another way or
// drops the mode. Nothing for any other command.
std::optional<CopyCommand> copyCommand(const Process &process);

// The paths a process changes the mode of when it is a `chmod` that only
// adds or removes the right to read or to write, which leaves every
// executable bit as it was. Nothing for any other command.
std::optional<std::vector<std::string>> writeBitChange(const Process &process);

// The guest paths of the files a compile names as its sources: the
// arguments with the extension of a source file, resolved against the
// working directory of the process.
std::vector<std::string> sourcePaths(const Process &process);
} // namespace splice_ir

# Nixsplice

Nixsplice converts a recorded Nix build into Bazel and Buck2 genrules.
The C++23 compiler imports the derivation and Rewind events into an MLIR
dialect, emits both build files and checks the resulting archive against
an independent Nix build.

The current importer accepts one builder language on x86_64 Linux. The
pinned Bash runs a straight-line script of GNU dd copies and appends over
regular files. This narrow example exercises in-place writes, an actual
MLIR optimization and both build systems. It gives us a regression test
for the conversion before taking on compiler drivers and stdenv hooks.

## Try the conversion

Run the interactive explorer locally with `nix run .#serve`. Open
<http://127.0.0.1:8136>. The site build generates the downloadable
MLIR, BUILD.bazel and BUCK files with the compiler.

For the recorded fixture, run the following in the checkout with a new
destination directory. Building the example realizes its store inputs.

```sh
nix build --no-link .#example
nix run . -- import tests/data/derivation.json tests/data/events.jsonl > recorded.mlir
nix run . -- opt recorded.mlir > folded.mlir
nix run . -- emit folded.mlir workspace
```

For a fresh recording, run `tools/reproduce.sh` from a machine with Nix,
Rewind, jq and working KVM. It builds the Nix reference, records the
builder in Rewind, generates two workspaces, builds both with the pinned
Bazel and Buck2, and compares all four archives. The script prints the
directory containing the results. The workers use the local Nix store.

## What the conversion does

1. **Import.** Read format 4 from `nix derivation show` and the JSONL
   from `rewind events --json`. Check the complete script, expected argv,
   successful write opens and child exits. Derive each read from the
   semantics of the accepted command. An unrecognized script fails
   import. The checked-in event fixture retains the exec, open and exit
   records from run `16b4e3d79a715325`.
2. **Assign file values.** In the fixture, the write at step 746 produces
   the first version of `/build/bundle`. The write at step 775 appends a
   second input. The append takes the first version as an operand. Its
   result has a different SSA identity even though both writes name the
   same pathname. The final command copies that result to the Nix output.
3. **Fold copies.** A `!splice.file` represents the complete contents of
   a non-executable regular file. The registered `splice.copy` folder
   returns its operand. MLIR's canonicalizer removes both copies and
   keeps the append's two operands in byte order. This optimization
   relies on the file type's restricted semantics.
4. **Lower.** Emit one genrule per surviving file operation. Each rule
   consumes declared source files or prior targets and writes a fresh
   output. A final rule serializes the published file with its Nix mode.
   Running `emit` on the original module keeps the three file operations.
   Running it after `opt` emits one. Both graphs have the archive rule.
5. **Verify.** Hash the backend's NAR and a separate `nix-store --dump`
   of the original Nix output. Require both SHA-256 digests to equal
   Rewind's recorded output digest. For the fixture the digest is
   `1c763cb553edd9c4e7632da40b6b2466b54b89b818844c85ed2033df7cf9eab9`.

The complete compiler is C++ with Abseil and MLIR's TableGen operation
definitions. `nix develop` provides matching MLIR headers and TableGen.
`nix flake check` builds the compiler, runs the native CLI regression
tests and builds the explorer.

## Fixed-build verification

The reference is a successful execution with a specific input closure
and execution platform. Matching every output NAR establishes equality
of the archived trees for that execution, including executable bits,
symlink targets and directory entries where applicable. Nix store
registration metadata, including references, is a separate comparison
for a converter that also promises interchangeable store objects.

A matching final archive cannot certify that a proposed action is safe
to cache on another machine or reuse after an edit. For that claim,
every observation that affects the action must be in its inputs or its
execution platform. A stronger verifier would rerun isolated actions,
check intermediate artifacts at each cut, and retrace or reject a build
when a recorded assumption changes. Adding a header changes the fixed
input set and establishes a new verification case.

## Extending the importer to stdenv

Rewind has two relevant streams. Its exec event has a filename and up to
1,024 bytes of argv. It has no envp. Its separate binary I/O log tracks
regular-file read and write opens, write offsets and lengths, and lost
records. The public event stream's read-open event has the narrower
meaning of a read open concurrent with a writer. Neither interface
supplies a complete account of failed lookups and directory walks.
See the [guest exec probe](https://github.com/fzakaria/rewindvm/blob/8e713840570c/guest/linux/rewind-guest.patch),
[event types](https://github.com/fzakaria/rewindvm/blob/8e713840570c/crates/rewind-trace/src/event.rs)
and [I/O record types](https://github.com/fzakaria/rewindvm/blob/8e713840570c/crates/rewind-trace/src/io_records.rs).

For a real compile, start with the compiler driver and its descendants as
one action. The driver already knows how to run its children. A useful
cut needs the exact argv, environment, working directory, inherited
descriptors and input tree. Record object identity through renames, links
and unlinks. Version bytes at observable changes, since opening an
existing file for writing can leave every byte untouched. Keep pipes,
shared mappings and concurrent writers within one action until their
state can be represented across a cut. Shell builtins can write files
without introducing any exec boundary.

Include the complete tool closure in a worker image or stage it under
its original store paths and bind the platform identity into the action
key. Materializing an empty directory for a failed lookup can turn
`ENOENT` into success. Reproduce the observed filesystem and track the
directory membership that justified the absence. A broad fallback is to
rerun the enclosing builder in its recorded environment, with fewer
independent actions available for caching.

A close precedent is Riker's TraceIR. Its treatment of filesystem state
and command re-execution is relevant to the step from fixed builds to
incremental reuse. [Riker, USENIX ATC 2022](https://www.usenix.org/conference/atc22/presentation/curtsinger)

## A compiler boundary in colstr

I recorded the locked nixpkgs package `colstr-1.0.0` in Rewind run
`534fa58c62c668f8`. Its 83 exec events include the GCC wrapper, driver,
compiler, assembler, linker and stdenv fixup tools. Rewind matched the
package's Nix store NAR. The `g++` exec at step 2660 ends mid-argument
in the event stream.

At that step, `rewind cat 534fa58c62c668f8 2660 /proc/106/cmdline`
recovered 1,291 bytes of NUL-delimited arguments. The same interface
returned the process environment from `/proc/106/environ`. I unpacked a
snapshot of `/build` from before compilation, mounted it at the recorded
path in a fresh mount namespace with the input store closure, and ran
the recovered driver in `/build/source`. The snapshot contained
`main.cpp`, its license and the stdenv environment dump.

The rebuilt `a.out` and the guest's `a.out` at step 2982 both hashed to
`bd25c4990c4631046d50dc0a711b878941fb24bc27e31e6d34512633b9615812`.
This was a manually selected compiler boundary. Automating the boundaries
and carrying installation and fixup through both backends are open work.

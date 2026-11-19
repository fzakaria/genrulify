<p align="center">
  <img src="site/img/mark.svg" width="64" height="64" alt="" />
</p>

<h1 align="center">genrulify</h1>

<p align="center">
  <b>Turn a recorded Nix build into Bazel and Buck2 rules.</b>
</p>

<p align="center">
  <a href="https://fzakaria.github.io/genrulify/">Explorer</a> ·
  <a href="https://fzakaria.github.io/genrulify/how-to.html">How to use it?</a> ·
  <a href="https://fzakaria.github.io/genrulify/theory.html">Why it works</a> ·
  <a href="docs/theory.md">Theory</a> ·
  <a href="src/Splice.td">The MLIR dialect</a>
</p>

<p align="center">
  <a href="https://nixbot.decio.us/repos/github/fzakaria/genrulify"><img src="https://nixbot.decio.us/badge/3b4d3f0d-de80-420b-8aa6-d99459a79380.svg" alt="nixbot" /></a>
</p>

genrulify records a Nix build with [Rewind](https://rewindvm.dev) and
writes `BUILD.bazel` and `BUCK` from the commands that ran. If GCC read a
header that `configure` wrote, the generated compile rule depends on
that version of the header.

## Use it

Recording requires Nix with flakes enabled, Rewind on `PATH` and KVM on
x86-64 Linux. The generated rules need Bazel or Buck2, bubblewrap and the
Nix store paths used in the recording. The flake offers a binary cache
at `genrulify.cachix.org`.

### Rebuild a Nix package

Record GNU hello and build its Nix archive (NAR) from the generated
workspace. In this run the archive from Bazel has the same SHA-256
digest as the output that Rewind recorded.

```console
$ nix run github:fzakaria/genrulify -- convert nixpkgs#hello hello
650 process groups: 132 replayed and reproduced, 75 raised to copies, 50 folded to constants, 0 kept as recorded bytes, 393 dead
out: 49 files (253442 bytes) from commands that run, 0 files (0 bytes) from the recording, 99 entries without bytes
cca3804d340495150db44b1e762655d8a0b4fe6d37b8350f1a0d8fa33c27c8cc  /nix/store/5z2yp3ysx8476c8g5w25b0smlgkjvaq3-hello-2.12.3

$ cd hello/workspace && bazel build //:archive
INFO: Build completed successfully, 134 total actions

$ sha256sum bazel-bin/archive.nar
cca3804d340495150db44b1e762655d8a0b4fe6d37b8350f1a0d8fa33c27c8cc  bazel-bin/archive.nar
```

From `hello/workspace`, `buck2 build //:archive` builds the same target.
The recording and optimized graph are in `hello/recording` and
`hello/graph.mlir`.

### Build a project you can edit

From the root of this repository, convert the [C example](examples/project/README.md).
The `.#project` derivation uses `examples/project` as its source.

```console
$ nix run . -- convert --source examples/project .#project /tmp/project-recording
$ cd examples/project
$ bazel build //:archive
```

`--source` writes the rules into `genrulify/` in the project, with a
`BUILD.bazel` and a `BUCK` that load them. A build file you wrote
yourself is left alone, and `convert` prints the two lines that load the
rules from it. Each compile reads its source from your checkout.

Change the greeting in `greet.c` and build again. Bazel and Buck2
recompile `greet.c`, reuse `main.o` and relink the program. To try the
program without packing it, run it.

```console
$ bazel run //:greet
```

`//:greet` and `//:greet.o` are named after the files, so a rule of your
own can depend on them and still build after you convert again. If you
add a header dependency or change a compile command, run `convert` again
from the repository root with a fresh output directory. A command that
no longer leaves a file or a directory it left before fails its rule,
and the message says to convert again. The derivation's source should
not take in `buck-out` or Bazel's `bazel-*` links. A flake in a git
repository leaves such untracked files out.

## How it works

Rewind lets genrulify inspect `/build` and the output paths before and
after each command. Comparing those snapshots tells us which files
changed. Commands that share a pipe or overlap form one group. A program
like `make` that only launches other commands can be split at its children.

Each write creates a new version of a path. If one command appends to a
file another command created, the graph connects the first version to
the command that reads it. genrulify represents these connections in an
MLIR dialect called `splice`.

The optimizer removes groups whose results never reach the output. It
can also replace `mkdir` with a directory entry or `cp` with a reference
to the original bytes. You can turn each optimization on and off in the
[build explorer](https://fzakaria.github.io/genrulify/).

Each remaining group runs in a sandbox with its recorded inputs. The
checker compares its exit statuses and used results with the recording.
If a group fails, the checker can try more inputs or take individual
results from the recording. The report counts how many groups replayed
and how many needed recorded bytes.

Each replayed group becomes a build rule. For example, the GNU hello
rule below takes `hello.c` from the tarball and `config.h` from the
configure step. The `cc_object` rule calls `sandbox.sh` to stage those
files and run the recorded GCC command. The full rule is in the
[hello explorer](https://fzakaria.github.io/genrulify/?pkg=hello).

```python
cc_object(
    name = "s95823_gcc",
    src = ":s1378_tar/build/hello-2.12.3/src/hello.c",
    hdrs = [
        …
        ":s4254_bash/build/hello-2.12.3/config.h",
        …
    ],
    out = "s95823_gcc/build/hello-2.12.3/src/hello.o",
    …
)
```

If each group produces the same result on the same inputs and platform,
any build order that respects the dependencies reproduces the recorded
archive. The checker samples this with two clean replay passes. In a
recorded workspace each command rule checks its outputs again when it
runs, and the archive rule fails unless the NAR has the recorded digest. [Why this works](https://fzakaria.github.io/genrulify/theory.html)
gives the proof.

## Results

| Package | Build | `execve` | Groups | Run | Copies | Constants | Recorded | Rules |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `colstr-1.0.0` | stdenv, one `c++` | 83 | 54 | 4 | 2 | 1 | 0 | 5 |
| `sl-5.05` | Makefile | 94 | 59 | 6 | 3 | 1 | 0 | 7 |
| `tree-2.3.2` | Makefile | 183 | 92 | 18 | 3 | 4 | 0 | 20 |
| `cowsay-3.8.4` | Makefile, Perl | 147 | 94 | 4 | 5 | 9 | 0 | 6 |
| `hello-go` | `go build` | 192 | 55 | 3 | 2 | 2 | 0 | 4 |
| `python3.14-six-1.17.0` | `pyproject-build` | 137 | 107 | 3 | 2 | 2 | 0 | 5 |
| `hexyl-0.17.0` | Rust, `cargo` | 612 | 82 | 9 | 4 | 3 | 0 | 10 |
| `hexyl-0.17.0`, `--open cargo` | Rust, `cargo` | 612 | 164 | 42 | 4 | 3 | 0 | 43 |
| `ninja-1.13.2` | Python bootstrap, Ninja | 493 | 81 | 9 | 4 | 4 | 0 | 10 |
| `inih-62`, `--open ninja` | Meson, Ninja | 472 | 121 | 10 | 3 | 4 | 0 | 12 |
| `tinyxml2-11.0.0` | CMake | 275 | 81 | 6 | 2 | 3 | 0 | 7 |
| `jsoncpp-1.9.8` | CMake | 602 | 137 | 8 | 4 | 6 | 0 | 10 |
| `lz4-1.10.0` | patch, CMake | 806 | 194 | 39 | 6 | 8 | 0 | 43 |
| `zlib-1.3.2` | patches, `configure` | 700 | 202 | 39 | 20 | 12 | 0 | 42 |
| `lua-5.2.4` | Makefile | 386 | 145 | 50 | 9 | 5 | 0 | 52 |
| `patchelf-0.15.2` | autotools | 503 | 76 | 6 | 3 | 6 | 0 | 7 |
| `which-2.25` | patch, autotools | 1,137 | 98 | 15 | 3 | 3 | 0 | 18 |
| `hello-2.12.3` | autotools | 4,605 | 650 | 132 | 75 | 50 | 0 | 133 |
| `gawk-5.4.1` | autotools | 6,857 | 1,260 | 145 | 96 | 36 | 0 | 148 |
| `gnused-4.10` | autotools | 8,848 | 645 | 152 | 84 | 65 | 0 | 154 |
| `bash-interactive-5.3p15` | autotools, 6 outputs | 11,847 | 1,450 | 360 | 161 | 138 | 0 | 366 |
| `sqlite-3.53.3`, tests off | `configure`, 6 outputs | 1,121 | 255 | 42 | 15 | 20 | 0 | 48 |
| `hello-2.12.3`, coarse cuts | autotools | 4,605 | 70 | 9 | 1 | 2 | 0 | 10 |
| `hello-2.12.3`, 4 jobs | autotools | 4,605 | 125 | 9 | 1 | 2 | 0 | 10 |

*Groups* are commands or pipelines. *Run* is how many of them a build
system runs again, each checked to leave the recorded bytes. *Copies*
and *Constants* are groups the passes replaced. *Recorded* is how many
had to be carried as recorded bytes, and everything else was dead. For
every package the NAR Bazel builds and the NAR Buck2 builds are the NAR
Rewind recorded.

The coarse row cuts hello only at the commands the builder runs, which
leaves `make` as one rule. The four-job row is a recording made with
`--cores 4`. Commands that overlapped share a group.

## Inspect a conversion

Use `--open` to split a program such as Cargo into the commands it ran,
even if it also wrote files. Use `--coarse` to split only at commands
started by the builder.

```console
$ nix run github:fzakaria/genrulify -- convert --open cargo nixpkgs#hexyl hexyl
```

You can also run the stages individually to inspect the recording or try
a different set of optimizations.

```console
$ genrulify record RUN /nix/store/…-hello-2.12.3.drv recording
$ genrulify check recording dce fold
$ genrulify import recording > recorded.mlir
$ genrulify opt --recording recording recorded.mlir dce fold > graph.mlir
$ genrulify emit recording graph.mlir workspace
```

`check` writes its replay decisions into `recording/verdicts.json`.
`import recording` reads that file. For example, if a command needed an
extra header to replay, the imported graph gives the command that input.
If a result had to come from the recording, the graph represents it as a
literal. `recorded.mlir` therefore already includes the checker's decisions.

Use the same passes for `check` and `opt` (`dce fold` here). `check` also
writes `recording/check.json`, a report that `emit` reads so it can reject
a graph that asks it to run a group the checker skipped.

## Limits

Rewind records on one virtual CPU with at most 3 GiB of memory. Use one
build job for finer rules. Commands that overlap share a snapshot, so
they must stay together. The generated rules run locally because they
read tools and libraries from the host's `/nix/store`.

A project's rules hold what one build did: which commands ran and with
which arguments. A value the builder read from a source and passed on a
command line stays as recorded, and so does a symlink in the source
tree. Bazel and Buck2 do not run a rule again
when only a file's mode changes, so `chmod +x` on a source reaches the
output with the next change to the file's text.

## Layout

```
docs/theory.md     what is assumed, what is checked, what follows
src/Splice.td      the dialect: entries and four operations
src/trace.cpp      events to cuts: which processes are commands
src/record.cpp     a Rewind run to a recording: processes and snapshots
src/import.cpp     a recording to file SSA
src/passes.cpp     link, fold, unpack, merge, raise, and the pipeline
src/rules.cpp      what a command line is: a compile, a link, a copy
src/check.cpp      the replay ladder
src/origins.cpp    where the files of each output came from
src/action.cpp     one operation as a step: inputs, command lines, results
src/sandbox.sh.in  the script a rule runs, shipped in every workspace
src/emit.cpp       the graph to BUILD.bazel and BUCK
src/genrulify.bzl  the rule kinds, shipped in every workspace
src/convert.cpp    the whole pipeline as one command
site/              the explorer
tests/             CLI tests over a checked-in recording
tools/validate.sh  convert, build with both systems, compare the NARs
```

```console
$ nix build                 # the compiler, with its tests
$ nix develop               # MLIR, TableGen, CMake
$ nix run .#serve           # the explorer, on port 8139
```

## Credits

Filesystem snapshots, process state and deterministic VM replay come
from [Rewind](https://rewindvm.dev).

The model of tasks, a store and a scheduler is from
[Build Systems à la Carte](https://doi.org/10.1145/3236774) by Mokhov,
Mitchell and Peyton Jones. In its terms a recording turns a build's
dynamic dependencies into static ones.

[Riker](https://www.usenix.org/conference/atc22/presentation/curtsinger)
(Curtsinger and Barowy, USENIX ATC 2022) builds from a trace of commands
and their filesystem effects, and its TraceIR is the closest relative of
this dialect. Memoize and Fabricate used `strace` to find dependencies
after the fact.

## License

MIT, please see [LICENSE](LICENSE).

<p align="center">
  <img src="site/img/mark.svg" width="64" height="64" alt="" />
</p>

<h1 align="center">genrulify</h1>

<p align="center">
  <b>Any Nix build, as Bazel and Buck2 genrules.</b><br />
  Record a build once and get a build file with one rule for every command it ran.
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

genrulify takes a build that [Rewind](https://rewindvm.dev) recorded and
writes a `BUILD.bazel` and a `BUCK` file for it. It has no knowledge of
C, Rust or autotools that a build depends on, and it never reads the
build script. The graph comes from the recording: which commands ran,
what each one found on disk, and what each one left behind.

```console
$ nix run github:fzakaria/genrulify -- convert nixpkgs#hello hello
659 process groups: 132 replayed and reproduced, 75 raised to copies, 50 folded to constants, 0 kept as recorded bytes, 402 dead
cca3804d340495150db44b1e762655d8a0b4fe6d37b8350f1a0d8fa33c27c8cc  /nix/store/5z2yp3ysx8476c8g5w25b0smlgkjvaq3-hello-2.12.3

$ cd hello/workspace && bazel build //:archive
INFO: Build completed successfully, 134 total actions

$ sha256sum bazel-bin/archive.nar
cca3804d340495150db44b1e762655d8a0b4fe6d37b8350f1a0d8fa33c27c8cc  bazel-bin/archive.nar

$ nix-store --dump /nix/store/5z2yp3ysx8476c8g5w25b0smlgkjvaq3-hello-2.12.3 | sha256sum
cca3804d340495150db44b1e762655d8a0b4fe6d37b8350f1a0d8fa33c27c8cc  -
```

Bazel built the archive Nix built, byte for byte, from 133 rules. 83 of
them compile one file each, one is the `ar` and one is the link. This is
the rule for `src/hello.c`:

```python
cc_object(
    name = "s95245_gcc",
    src = ":s1375_tar/build/hello-2.12.3/src/hello.c",
    hdrs = [
        ":s1375_tar/build/hello-2.12.3/src/system.h",
        ":s4251_bash/build/hello-2.12.3/config.h",
        ":s91456_sed/build/hello-2.12.3/lib/stddef.h-t",
        …
    ],
    out = "s95245_gcc/build/hello-2.12.3/src/hello.o",
    data = [":actions/s95245_gcc.manifest", ":closure.txt", ":env/62.env", ":sandbox.sh", …],
    cmd = [
        "./sandbox.sh --in . --in $(RULEDIR) --out $(RULEDIR)",
        "--name s95245_gcc --timeout 300",
        "run --cwd /build/hello-2.12.3 --env 62 --fd 0=null --fd 1=err --fd 2=err --status 0 --argc 40 --",
        "/nix/store/5q6b…-gcc-16.2.0/bin/gcc … -g -O2 -c -o src/hello.o src/hello.c",
    ],
)
```

Nobody declared those headers. They are the files the compiler opened:
`system.h` as `tar` unpacked it, `config.h` as `configure` wrote it, and
one that `sed` generated a moment earlier. The last line of `cmd` is the
`execve` that was recorded.

## How it works

1. **Cut the build at its commands.** The builder's shell is not
   replayed. Neither is a process that only ran other commands and wrote
   no file itself, such as `make` or a recipe shell: the trace shows
   which processes those are, so no list of tool names is involved. The
   cuts land on `gcc`, `sed`, `install`, `strip`. A process that writes
   files, such as `configure`, stays one command.
2. **Diff the filesystem around each command.** Rewind can return to any
   step of the build, so genrulify reads `/build` and the output paths
   before and after every command. The difference is what the command
   wrote. No system call is modelled.
3. **Give every write a version.** A file written twice has two values,
   so an in-place edit is an ordinary edge. This is SSA over files, in an
   [MLIR](https://mlir.llvm.org) dialect called `splice`:

   ```mlir
   %dd_735 = splice.exec "dd" at 735(%sandbox_717) -> [
       #splice.entry<"/build/bundle" file 644 mtime 1 "cb94740a…">
   ]
   %dd_764 = splice.exec "dd" at 764(%sandbox_717, %dd_735) -> [
       #splice.entry<"/build/bundle" file 644 mtime 1 "50f55425…">
   ]
   ```

4. **Optimize.** Passes over the dialect remove what a build system need
   not run. They can be switched on and off in the
   [explorer](https://fzakaria.github.io/genrulify/).

   | Pass | What it does | What it removes |
   | --- | --- | --- |
   | `link` | A file the driver wrote with the bytes of an existing file becomes a copy of it. | `cargo` linking a binary to its final name |
   | `dce` | Deletes operations nothing reads. MLIR's stock canonicalizer. | `date`, a `find` that only looked |
   | `fold` | Directories, symlinks and empty files become constants. | `mkdir`, `ln -s`, `mktemp`, `touch` |
   | `merge` | A result with the bytes of the operand at the same path becomes a copy. | `chmod` |
   | `raise` | A result with the bytes of an operand at another path becomes a copy. | `cp`, `install`, `mv` |
   | `rules` | A command recognised from its arguments is given a rule of its kind. | `gcc -c` → `cc_object`, `ar` → `cc_library`, a link → `cc_binary` |

   A copy needs no rule. Whoever reads it is given the original file at
   the copy's path.
5. **Replay every command that is left, alone.** Each one runs in a
   sandbox with only its recorded inputs and must leave files with the
   recorded hashes, twice. A command that fails is given more of the
   tree. If it still fails, the files it cannot reproduce are taken from
   the recording, and in the last resort all of them are.
6. **Lower to rules.** A command becomes one rule. The rule kinds are
   defined in `genrulify.bzl`, which ships in the workspace, and every
   one of them runs the recorded command through
   [`sandbox.sh`](src/sandbox.sh.in), which ships there too: it puts the
   inputs back at their recorded paths, runs the command, and compares
   every output with its recorded hash. So the kind decides how a rule
   reads and never what it does, and the workspace builds without
   genrulify.

## What is claimed

If every command passed its replay, then running the graph in any order
that respects its edges produces the recorded files, and so the recorded
NAR. The proof is an induction over that order, and it uses nothing else
about the scheduler: Bazel and Buck2 may run independent rules in
parallel, cache them or skip them.

A complete recording can supply the bytes for any command that fails
its replay check. The report counts how often that happened. What differs between
packages is how much of the build is run again.

The byte-for-byte guarantee applies to the recorded build. To work on
sources in your checkout, use the [project example](examples/project/README.md).

[Why it works](https://fzakaria.github.io/genrulify/theory.html) has the
proof, and [docs/theory.md](docs/theory.md) relates it to
[Build Systems à la Carte](https://doi.org/10.1145/3236774).

## Results

| Package | Build | `execve` | Groups | Run | Copies | Constants | Recorded | Rules |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `colstr-1.0.0` | stdenv, one `c++` | 83 | 54 | 4 | 2 | 1 | 0 | 5 |
| `sl-5.05` | Makefile | 94 | 59 | 6 | 3 | 1 | 0 | 7 |
| `tree-2.3.2` | Makefile | 183 | 92 | 18 | 3 | 4 | 0 | 20 |
| `cowsay-3.8.4` | Makefile, Perl | 147 | 94 | 4 | 5 | 9 | 0 | 6 |
| `hello-go` | `go build` | 192 | 55 | 3 | 2 | 2 | 0 | 4 |
| `python3.14-six-1.17.0` | `pyproject-build` | 137 | 107 | 3 | 2 | 2 | 0 | 5 |
| `ninja-1.13.2` | Python bootstrap, Ninja | 493 | 81 | 9 | 4 | 4 | 0 | 10 |
| `tinyxml2-11.0.0` | CMake | 275 | 81 | 6 | 2 | 3 | 0 | 7 |
| `jsoncpp-1.9.8` | CMake | 602 | 136 | 8 | 4 | 6 | 0 | 10 |
| `lz4-1.10.0` | patch, CMake | 806 | 195 | 39 | 6 | 8 | 0 | 43 |
| `zlib-1.3.2` | patches, `configure` | 700 | 202 | 39 | 20 | 12 | 0 | 42 |
| `lua-5.2.4` | Makefile | 386 | 145 | 50 | 9 | 5 | 0 | 52 |
| `patchelf-0.15.2` | autotools | 503 | 76 | 6 | 3 | 6 | 0 | 7 |
| `which-2.25` | patch, autotools | 1,137 | 98 | 15 | 3 | 3 | 0 | 18 |
| `hello-2.12.3` | autotools | 4,605 | 659 | 132 | 75 | 50 | 0 | 133 |
| `hello-2.12.3`, coarse cuts | autotools | 4,605 | 70 | 9 | 1 | 2 | 0 | 10 |
| `hello-2.12.3`, 4 jobs | autotools | 4,605 | 126 | 9 | 1 | 2 | 0 | 10 |

*Groups* are commands or pipelines. *Run* is how many of them a build
system runs again, each checked to leave the recorded bytes. *Copies*
and *Constants* are groups the passes replaced. *Recorded* is how many
had to be carried as recorded bytes, and everything else was dead. For
every package the NAR Bazel builds and the NAR Buck2 builds are the NAR
Rewind recorded.

The coarse row cuts hello only at the commands the builder runs, which
leaves `make` as one rule. The four-job row is a recording made with
`--cores 4`: commands that overlapped cannot be told apart, so they
merge.

## Use it

Recording needs [Rewind](https://rewindvm.dev) on the `PATH` and KVM.
Building the result needs Bazel or Buck2, bubblewrap and the Nix store.
CI pushes the compiler to `genrulify.cachix.org`, and the flake names
that cache, so `nix run` downloads it when you accept the flake's
configuration.

```console
$ nix run github:fzakaria/genrulify -- convert nixpkgs#hello hello
$ nix run github:fzakaria/genrulify -- convert --open cargo nixpkgs#hexyl hexyl
```

`convert` writes three things into the directory: `recording`, what was
observed; `graph.mlir`, the optimized graph; and `workspace`, which
`bazel build //:archive` or `buck2 build //:archive` turns into the NAR
of the output. `--open` names programs to take apart even though they
write files of their own, and `--coarse` cuts only at the commands the
builder runs.

The steps are commands too, for looking at one stage:

```console
$ genrulify record RUN /nix/store/…-hello-2.12.3.drv recording
$ genrulify check recording
$ genrulify import recording > recorded.mlir
$ genrulify opt --recording recording recorded.mlir dce fold > graph.mlir
$ genrulify emit recording graph.mlir workspace
```

## Limits

- **One build, one graph.** With `convert --source`, edits to known
  inputs can reuse the graph. Changes to commands or dependencies need a
  new recording.
- **Determinism is sampled.** Two passing replays are evidence that a
  command is a function of its inputs, not proof. Rules for a recorded
  workspace check their output hashes. Project rules accept new bytes so
  source edits can change the build.
- **A build has to be recordable.** Rewind runs it on one virtual CPU
  with at most 3 GiB of memory.
- **Record with one job.** Commands that overlap in time cannot be told
  apart by a snapshot and are merged into one rule.
- **The platform is an input.** Rules read the host's `/nix/store` and
  are marked local-only.

## Layout

```
docs/theory.md     what is assumed, what is checked, what follows
src/Splice.td      the dialect: entries and four operations
src/trace.cpp      events to cuts: which processes are commands
src/record.cpp     a Rewind run to a recording: processes and snapshots
src/import.cpp     a recording to file SSA
src/passes.cpp     link, fold, merge, raise, and the pipeline
src/rules.cpp      which kind of rule a command line is
src/check.cpp      the replay ladder
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

The snapshots, the descriptor tables and the determinism that make a
replay mean something all come from [Rewind](https://rewindvm.dev).

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

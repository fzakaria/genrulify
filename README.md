# genrulify

Any Nix build, as Bazel and Buck2 genrules.

**<https://fzakaria.github.io/genrulify/>**

**Documentation:** [the theory](docs/theory.md)

genrulify takes a build that [Rewind](https://rewindvm.dev) recorded and
compiles it into a `BUILD.bazel` and a `BUCK` file with one `genrule` per
process group. It has no rules for C, Rust, autotools or any other
toolchain, and it never reads the derivation's build script. The graph
comes from the trace: which processes ran, what each one found on disk,
and what each one left behind.

```console
$ tools/convert.sh nixpkgs#hello
70 process groups: 12 replayed and reproduced, 1 kept as recorded bytes, 57 dead
…
out: Nix, Bazel and Buck2 agree on sha256:cca3804d340495150db44b1e762655d8a0b4fe6d37b8350f1a0d8fa33c27c8cc
```

That hash is the NAR hash [cache.nixos.org](https://cache.nixos.org) serves
for `hello-2.12.3`. Bazel and Buck2 arrived at it by running `tar`,
`configure`, `make` and `strip` as genrules, and the `cmd` of each
genrule is the command line that was recorded:

```python
cmd = " ".join([
    "./sandbox.sh --in . --in $(RULEDIR) --out $(RULEDIR)",
    "--name s1375_tar --timeout 306",
    "run --cwd /build --env 12 --argv0 tar --fd 0=null --fd 1=err --fd 2=err --status 0 --argc 5 --",
    "/nix/store/d1x9…-gnutar-1.35/bin/tar xf /nix/store/wj7p…-hello-2.12.3.tar.gz --mode=+w --warning=no-timestamp",
]),
```

## The idea

A build system is a way to decide which processes to run and in what
order. Once a build has run, that decision is a fact you can observe. If
the observation is complete, every build system reduces to the same thing:
a list of `execve` calls and the files that flowed between them.

The catch has always been the observation. `strace` misses what happens
inside a process, races with the build, and cannot tell you what a
directory looked like a moment ago. Rewind runs the build in a
deterministic VM and can return to any step of it, so the question "what
was in `/build` when this compiler started" has an exact answer.

genrulify asks that question at the start and end of every process group
and turns the answers into single static assignment over files.

## How a build becomes a graph

### 1. Find the cuts

The builder is the first process the sandbox starts, usually `bash`
running stdenv's `setup.sh`. The builder and the subshells it forks are
*driver* code: `cd`, `export`, `$(...)`, redirections. None of that has a
command line to run again.

A *cut* is where a driver process calls `execve`. Everything from that
call until the process and its descendants exit is one unit of replay.
Cuts that share a pipe, or that were alive at the same time, are merged
into a *group*, because a snapshot cannot separate what they did.

`colstr-1.0.0`, a single-file C++ program from nixpkgs, runs 83 `execve`
calls in 62 cuts and 52 groups.

### 2. Snapshot around each group

genrulify asks Rewind for the contents of `/build` and of each output
path at the step a group starts and the step it ends. The difference
between the two is the group's effect: every file, directory and symlink
it created or changed, with mode and content hash.

Nothing is inferred from system calls, so `mkdir`, `chmod`, `rename` and
`ln -s` need no special handling. They are differences between two
states like any other write.

Whatever changed *between* groups was written by the builder itself, for
example `echo "$flags" > env-vars`. Those bytes have no `execve` behind
them, so they enter the graph as constants.

### 3. Assign versions

Each write produces a new SSA value for its path. A value is one version
of one filesystem entry. This derivation appends to a file in place:

```nix
${dd} if=${header} of=bundle status=none
${dd} if=${body} of=bundle oflag=append conv=notrunc status=none
${dd} if=bundle of="$out" status=none
```

and its graph, in genrulify's MLIR dialect, is:

```mlir
%0 = splice.literal "sandbox" at 717 -> [
    #splice.entry<"/build" dir 700 mtime 1>
]
%1 = splice.exec "dd" at 735(%0) -> [
    #splice.entry<"/build/bundle" file 644 mtime 1 "cb94740a…">
]
%2 = splice.exec "dd" at 764(%0, %1) -> [
    #splice.entry<"/build/bundle" file 644 mtime 1 "50f55425…">
]
%3 = splice.exec "dd" at 782(%0, %2) -> [
    #splice.entry<"/nix/store/l1dx8g9m…-genrulify-example" file 644 mtime 1 "50f55425…">
]
splice.output "out" "/nix/store/l1dx8g9m…-genrulify-example"(%3)
```

`/build/bundle` has two values. The append takes the first as an operand
and defines the second. A build system that requires every file to have
one producer is satisfied, because `%1` and `%2` are different artifacts
that happen to share a path inside the sandbox.

The dialect has three operations.

| Operation | Meaning |
| --- | --- |
| `splice.exec` | Run a recorded process group. Operands are the entry versions it is given, results are the versions it leaves. |
| `splice.literal` | Entries copied from the recording: the sandbox Nix prepared, the builder's own writes, or a group that could not be replayed. |
| `splice.output` | The final version of every path under a derivation output. |

### 4. Delete what the output does not need

`splice.exec` and `splice.literal` carry MLIR's `Pure` trait. That is a
claim about the build: a group's writes are a function of the entries it
was given and of the store closure. Under that claim an operation whose
results are never used is dead, and MLIR's stock canonicalizer removes
it. No genrulify-specific pass is involved.

For `colstr` this deletes 45 of 52 groups: every `date +%s`, every
`find` that only looked, `mktemp` and `rm` pairs, the compiler's
temporary files, and stdenv's `env-vars` dump.

### 5. Replay every survivor, alone

The `Pure` claim is checked, not assumed. `genrulify check` runs each
remaining group by itself in [bubblewrap](https://github.com/containers/bubblewrap):

- the store closure is mounted read-only at its original paths;
- the filesystem holds exactly the group's operands, taken from the
  recording, with their recorded modes and mtimes;
- each process gets its recorded program, `argv`, environment, working
  directory and file descriptors, and pipes between them are rebuilt;
- processes start in the order the recording allows: one starts only
  after every process that had exited before its `execve`.

The group passes if every process ends with its recorded status and every
result that something uses has the recorded kind, mode and SHA-256. The
whole pass is repeated until two in a row change nothing, so each
surviving group has reproduced the recording at least twice.

A group that fails moves down a ladder:

1. **Narrow.** Operands are the files the group opened or executed, the
   prior version of anything it modified, and the directories leading to
   them.
2. **Near.** Add every other entry of the directories the group worked
   in. `make` calls `stat` on `Makefile.am` without opening it, and
   Rewind's I/O log records opens, so `make` fails at narrow and passes
   here.
3. **Wide.** Operands are every entry that existed.
4. **Traced.** A group that passed at near or wide is replayed once more
   under `strace`. The trace has what Rewind's log lacks: every `stat`,
   `access`, `readlink` and directory listing. The group's operands are
   cut down to the entries the trace shows it touching, plus the contents
   of any directory it listed, and that smaller set is checked like any
   other. If it fails, the group keeps the wider set that worked.
5. **Pinned.** The group replays, and the specific results that differ
   are taken from the recording. `configure` writes the same `Makefile`
   every time and a different `config.log`.
6. **Literal.** The group is not replayed and all of its writes come from
   the recording.

One case needs no rung. `mktemp` picks a random name, and later commands
carry the recorded name in their `argv`. What a later step can observe
of a path is its kind, mode and content, so when a result is missing and
an entry the group was not given sits beside it with the recorded kind,
mode and content, the script takes that entry as the result and hands it
on under the recorded name.

### 6. Lower to genrules

Every file version becomes a real file in the build system's tree, named
after the rule that wrote it and its path in the build. A `splice.exec`
becomes a `genrule` whose `srcs` are the versions it reads, whose `outs`
are the versions it writes, and whose `cmd` is the recorded command
line. This is the append from step 3:

```python
genrule(
    name = "s764_dd",
    srcs = [
        ":actions/s764_dd.manifest",
        ":closure.txt",
        ":env/0.env",
        ":platform/bin/sh",
        ":platform/etc/group",
        ":platform/etc/hosts",
        ":platform/etc/passwd",
        ":sandbox.sh",
        ":s735_dd/build/bundle",
        ":s735_dd.ok",
    ],
    outs = [
        "s764_dd/build/bundle",
        "s764_dd.ok",
    ],
    cmd = " ".join([
        "./sandbox.sh --in . --in $(RULEDIR) --out $(RULEDIR)",
        "--name s764_dd --timeout 300",
        "run --cwd /build --env 0 --fd 0=null --fd 1=err --fd 2=err --status 0 --argc 6 --",
        "/nix/store/2gfx…-coreutils-9.11/bin/dd if=/nix/store/vr61…-body.txt of=bundle oflag=append conv=notrunc status=none",
    ]),
    tags = ["no-remote"],
)
```

It reads `%1` as `:s735_dd/build/bundle` and writes `%2` as
`s764_dd/build/bundle`. The SSA values of the module are the labels of
the build file.

A recorded command only makes sense where it ran. It names `/build`
absolutely and expects its environment and its descriptors.
[`sandbox.sh`](src/sandbox.sh.in) is a shell script that every workspace
carries, and it puts those back:

- it copies the declared inputs to their recorded paths under a private
  root, with the modes and mtimes in the rule's manifest;
- it mounts that root and the store paths in `closure.txt` with
  bubblewrap;
- it starts each process with its recorded environment (`env/0.env`),
  working directory, `argv[0]` and descriptors, which is what the `run`
  line above each command says;
- it compares the kind, mode and SHA-256 of every declared result with
  the manifest, and only then copies the result to `outs`.

A pipeline is one rule with a `run` line per process and a `--pipe` for
each pipe between them. Directories and symlinks are not files a genrule
can declare, so they travel in the manifests, and a rule that uses one
depends on the stamp (`s735_dd.ok`) of the rule that made it.

A `splice.literal` is files checked into the workspace. `splice.output`
stages the final entries and writes a NAR with `nix-store --dump`.

Three properties come out of this:

- The workspace builds without genrulify. Bazel or Buck2, bash,
  bubblewrap and the Nix store paths of the closure are enough.
- The checker in step 5 runs this script with these arguments, so the
  thing that was checked is the thing that was emitted.
- A rule verifies its results on every build. A process that stops
  reproducing its recorded output fails that rule and cannot pass a
  different file downstream.

Store paths are read from the host's Nix store, which a remote worker
may not have, so rules are tagged `no-remote`.

## Why the result is the same build

Let a recorded build have states *σ₀ … σₙ*, where a state maps paths to
entries. For a group *G* running from step *a* to step *b*, let *R* be its
operand paths and *W* the results that have a use.

**Check.** Running *G*'s processes over the platform with a filesystem
equal to *σₐ* restricted to *R* ends with the recorded statuses and
leaves *σ_b(p)* at every *p* in *W*.

**Claim.** If every `splice.exec` in a graph passes its check, and each
is a function of its staged inputs and the platform, then evaluating the
graph in any topological order produces, for every value, the recorded
version. The NAR of each output is the recorded NAR.

**Proof.** By induction over a topological order. A `splice.literal` is
the recorded bytes by construction. For a `splice.exec`, each operand
equals its recorded version by the induction hypothesis, so the staged
filesystem is *σₐ* restricted to *R*, the same one the check ran on. The
group is a function of that filesystem, so it leaves what it left during
the check, which was the recorded version. ∎

Two properties follow.

*The conversion is total.* Replace every group with a literal and every
check passes trivially, so a graph that satisfies the claim always
exists. Each step down the ladder strictly reduces what replay has to
reproduce, so `check` terminates.

*The scheduler is free.* The proof uses a topological order and nothing
else. Bazel and Buck2 may run independent rules in parallel, cache them,
or skip them, and the output is unchanged.

What varies between packages is how much of the build is computed and
how much is carried as recorded bytes. `check.json` reports it per group.

[docs/theory.md](docs/theory.md) has the definitions, both proofs, and
why dead-code elimination, opening and demotion each preserve the claim.

## Results

`tools/convert.sh` was run on 20 packages from the nixpkgs revision in
`flake.lock`, recorded with Rewind 0.16.0 on x86_64-linux. They have 45
outputs between them. Every conversion needed one build, and for every
output the NAR Bazel built and the NAR Buck2 built are the NAR Rewind
recorded.

| Package | Build | `execve` | Groups | Replayed | Narrow | Traced | Wider | Pinned files | Recorded | Genrules |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `colstr-1.0.0` | stdenv, one `c++` | 83 | 52 | 7 | 7 | 0 | 0 | 0 | 0 | 8 |
| `sl-5.05` | Makefile | 94 | 55 | 9 | 9 | 0 | 0 | 0 | 0 | 10 |
| `tree-2.3.2` | Makefile | 183 | 73 | 13 | 12 | 1 | 0 | 0 | 0 | 15 |
| `cowsay-3.8.4` | Makefile, Perl | 147 | 79 | 14 | 13 | 1 | 0 | 0 | 0 | 16 |
| `hello-go` | `go build` | 192 | 53 | 7 | 7 | 0 | 0 | 0 | 0 | 8 |
| `python3.14-six-1.17.0` | `pyproject-build` | 137 | 99 | 7 | 7 | 0 | 0 | 0 | 0 | 9 |
| `hexyl-0.17.0` | Rust, `cargo` | 612 | 79 | 18 | 16 | 2 | 0 | 1 | 0 | 19 |
| `ninja-1.13.2` | Python bootstrap, Ninja | 493 | 79 | 17 | 15 | 2 | 0 | 0 | 0 | 18 |
| `tinyxml2-11.0.0` | CMake | 275 | 54 | 10 | 7 | 3 | 0 | 2 | 0 | 11 |
| `jsoncpp-1.9.8` | CMake | 602 | 81 | 15 | 12 | 3 | 0 | 3 | 0 | 17 |
| `lz4-1.10.0` | patch, CMake | 806 | 105 | 20 | 17 | 3 | 0 | 2 | 0 | 24 |
| `zlib-1.3.2` | patches, `configure` | 700 | 87 | 15 | 13 | 1 | 1 | 1 | 0 | 18 |
| `patchelf-0.15.2` | autotools | 503 | 59 | 12 | 9 | 3 | 0 | 1 | 0 | 13 |
| `which-2.25` | patch, autotools | 1,137 | 82 | 13 | 10 | 3 | 0 | 1 | 0 | 16 |
| `hello-2.12.3` | autotools | 4,605 | 70 | 12 | 9 | 3 | 0 | 1 | 1 | 13 |
| `gawk-5.4.1` | autotools | 6,857 | 85 | 16 | 11 | 4 | 1 | 4 | 0 | 19 |
| `gnused-4.10` | autotools | 8,848 | 77 | 14 | 11 | 3 | 0 | 1 | 0 | 16 |
| `bash-interactive-5.3p15` | autotools, 6 outputs | 11,847 | 160 | 35 | 31 | 4 | 0 | 1 | 1 | 41 |
| `sqlite-3.53.3`, tests off | `configure`, 6 outputs | 1,121 | 149 | 27 | 24 | 3 | 0 | 1 | 1 | 33 |
| `lua-5.2.4`, `make` opened | Makefile | 386 | 130 | 67 | 65 | 2 | 0 | 0 | 0 | 69 |
| `hello-2.12.3`, `make` opened | autotools | 4,605 | 445 | 165 | 164 | 1 | 0 | 0 | 0 | 166 |
| `hello-2.12.3`, `make` opened, 4 jobs | autotools | 4,605 | 233 | 18 | 16 | 2 | 0 | 1 | 0 | 19 |

*Replayed* groups reproduced their recorded writes in two consecutive
passes. *Narrow* groups did so with the files Rewind saw them open.
*Traced* groups needed more and were cut back to what a trace of their
replay shows them touching: `configure` in `hello` takes 143 of the 555
entries in its tree, in `patchelf` 23 of 108, in `gnused` 143 of 1,364.
*Wider* groups kept their directories or the whole tree because the
traced set did not reproduce the recording. There are two, `make
install` in `gawk` and a `make` in `zlib`. Everything not replayed or
recorded is dead.

*Pinned files* are what a replayed group wrote differently from the
recording. Most are logs (`config.log`, `CMakeConfigureLog.yaml`,
`test-suite.log`) or records of the machine: `CMakeSystem.cmake` holds
the kernel version and cargo's `.rustc_info.json` the host. A pinned file
comes from the recording, so the steps after it see what they saw when
the build was recorded.

*Recorded* counts the groups that are not replayed but still have a file
in the graph. There are three. One `find` in `bash` and one in `sqlite`
write a checksum file differently each time. In `hello` it is `make
check`: the only thing of it that the install step looks at is
`test-suite.log`, which never matches and is pinned, so the tests have
nothing left to contribute and Bazel and Buck2 do not run them.

A literal does not mean the output came from the recording. Across the
45 outputs, 2,375 files, directories and symlinks were written by
replayed rules and 10 were not. All 10 are small files the builder's
shell wrote with a redirection, such as
`nix-support/propagated-build-inputs`. No output entry comes from a
process group that failed to replay.

The last row is the same build recorded with `--cores 4`. Recipes that
ran at the same time cannot be separated by a snapshot, so 205 of them
merge into two groups, which replay and reproduce. The conversion stays
correct and gets coarser: 19 rules where the one-job recording gives
166.

Rewind also compares what it built with
[cache.nixos.org](https://cache.nixos.org). `sqlite` was built with its
tests off, which is a different derivation, so it has nothing to compare
with. Of the 39 outputs of the other 19 packages, 37 are bit-identical
to what Hydra built, so for those the Bazel and Buck2
archives equal the public binary cache. The other two, one output each of
`six` and `gnused`, differ between Hydra and this machine under plain Nix
as well. genrulify reproduces the recorded build, and that is the build
it was compared with.

Two things are worth knowing about how those numbers came about.

`gnused` failed once, in an earlier round. Its `configure` passed both
replay checks and then wrote a different `config.h` inside Bazel's
sandbox while four other conversions were loading the machine. The failing rule named the files,
which is what `genrulify learn` and the retry loop in `tools/convert.sh`
are for: a rule that fails to reproduce during a build is pinned from
the build log and the graph is built again. On a quiet machine the same
package converts in one build with only `config.log` pinned.

`hexyl` first passed for the wrong reason. stdenv installs the binary
with a here-string piped into `xargs cp`, that group was a literal, and
so the installed binary came from the recording and `rustc` never ran.
Capturing the pipe's contents made `xargs` replayable, which made the
`cargo build` live, and it reproduces the recorded binary. A conversion
that verifies is only as real as its replayed share, which is why the
table reports it.

`sqlite` is in the table with its test phase disabled. The compile of
its single 9 MB source file thrashed in Rewind's default 1 GiB guest
and needed `REWIND_ARGS='--mem 3072'`. With that, the build recorded in
ten minutes, but sqlite's own test runner then estimated 44 hours for
its suite on one virtual CPU. What limits a conversion in practice is
how long the build takes to record.

## Finer cuts

By default a whole `make` invocation is one cut, so `hello`'s build phase
is a single genrule. Name a program to *open* and its processes become
driver code, which moves the cuts down to its children:

```console
$ tools/convert.sh nixpkgs#hello make
445 process groups: 164 replayed and reproduced, 1 kept as recorded bytes, 280 dead
```

A shell is opened along with the program it leads to, so the recipe that
runs `make all-recursive` does not hide the inner `make`. A recipe shell
with nothing opened below it stays one cut and is replayed whole, with
its pipelines and builtins. Programs that are not shells are never opened
on account of what they run: `configure` runs `make` to test it and is
still one group.

The rule that compiles `src/hello.c` then reads like the line `make`
printed:

```python
genrule(
    name = "s95217_bash",
    srcs = [
        ":s1375_tar/build/hello-2.12.3/src/hello.c",
        ":s1375_tar/build/hello-2.12.3/src/system.h",
        ":s4163_configure/build/hello-2.12.3/config.h",
        ":s92510_bash/build/hello-2.12.3/lib/sys/types.h",
        …
    ],
    outs = [
        "s95217_bash/build/hello-2.12.3/src/hello.o",
        "s95217_bash.ok",
    ],
    cmd = " ".join([
        "./sandbox.sh --in . --in $(RULEDIR) --out $(RULEDIR)",
        "--name s95217_bash --timeout 300",
        "run --cwd /build/hello-2.12.3 --env 40 --fd 0=null --fd 1=err --fd 2=err --status 0 --argc 3 --",
        "/nix/store/10dx…-bash-5.3p15/bin/bash -c 'gcc -DLOCALEDIR=\\\"/nix/store/5z2y…-hello-2.12.3/share/locale\\\" -DHAVE_CONFIG_H -I.  -Ilib -I./lib -Isrc -I./src   -g -O2 -c -o src/hello.o src/hello.c'",
    ]),
    tags = ["no-remote"],
)
```

Its sources are `hello.c` and `system.h` as `tar` unpacked them,
`config.h` as `configure` wrote it, and the gnulib headers that earlier
recipes generated. Nobody declared those. The compiler opened them
while Rewind was watching.

The 13 genrules become 166. Every group except `configure` passes on
the narrow rung: a compile is given its source, the headers it opened,
and nothing else. Nix ran this build with `make -j1`. Bazel and Buck2
run the compiles in parallel, because the graph says they are
independent.

## Limits

These are properties of the current implementation, and each one is
either checked or reported.

- **One build, one graph.** The graph recomputes the build that was
  recorded. Editing a source file is a different build, and the narrow
  operand sets say nothing about which unopened files would have
  mattered. Re-record after a change.
- **A check is a sample.** `check` replays every group twice. A process
  that is nondeterministic and still matched both times will be caught by
  its own genrule on a later build, which fails. It is not caught
  earlier.
- **A build has to be recordable.** Rewind runs the build on one virtual
  CPU with at most 3 GiB of memory. A build that needs more of either
  cannot be recorded, or takes days.
- **Record with one job for fine rules.** Rewind's default tells the
  build it has one core, so `make` and `ninja` run one command at a time
  and each command can be its own group. A recording made with
  `--cores N` interleaves commands, and commands that overlap are merged
  into one group. The last row of the results table is such a recording.
- **`stat` and directory listings are not in Rewind's trace.** genrulify
  recovers them by tracing the replay, which costs one extra replay of
  each group that needed them. A path reached only through a symlinked
  directory, or named relative to the working directory by one of the
  older system calls, is not resolved, and a group that depends on one
  keeps its wider inputs.
- **Pipe input is captured at the `execve` only.** A here-string or a
  short here-document is already in the pipe when the command starts, and
  is recorded and fed back. Input the shell writes while the command is
  running is not, and such a command becomes a literal.
- **The platform is part of the input.** Rules read the host's
  `/nix/store` and are marked local-only. The kernel and CPU are whatever
  the host has.
- **Not captured:** FIFOs, sockets and devices inside the build tree,
  writes under `/tmp`, sub-second mtimes, and directory mtimes.

## Layout

```
docs/theory.md     what is assumed, what is checked, what follows
src/Splice.td      the dialect: entry type, entry attribute, three operations
src/trace.cpp      events and I/O log to cuts
src/record.cpp     Rewind run to a recording: process details and snapshots
src/import.cpp     recording to file SSA
src/check.cpp      the replay ladder
src/action.cpp     one operation as a step: inputs, command lines, results
src/sandbox.sh.in  the script a genrule runs, shipped in every workspace
src/emit.cpp       graph to BUILD.bazel and BUCK
tests/             CLI tests over a checked-in recording
tools/convert.sh   record, check, emit, build with both systems, verify
```

## Running

```console
$ nix build                 # compiler, with tests
$ nix develop               # MLIR, TableGen, CMake
$ nix flake check
```

Recording needs Rewind and KVM. Everything after `genrulify record` works
from the recording directory alone:

```console
$ genrulify record RUN /nix/store/…-hello-2.12.3.drv recording
$ genrulify check recording
$ genrulify import recording > recorded.mlir
$ genrulify opt recorded.mlir > graph.mlir
$ genrulify emit recording graph.mlir workspace
$ cd workspace && bazel build //:archive
```

The workspace is self-contained apart from the store paths its commands
name. Building it needs Bazel or Buck2 and no genrulify.

## Credits

The model of tasks, a store, schedulers and rebuilders is from
[Build Systems à la Carte](https://doi.org/10.1145/3236774) by Mokhov,
Mitchell and Peyton Jones. In its terms a recording turns a build's
monadic tasks into applicative ones and is itself a constructive trace;
[docs/theory.md](docs/theory.md) spells out the correspondence.

[Riker](https://www.usenix.org/conference/atc22/presentation/curtsinger)
(Curtsinger and Barowy, USENIX ATC 2022) builds from a trace of commands
and their filesystem effects, and its TraceIR is the closest relative of
this dialect. Memoize and Fabricate used `strace` to discover
dependencies after the fact. The snapshots, the fd tables
and the determinism that make the replay check meaningful all come from
[Rewind](https://rewindvm.dev).

## License

MIT, please see [LICENSE](LICENSE).

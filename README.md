# nixsplice

Any Nix build, as Bazel and Buck2 genrules.

nixsplice takes a build that [Rewind](https://rewindvm.dev) recorded and
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
`configure`, `make` and `strip` as genrules.

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

nixsplice asks that question at the start and end of every process group
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

nixsplice asks Rewind for the contents of `/build` and of each output
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

and its graph, in nixsplice's MLIR dialect, is:

```mlir
%0 = splice.literal "sandbox" at 717 -> [
    #splice.entry<"/build" dir 700 mtime 1>
]
%1 = splice.exec "dd" at 735(%0) -> [
    #splice.entry<"/build/bundle" file 644 mtime 1 "b08e44f9…">
]
%2 = splice.exec "dd" at 764(%0, %1) -> [
    #splice.entry<"/build/bundle" file 644 mtime 1 "c82437d3…">
]
%3 = splice.exec "dd" at 782(%0, %2) -> [
    #splice.entry<"/nix/store/r8mgc2hq…-nixsplice-example" file 644 mtime 1 "c82437d3…">
]
splice.output "out" "/nix/store/r8mgc2hq…-nixsplice-example"(%3)
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
it. No nixsplice-specific pass is involved.

For `colstr` this deletes 45 of 52 groups: every `date +%s`, every
`find` that only looked, `mktemp` and `rm` pairs, the compiler's
temporary files, and stdenv's `env-vars` dump.

### 5. Replay every survivor, alone

The `Pure` claim is checked, not assumed. `nixsplice check` runs each
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
2. **Wide.** Operands are every entry that existed. `make` needs this: it
   calls `stat` on `Makefile.am` without opening it, and Rewind's I/O log
   records opens.
3. **Pinned.** The group replays, and the specific results that differ
   are taken from the recording. `configure` writes the same `Makefile`
   every time and a different `config.log`.
4. **Literal.** The group is not replayed and all of its writes come from
   the recording. `mktemp` lands here: the file name is random and later
   commands carry the recorded name in their `argv`.

### 6. Lower to genrules

Each `splice.exec` becomes a `genrule` whose command is
`nixsplice replay plan.json`. Its output is a *layer*: the entries it
wrote, with full modes, as one file. Its `srcs` are the layers of the
operations that define its operands. Each `splice.literal` is a layer
checked into the workspace. `splice.output` assembles the final entries
and writes a NAR.

A replay rule verifies its results on every build, so a process that
stops reproducing its recorded output fails that rule. It cannot pass a
different file downstream.

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

`tools/convert.sh` was run on 19 packages from the nixpkgs revision in
`flake.lock`, recorded with Rewind 0.16.0 on x86_64-linux. They have 39
outputs between them. For every output, the NAR Bazel built and the NAR
Buck2 built are the NAR Rewind recorded.

| Package | Build | `execve` | Groups | Replayed | Wide | Pinned files | Literal | Genrules |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `colstr-1.0.0` | stdenv, one `c++` | 83 | 52 | 6 | 0 | 0 | 1 | 7 |
| `sl-5.05` | Makefile | 94 | 55 | 8 | 0 | 0 | 1 | 9 |
| `tree-2.3.2` | Makefile | 183 | 73 | 12 | 1 | 0 | 1 | 14 |
| `cowsay-3.8.4` | Makefile, Perl | 147 | 79 | 14 | 1 | 0 | 0 | 16 |
| `hello-go` | `go build` | 192 | 53 | 6 | 0 | 0 | 1 | 7 |
| `python3.14-six-1.17.0` | `pyproject-build` | 137 | 99 | 7 | 0 | 0 | 0 | 9 |
| `hexyl-0.17.0` | Rust, `cargo` | 612 | 79 | 19 | 5 | 7 | 1 | 20 |
| `ninja-1.13.2` | Python bootstrap, Ninja | 493 | 79 | 16 | 2 | 2 | 1 | 17 |
| `tinyxml2-11.0.0` | CMake | 275 | 54 | 9 | 3 | 2 | 1 | 10 |
| `jsoncpp-1.9.8` | CMake | 602 | 81 | 14 | 3 | 3 | 1 | 16 |
| `lz4-1.10.0` | patch, CMake | 806 | 105 | 18 | 3 | 2 | 2 | 22 |
| `zlib-1.3.2` | patches, `configure` | 700 | 87 | 13 | 2 | 1 | 2 | 16 |
| `patchelf-0.15.2` | autotools | 503 | 59 | 11 | 3 | 1 | 1 | 12 |
| `which-2.25` | patch, autotools | 1,137 | 82 | 12 | 3 | 1 | 1 | 15 |
| `hello-2.12.3` | autotools | 4,605 | 70 | 12 | 4 | 2 | 1 | 13 |
| `gawk-5.4.1` | autotools | 6,857 | 85 | 15 | 5 | 4 | 1 | 18 |
| `gnused-4.10` | autotools | 8,848 | 77 | 13 | 3 | 1 | 1 | 15 |
| `bash-interactive-5.3p15` | autotools, 6 outputs | 11,847 | 160 | 34 | 4 | 1 | 2 | 40 |
| `lua-5.2.4`, `make` opened | Makefile | 386 | 130 | 68 | 2 | 0 | 1 | 70 |
| `hello-2.12.3`, `make` opened | autotools | 4,605 | 445 | 164 | 1 | 1 | 1 | 165 |

*Replayed* groups reproduced their recorded writes in two consecutive
passes. *Wide* counts the replayed groups that needed the whole tree,
mostly `configure`, `cmake`, `cargo` and unopened `make`. Everything not
replayed or literal is dead.

*Pinned files* are what a replayed group wrote differently from the
recording. Most are logs (`config.log`, `CMakeConfigureLog.yaml`,
`test-suite.log`) or records of the machine: `CMakeSystem.cmake` holds
the kernel version and cargo's `.rustc_info.json` the host. A pinned file
comes from the recording, so the steps after it see what they saw when
the build was recorded.

The literals are `mktemp`, whose random name later commands carry in
their `argv`, and in `bash` one `find` that writes a checksum file
differently each time.

Rewind also compares what it built with
[cache.nixos.org](https://cache.nixos.org). 37 of the 39 outputs are
bit-identical to what Hydra built, so for those the Bazel and Buck2
archives equal the public binary cache. The other two, one output each of
`six` and `gnused`, differ between Hydra and this machine under plain Nix
as well. nixsplice reproduces the recorded build, and that is the build
it was compared with.

Two things are worth knowing about how those numbers came about.

`gnused` failed once. Its `configure` passed both replay checks and then
wrote a different `config.h` inside Bazel's sandbox while four other
conversions were loading the machine. The failing rule named the files,
which is what `nixsplice learn` and the retry loop in `tools/convert.sh`
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

`sqlite` is not in the table: Rewind had not finished recording its
single-file compile after 36 minutes and I stopped it.

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

The 13 genrules become 165. 155 of them are individual `make` recipes,
84 of which run `gcc`, and every group except `configure` passes on the
narrow rung: a compile is given its source, the headers it opened, and
nothing else. Nix ran this build with `make -j1`. Bazel and Buck2 run the
compiles in parallel, because the graph says they are independent. Three
clean Bazel builds of each workspace averaged 17.3 s with 13 rules and
14.9 s with 165, with critical paths of 15.9 s and 12.7 s.

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
- **Record with one job.** Rewind's default tells the build it has one
  core, so `make` and `ninja` run one command at a time and each command
  can be its own group. A recording made with `--cores N` interleaves
  commands, and commands that overlap are merged into one group, which is
  still correct and coarser. The emitted graph is parallel either way.
- **`stat` and directory listings are not in the trace.** Groups that
  depend on them fall to the wide rung, where they are correct and
  coarse. Recording lookups in Rewind would let them stay narrow.
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
src/replay.cpp     the sandbox a genrule runs
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

Recording needs Rewind and KVM. Everything after `nixsplice record` works
from the recording directory alone:

```console
$ nixsplice record RUN /nix/store/…-hello-2.12.3.drv recording
$ nixsplice check recording
$ nixsplice import recording > recorded.mlir
$ nixsplice opt recorded.mlir > graph.mlir
$ nixsplice emit recording graph.mlir workspace
$ cd workspace && bazel build //:archive
```

## Credits

[Riker](https://www.usenix.org/conference/atc22/presentation/curtsinger)
(Curtsinger and Barowy, USENIX ATC 2022) builds from a trace of commands
and their filesystem effects, and its TraceIR is the closest relative of
this dialect. Memoize and Fabricate used `strace` to discover
dependencies after the fact. The snapshots, the fd tables
and the determinism that make the replay check meaningful all come from
[Rewind](https://rewindvm.dev).

## License

MIT, please see [LICENSE](LICENSE).

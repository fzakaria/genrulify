# Why a recorded build converts

When GCC builds GNU hello, it reads `hello.c` from the unpacked tarball
and `config.h` from an earlier run of `configure`. A recording tells us
which version of each file GCC saw. If we give GCC those versions again,
can we reproduce `hello.o`? genrulify tries the compile in a sandbox
and compares the result with the recording. The same check works for any
command in the build.

## What a reliable trace provides

[Rewind](https://rewindvm.dev) can return to a step in a build and read
the guest filesystem. genrulify uses the following information from that run.

- The state $\sigma_t$ of `/build` and the output paths at step $t$.
  Each path names a file with bytes, mode and modification time, a
  directory with a mode, or a symlink with a target.
- The program, arguments, environment, working directory and open file
  descriptors at each `execve`, including bytes waiting in input pipes.
- When processes forked, ran a program and exited, and their exit statuses.
- The platform $P$ shared by the recording and the replay, including
  the Nix store closure, user identity and kernel behavior.

## The graph

A *group* is a unit we can replay, such as a compile or a whole pipeline.
Commands that overlap have to share a group because the snapshots show
their combined changes. Commands joined by a pipe share a group as well,
since no snapshot shows what passed through the pipe. A job server's
pipe, where every command holds both ends, joins only the commands that
ran at the same time. A program like `make` that only starts commands
can be opened up so its children get separate rules. The builder and
other processes outside the groups form the *driver*. The graph takes
their writes from the recording.

For a group $G$ that starts at step $a$ and ends at step $b$, let $R(G)$
be the paths we give it at the start. The set of changed paths comes
from comparing the two snapshots.

$$
W(G) = \{p \mid \sigma_b(p) \ne \sigma_a(p)\}.
$$

Each change creates a new version of the path. If one command writes
`bundle` and another appends to it, the second command reads the first
version and produces the second. Both versions have their own name in
the graph. This is single static assignment (SSA) for files. Every edge
points from a producer to a later consumer.

## The local check

Suppose a group creates an object file and a scratch file. If the
linker reads the object and nothing uses the scratch file, only the
object needs to match. Call the set of changed paths whose versions are
used $U(G)$. A final output counts as a use.

The checker (`genrulify check`, also run by `convert`) tests the input
set chosen for each group. It stages those inputs at their original paths, restores
the group's commands and pipes, and runs the group. The processes must
exit with their recorded statuses. Each used result must yield the
recorded entry.

$$
\operatorname{run}_P(G,\sigma_a|_{R(G)})(p) = \sigma_b(p)
\qquad (p \in U(G)).
$$

Read the equation as a comparison at one output path $p$. The left side
runs the group on platform $P$ with the recorded entries at its input
paths, then looks up $p$ in the result. The right side looks up the same
path in the recording after the group finished. For the example's second
`dd`, the input is the one-line `bundle` in `/build` and both sides must
give the two-line file at `/build/bundle`. The checker makes this
comparison for every used result, along with the process status checks.

A result can come from a fresh temporary name.
If the recorded name is missing, genrulify can match a new entry in the
same directory by kind, mode and content. The next group receives that
entry at the recorded path.

## Composition

If every replayed group passes the local check, and running a group on
the same inputs and platform always gives the same result, the whole
graph reproduces the recorded output.

To see why, follow the graph in any order that builds inputs before
their consumers. A literal starts with its recorded entry. A copy takes
bytes from an earlier version with matching content and applies the
recorded metadata. When we reach a command, its inputs therefore match
the ones used in its check. By assumption it produces the same results
again. This argument applies at each step, so the final entries pack
into the same Nix archive (NAR). Independent rules can run in parallel.

## Totality

What if a group fails the check? genrulify can give it more inputs,
up to the whole recorded tree. For example, a command might list a
directory without opening every file in it. Tracing the replay can then
narrow the inputs to the entries the command actually used.

If a result still differs, the checker can *pin* it, taking that entry
from the recording while replaying the other results. If nothing useful
can be replayed, the graph uses the recorded entries for the whole group.

Every complete recording therefore has a graph that reproduces its
output. In the worst case, every entry is a literal. The checker has
finitely many input sets to try and each pin leaves fewer results to
reproduce. It finishes with two clean replay passes. The report in
`check.json` shows which groups ran and which needed recorded bytes.

## Rewrites

The optimizer can skip work when it already knows the entry a consumer
will receive.

| Pass | Example | Why it works |
| --- | --- | --- |
| Eliminate dead code | A scratch file nobody reads | Removing its producer changes no used value if all its results are unused. |
| Fold constants | `mkdir` | A directory, symlink or empty file is fully described by its recorded entry. |
| Merge or raise | `chmod` or `cp` | The result has the bytes of an input at the same path (merge) or a different path (raise). A copy carries the new metadata. |
| Link driver writes | A build driver puts a binary in its final location | A driver entry with the same bytes as an earlier file can refer to that file. Linking before dead-code elimination keeps its producer in the graph. |
| Unpack sources | `cp -r $src` and `chmod -R u+w` in a project | The command lines name the file each result came from, and the recording agrees on its bytes. |

## The mapping to build rules

| Graph operation | What the generated workspace does |
| --- | --- |
| `splice.literal` | Supplies a recorded entry. |
| `splice.copy` | Stages the original bytes at the copy's path with its metadata. |
| `splice.exec` | Runs a rule with the group's inputs, recorded command lines and used outputs. |
| `splice.output` | Packs the final entries for one derivation output into a NAR. |

Each command rule calls `sandbox.sh`, the same script used for the local
check. A regular file is a build artifact. For a directory or symlink,
the rule reads the entry from its manifest and depends on a stamp from
the producer. The producer writes the stamp after its check passes.

## Scope of the proof

Two successful replays sample determinism. A recorded workspace checks
the results each time its command runs, so later drift can fail a build.
The rules rely on the host's store paths, kernel and CPU. Snapshots omit
some observable state, including `/tmp`, sockets and sub-second
timestamps. Pipe data from the driver is captured at `execve`. A group
that needs missing state may have to use recorded bytes.

The proof covers the inputs from one run. With `convert --source`, rules
read files from the checkout. A command that depends on them is trusted:
its rule takes the bytes the command now leaves. A command that reads no
source is still held to the recording. An edit to `greet.c` can reuse the
recorded commands. Adding a dependency or changing a command calls for a
new recording.

In such a graph no pass rewrites a command that read a source for the
bytes it left. A result with the bytes of an input, an empty file and a
directory were each computed from the source as it was that day. The
copy of the source tree into `/build` and the `chmod` after it are
rewritten for another reason, which is what their command lines say:
each file they leave is a copy of the checkout's file.

A trusted rule writes down the modes and symlink targets it left, and
the rules that read its results stage those. The rule fails if a result
is missing, or if the command leaves an entry under an output path that
the recording did not have. A file the builder wrote itself is not tied
to a source with the same bytes. It stays recorded bytes, and `check`
lists every recorded file the project still uses, since an edit will not
change those. The same goes for what the builder did with a source
itself: which commands it ran and the arguments it gave them.
[Try the C project](../examples/project/README.md).

## In the terms of Build Systems à la Carte

In [Build Systems à la Carte](https://doi.org/10.1145/3236774), Mokhov,
Mitchell and Peyton Jones call a computation a *task* and each value it
requests a *key*. Here a task is a group and a key names one version of
one path. A compiler discovers headers as it reads its source. For the
recorded run those choices are known, so we can list them in the build
rule. In the paper's terms, recording turns a task with dynamic
(*monadic*) dependencies into one with static (*applicative*) dependencies.

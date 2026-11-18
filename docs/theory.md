# Why a recorded build converts

This note states what genrulify assumes about a trace, what it checks, and
what follows. The claim is narrow and exact: the emitted graph recomputes
the recorded build, under any scheduler, and the conversion exists for
every recording.

## In the terms of Build Systems à la Carte

[Build Systems à la Carte](https://doi.org/10.1145/3236774) (Mokhov,
Mitchell and Peyton Jones, ICFP 2018) describes a build system as a
*scheduler* and a *rebuilder* over a set of *tasks* that read and write a
*store* of keys and values. This note uses the same model, and the
correspondence is direct.

| The paper | Here |
| --- | --- |
| key | one version of one path, an SSA value |
| value | an entry: a file's bytes and mode, a directory, a symlink |
| store | the filesystem state *σ* |
| task | a process group, `splice.exec` |
| input key | a `splice.literal` |
| constructive trace | the recording: every key's dependencies and its value |

The paper separates tasks by how they find their dependencies.
*Applicative* tasks have static dependencies, known before the task
runs. *Monadic* tasks have dynamic ones, each learned from the value of
the last. A real build's tasks are monadic: a compiler learns which
headers it needs by reading its source.

A recording is one run of those monadic tasks with every choice already
made. For that run the dependencies of each task are a fixed set, so the
graph genrulify builds has only applicative tasks. That is the reason
its dependencies can be written down as a rule's `srcs`, and the reason
any scheduler will do.

The paper calls a build correct when every key's value in the final
store equals the result of running its task against that store. The
check below establishes that for one task at a time, against the
recorded store, and Theorem 1 composes the checks. In the paper's
classification Nix pairs a suspending scheduler with deep constructive
traces and Bazel a restarting scheduler with constructive traces.
genrulify keeps Nix's tasks and changes the other two.

## What a reliable trace provides

Fix one run of one derivation's builder. Number its observable moments
*t₀ < t₁ < … < tₙ*, from the builder's `execve` to its exit.

- **T1, states.** For every step *t* the trace gives *σₜ*, a finite map
  from paths under the writable roots to *entries*. An entry is a regular
  file with bytes, mode and mtime, a directory with a mode, or a symlink
  with a target. The writable roots are `/build` and the output paths.
- **T2, images.** For every `execve` the trace gives the step, the
  program the kernel loaded, `argv`, the environment, the working
  directory, the descriptor table, and the bytes waiting in any pipe the
  process inherited for reading.
- **T3, lifetimes.** Every fork, `execve` and exit, with pid, parent and
  wait status, ordered by step.
- **T4, platform.** Everything a process can observe that the build
  cannot change: the store closure, `/bin`, `/etc`, the user, the
  hostname, the kernel. Call it *P*.

Rewind supplies T1 by returning to a step and reading the guest
filesystem, T2 from `/proc` at that step, and T3 from its event stream.
Nix's sandbox and the Rewind guest fix T4.

A system-call tracer supplies T2 and T3 and can only approximate T1. T1
is what removes the need to model system calls at all.

## Units

A *cut* is an `execve` by a process, together with everything that
process and its descendants do until they have all exited. A *group* is a
set of cuts with an interval of steps *[a, b]* that contains them.

A *partition* of the run is a sequence of groups with disjoint intervals,
in step order. The steps not covered by any group belong to the *driver*:
the builder and whatever other processes were chosen not to be replayed.

Any partition is admissible. The coarsest useful one has a handful of
groups per build phase. The finest has one group per leaf `execve`.
Choosing a partition decides granularity. It never decides correctness.

What a partition does decide is how much is computed. Everything the
driver writes is a literal, so a process moved into the driver takes its
own writes out of what the graph recomputes. genrulify therefore moves a
process into the driver only when the event stream shows that the
process ran other commands and neither opened a file for writing nor
renamed one. Such a process wrote nothing to lose: `make`, a recipe
shell that runs one command, `xargs`. A process that writes, such as
`configure`, stays a group. A program can still be named to be opened
regardless, which is how `cargo` and `ninja` are taken apart.

## The graph

For a group *G* with interval *[a, b]*:

- its **effect** *W(G)* is the set of entries *σ_b(p)* over the paths *p*
  where *σ_b(p) ≠ σ_a(p)*;
- its **operands** *R(G)* are a chosen subset of the paths of *σ_a*.

For the driver steps between two groups, the effect is defined the same
way between the earlier group's end and the later group's start.

The graph has one value for every entry in every effect. Its nodes are:

- `splice.exec` for a group *G*: operands are the values that hold
  *σ_a(p)* for *p ∈ R(G)*, results are the values of *W(G)*;
- `splice.literal` for a driver effect, or for a group that is not
  replayed: no operands, results are the recorded entries themselves;
- `splice.copy` for a file entry whose content is the content of a value
  that already exists: one operand, that value, and one result;
- `splice.output` for an output path: operands are the values that hold
  *σₙ(p)* for every *p* under the path.

Every operand's definition lies at an earlier step, so the graph is
acyclic. A path written *k* times has *k* values, which is how an
in-place update becomes an ordinary edge.

## The local check

Write *run(G, F)* for the result of starting *G*'s processes over *P*
with a writable filesystem that is exactly *F*: recorded images, recorded
descriptor tables with their pipes rebuilt, and each process started once
every process that had exited before its `execve` has exited.

> **Check(G).** *run(G, σ_a restricted to R(G))* ends with every process
> at its recorded wait status and leaves *σ_b(p)* at every path *p* whose
> value has a use.

`genrulify check` performs this for each `splice.exec`, twice.

The check is on the value a group yields for *p*, which need not be the
path the process wrote. `mktemp` leaves its file under a name that
differs on every run. When nothing is at *p* and an entry the group was
not given sits in the same directory with the kind, mode and content of
*σ_b(p)*, that entry is the value for *p*. Later groups are staged from
values, at the recorded paths, so they see *σ_b(p)* at *p* either way.

## Composition

> **Theorem 1.** Suppose every `splice.exec` in a graph satisfies its
> check and *run(G, F)* is a function of *F* and *P*. Then evaluating the
> graph in any topological order gives every used value its recorded
> entry. In particular each output's NAR is the recorded NAR.

*Proof.* Induct along a topological order. A `splice.literal` yields
recorded entries by definition. A `splice.copy` of a value *u* is built
only when its recorded entry and *u*'s have the same content. By the
induction hypothesis *u* holds its recorded entry, so the copy has the
recorded content, and its path, mode and mtime are the recorded ones by
construction. For a `splice.exec` of *G*, every operand
is a used value defined earlier in the order, so by the induction
hypothesis it holds *σ_a(p)*. The filesystem staged for *G* is therefore
*σ_a* restricted to *R(G)*, the filesystem the check ran on. *run* is a
function of that filesystem and *P*, so it produces what it produced
during the check, which was *σ_b(p)* for every used result. A
`splice.output` collects values that hold *σₙ(p)*, and a NAR is a
function of those entries. ∎

The proof uses nothing about the scheduler beyond a topological order.
Bazel and Buck2 are free to run independent rules in parallel, to cache
them and to skip them.

## Totality

> **Theorem 2.** For every recording there is a graph in which every
> `splice.exec` satisfies its check, and `genrulify check` finds one.

*Proof.* The graph with no `splice.exec` at all, every effect a literal,
satisfies the hypothesis vacuously, so one exists. `check` only ever
moves a group down the ladder narrow → near → wide → pinned → literal.
Widening happens at most twice per group, and narrowing from a trace is
attempted once per group and undone if it fails. Pinning strictly shrinks the set of
results the group must reproduce. A group whose used results are all
pinned has no use left and is deleted. Each move is therefore bounded, the
number of groups is finite, and the procedure stops at a graph where
every remaining `splice.exec` passed. ∎

So a conversion never fails. What differs between builds is how much of
the output is computed and how much is carried as recorded bytes, and
`check.json` reports that per group.

## Rewrites

> **Corollary.** Any two graphs of the same recording that satisfy
> Theorem 1's hypothesis compute the same outputs.

Both compute the recorded outputs. A transformation is therefore sound
exactly when the graph it produces still satisfies the hypothesis: every
`splice.exec` passes its check, and every `splice.copy` has the content
of its operand. These are in use:

- *Dead-code elimination.* Deleting a node with no used result changes no
  other node's operands, so every remaining check still holds as it
  stood. This is why the operations may carry MLIR's `Pure` trait and be
  cleaned up by the stock canonicalizer.
- *Opening.* Moving a program from the groups into the driver refines the
  partition. The new, smaller groups are checked like any others.
- *Demotion.* Replacing a `splice.exec`, or some of its results, with a
  `splice.literal` removes an obligation and adds none.
- *Constant folding.* A directory, a symlink and an empty file are given
  in full by their entry: there are no bytes for a process to produce.
  Moving such results of a group into a literal is a demotion of those
  results. A group left with no used result, such as a `mkdir`, is then
  dead.
- *Merging and raising.* Let a used result of *G* be a file with content
  *c*, and let an operand *u* of *G* be a file with content *c*. Replacing
  the result with `splice.copy(u)` is sound by the copy case of
  Theorem 1, and the result leaves *W(G)*, so *G* has less to reproduce.
  Merging takes *u* at the result's own path, which is what a `chmod`
  leaves. Raising takes *u* at another path: `cp`, `install`, `mv`. A
  copy of a copy is a copy of the original.
- *Linking.* The same rewrite for a file the driver wrote, where *u* is
  the latest existing value with content *c*. The driver could see every
  file there was. This must run before elimination: until the copy
  exists nothing uses *u*, and elimination would delete the group that
  computed it.

Folding and copying are decided from the recorded entries alone. No
replay is needed to justify them, and none is run for a group they
leave with nothing to do.

## The mapping to build rules

| Graph | Bazel and Buck2 |
| --- | --- |
| file value | a file in the build tree, named `<rule>/<path in the build>` |
| directory or symlink value | a line in the manifest of each rule that uses it, plus a dependency on the stamp of the rule that made it |
| `splice.exec` | `genrule` whose `srcs` are its operand files, whose `outs` are its used result files and whose `cmd` is the recorded command line |
| `splice.literal` | files checked into the workspace |
| `splice.copy` | no rule: a reader lists the file the copy came from and its manifest stages that file at the copy's path |
| `splice.output` | `genrule` that writes a NAR |

No rule knows what it is building. A rule's command is the recorded
command, and its dependencies are the recorded dependencies.

The command runs through `sandbox.sh`, a shell script in the workspace
that implements *run*: it stages *σₐ* restricted to *R(G)* from the
rule's `srcs` and manifest, starts the recorded processes, and compares
*W(G)* with the manifest before writing any `outs`. `genrulify check`
calls the same script with the same arguments, so the check of Theorem 1
and the rule are one procedure applied to equal inputs.

A directory or a symlink is fully described by its entry, so a rule
stages one from its manifest and needs no file for it. The stamp
dependency keeps the induction honest: the stamp exists only if the
producing rule ran and its check of that entry passed.

## What the theorems do not say

- **Determinism is sampled.** Theorem 1 assumes *run* is a function.
  `check` tests that with two full passes, which bounds luck and does not
  remove it. A rule verifies its own results on every build, so a process
  that later diverges fails its rule and never hands a different file
  downstream.
- **Operands are sufficient, not necessary and not complete.** Any
  choice of *R(G)* that passes the check is admissible, which is why the
  operands can come from Rewind's log of opens, from the neighbours of
  those files, or from a system call trace of the group's own replay. An
  operand set that passes is enough to reproduce this build. It is not a statement
  about which other files could have affected the group. Nothing here
  speaks about a build with an edited input: that is a different run with
  a different trace.
- **T1 is partial in practice.** Rewind's archives omit FIFOs, sockets
  and devices, and genrulify ignores sub-second and directory mtimes and
  does not snapshot `/tmp`. A group that depends on any of these fails its
  check and is demoted, so the gap costs replay coverage and cannot make
  a checked graph wrong.
- **T2 covers pipe contents only at the `execve`.** A shell writes a
  here-string or a short here-document into a pipe before it starts the
  command, so the bytes are there to capture. Bytes the driver writes
  into a pipe while the command is already running are not captured. A
  group that needed them fails its check and becomes a literal.
- **P is an assumption.** The rules read the host's store and run on the
  host's kernel and CPU.

"""Rules for a build that genrulify converted from a recording, for Buck2.

Every rule in this file runs one recorded command, or one recorded
pipeline, through sandbox.sh, and fails unless each output has the hash
the recording saw. The rules have the names of the ones in genrulify.bzl,
and a command was given its kind the same way, by reading its recorded
command line. A kind is a label: every rule here does the same thing.

The attributes are plainer than Bazel's. Buck2 hands a command all of
its sources in one environment variable, which the kernel caps at 128
KiB, so a rule cannot list a few thousand files. A rule here writes one
directory, named after itself, and its `srcs` are the directories of the
rules and of the recorded bytes it reads. Which files it reads from them
is in its manifest. With directories for inputs there is no source to
tell from a header, so every rule takes `srcs`, `out` and `cmd`.
"""

# Whether a rule takes what its command leaves without comparing it with
# the recording. False for a recorded build, whose rules must reproduce
# it. True for a project converted in place, where the sources are the
# project's own files and an edit is supposed to change the outputs.
_TRUST = False

def _run(name, srcs, out, cmd):
    """Runs the recorded command lines of one step."""
    native.genrule(
        name = name,
        srcs = srcs,
        out = out,
        cmd = " ".join(cmd[:1] + (["--trust"] if _TRUST else []) + cmd[1:]),
        # The command reads the host's Nix store, which a remote worker
        # may not have.
        remote = False,
    )

def recorded(name, srcs, out, cmd):
    """A recorded command or pipeline of no particular kind."""
    _run(name, srcs, out, cmd)

def cc_object(name, srcs, out, cmd):
    """A C or C++ compile, to an object."""
    _run(name, srcs, out, cmd)

def cc_library(name, srcs, out, cmd):
    """An `ar` that collects objects into a static library."""
    _run(name, srcs, out, cmd)

def cc_binary(name, srcs, out, cmd):
    """A link of objects and libraries into a program."""
    _run(name, srcs, out, cmd)

def cc_shared_library(name, srcs, out, cmd):
    """A link of objects and libraries into a shared library."""
    _run(name, srcs, out, cmd)

def rust_library(name, srcs, out, cmd):
    """A rustc invocation that builds a library crate."""
    _run(name, srcs, out, cmd)

def rust_binary(name, srcs, out, cmd):
    """A rustc invocation that builds a program."""
    _run(name, srcs, out, cmd)

def rust_proc_macro(name, srcs, out, cmd):
    """A rustc invocation that builds a procedural macro."""
    _run(name, srcs, out, cmd)

def nix_archive(name, srcs, out, cmd):
    """Packs one derivation output as a NAR, the form Nix hashes."""
    _run(name, srcs, out, cmd)

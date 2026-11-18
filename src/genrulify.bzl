"""Rules for a build that genrulify converted from a recording.

Every rule in this file runs one recorded command, or one recorded
pipeline, through sandbox.sh, and fails unless each output has the hash
the recording saw. The rules differ in the names of their attributes and
in nothing else. `cc_object` says which input is the source and which are
headers, then runs the same command a plain `recorded` rule would.

A command was given its kind by reading its recorded command line: a
compiler driver with `-c` is a `cc_object`, `ar rc` is a `cc_library`. So
a kind is a label. A command that was misread gets a wrong label on a
rule that still does exactly what was recorded.

In every rule `cmd` is the list of lines the command is written on: where
sandbox.sh finds its inputs and puts its outputs, then for each process
its wiring and its recorded command line. `data` is what a command needs
that is not a source: its manifest, its environment, the store closure,
sandbox.sh, and the stamps of rules whose directories it uses.
"""

# The extension of the file a rule writes once every output has checked
# out. A rule whose results are all directories has no other file.
_STAMP = ".ok"

# The command reads the host's Nix store, which a remote worker may not
# have.
_TAGS = ["no-remote"]

def _run(name, srcs, outs, cmd):
    """Runs the recorded command lines of one step."""
    native.genrule(
        name = name,
        srcs = srcs,
        outs = outs + [name + _STAMP],
        cmd = " ".join(cmd),
        tags = _TAGS,
    )

def _outputs(out, outs):
    """The files a rule writes: the one it names, or the several."""
    return ([out] if out else []) + outs

def recorded(name, srcs = [], outs = [], cmd = []):
    """A recorded command or pipeline of no particular kind."""
    _run(name, srcs, outs, cmd)

def cc_object(name, src, hdrs = [], out = None, outs = [], data = [], cmd = []):
    """A C or C++ compile: one source and the headers it opened, to an object."""
    _run(name, [src] + hdrs + data, _outputs(out, outs), cmd)

def cc_library(name, objs = [], out = None, outs = [], data = [], cmd = []):
    """An `ar` that collects objects into a static library."""
    _run(name, objs + data, _outputs(out, outs), cmd)

def cc_binary(name, objs = [], deps = [], out = None, outs = [], data = [], cmd = []):
    """A link of objects and libraries into a program."""
    _run(name, objs + deps + data, _outputs(out, outs), cmd)

def cc_shared_library(name, objs = [], deps = [], out = None, outs = [], data = [], cmd = []):
    """A link of objects and libraries into a shared library."""
    _run(name, objs + deps + data, _outputs(out, outs), cmd)

def rust_library(name, srcs = [], deps = [], outs = [], data = [], cmd = []):
    """A rustc invocation that builds a library crate."""
    _run(name, srcs + deps + data, outs, cmd)

def rust_binary(name, srcs = [], deps = [], outs = [], data = [], cmd = []):
    """A rustc invocation that builds a program."""
    _run(name, srcs + deps + data, outs, cmd)

def rust_proc_macro(name, srcs = [], deps = [], outs = [], data = [], cmd = []):
    """A rustc invocation that builds a procedural macro."""
    _run(name, srcs + deps + data, outs, cmd)

def nix_archive(name, srcs, out, cmd):
    """Packs one derivation output as a NAR, the form Nix hashes."""
    native.genrule(
        name = name,
        srcs = srcs,
        outs = [out],
        cmd = " ".join(cmd),
        tags = _TAGS,
    )

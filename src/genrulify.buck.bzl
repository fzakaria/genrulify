"""Rules for a build that genrulify converted from a recording, for Buck2.

Every rule in this file runs one recorded command, or one recorded
pipeline, through sandbox.sh, and fails unless each output has the hash
the recording saw. In a project converted in place, a command that reads
the project's sources is run with --trust and its outputs are taken as
they come. The rules have the names and the attributes of the ones in
genrulify.bzl, and a command was given its kind the same way, by reading
its recorded command line. A kind is a label: every rule here does the
same thing.

A rule declares each file its command writes as an output of its own. The
rule itself stands for its stamp, the file written once every output has
checked out, and `:rule[path]` is one of its files. A rule that reads one
file of another rule depends on that file alone, and runs again only when
the file changes.

Buck2's own genrule is not used. It hands a command all of its sources in
one environment variable, which the kernel caps at 128 KiB, and its one
output is a directory, so a reader of any file in it runs again when any
other file in it changes.
"""

# The extension of the file a rule writes once every output has checked
# out. A rule whose results are all directories has no other file.
_STAMP = ".ok"

# The names a step gives the directory of its inputs and its script, which
# no rule has: a rule's name starts with a letter.
_SOURCES = "__srcs"
_SCRIPT = "__run.sh"

def _step_impl(ctx):
    """Runs the recorded command lines of one step."""

    # The file the step is known by, and every other file it writes. They
    # are all below one directory, which the command is told as OUTDIR.
    main = ctx.actions.declare_output(ctx.attrs.main)
    files = {path: ctx.actions.declare_output(path) for path in ctx.attrs.outs}

    # Everything the step reads, linked under the names sandbox.sh looks
    # files up by: a file of the workspace by its path, and a file of
    # another rule by that rule's name and then the file's path.
    sources = ctx.actions.symlinked_dir(
        _SOURCES,
        {source.short_path: source for source in ctx.attrs.srcs},
    )

    # The lines go into a script, because a long pipeline does not fit in
    # one argument.
    script = ctx.actions.write(_SCRIPT, " ".join(ctx.attrs.cmd) + "\n")
    ctx.actions.run(
        cmd_args(
            ["/usr/bin/env", "bash", "-e", script],
            hidden = [sources, main.as_output()] +
                     [file.as_output() for file in files.values()],
        ),
        env = {
            "OUTDIR": cmd_args(main.as_output(), parent = 1),
            "SRCDIR": sources,
        },
        category = "recorded",

        # The command reads the host's Nix store, which a remote worker
        # may not have.
        local_only = True,
    )

    # A file is named `rule/path`, and is the rule's sub-target `path`.
    prefix = ctx.label.name + "/"
    return [DefaultInfo(
        default_output = main,
        sub_targets = {
            path.removeprefix(prefix): [DefaultInfo(default_output = file)]
            for path, file in files.items()
        },
    )]

_step = rule(
    impl = _step_impl,
    attrs = {
        "cmd": attrs.list(attrs.string()),
        "main": attrs.string(),
        "outs": attrs.list(attrs.string(), default = []),
        "srcs": attrs.list(attrs.source(), default = []),
    },
)

def _run(name, srcs, outs, cmd):
    """One step, known by its stamp."""
    _step(name = name, srcs = srcs, main = name + _STAMP, outs = outs, cmd = cmd)

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

def _named_impl(ctx):
    """Stands for one file of another rule, and runs it when it is a program."""
    providers = [DefaultInfo(default_output = ctx.attrs.file)]
    if not ctx.attrs.runs:
        return providers
    if not ctx.attrs.libs:
        return providers + [RunInfo(args = cmd_args(ctx.attrs.file))]

    # Each library is linked into one directory under the name the link
    # knew it by, which is the name the loader asks for, and the program
    # is started with that directory as LD_LIBRARY_PATH.
    libraries = ctx.actions.symlinked_dir(
        ctx.label.name + ".libs",
        dict(zip(ctx.attrs.lib_names, ctx.attrs.libs)),
    )
    return providers + [RunInfo(args = cmd_args(
        "/usr/bin/env",
        cmd_args(libraries, format = "LD_LIBRARY_PATH={}"),
        ctx.attrs.file,
    ))]

_named = rule(
    impl = _named_impl,
    attrs = {
        "file": attrs.source(),
        "lib_names": attrs.list(attrs.string(), default = []),
        "libs": attrs.list(attrs.source(), default = []),
        "runs": attrs.bool(default = False),
    },
)

def built_file(name, file):
    """Names a file a rule wrote after the file's place in the build tree."""
    _named(name = name, file = file)

def program(name, file, libs = [], lib_names = []):
    """Names a program a rule linked, for `buck2 run`.

    The program is the recorded build's: it finds the libraries of other
    packages in the Nix store the recording used. A shared library its own
    project built is a file of another rule, so the target is given those,
    `libs`, with the name the loader asks for each by, `lib_names`. A
    library the program asks for under a third name, such as a versioned
    soname the link never read, is not found this way.
    """
    _named(name = name, file = file, libs = libs, lib_names = lib_names, runs = True)

def nix_archive(name, srcs, out, cmd):
    """Packs one derivation output as a NAR, the form Nix hashes."""
    _step(name = name, srcs = srcs, main = out, cmd = cmd)

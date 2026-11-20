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
    #
    # A file of another package of the project comes under its name in
    # that package, where the command reads it at its path in the project.
    # `foreign` lists those files and `foreign_paths` the path of each.
    placed = dict(zip(ctx.attrs.foreign, ctx.attrs.foreign_paths))
    sources = ctx.actions.symlinked_dir(
        _SOURCES,
        {placed.get(source, source.short_path): source for source in ctx.attrs.srcs},
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
        "foreign": attrs.list(attrs.source(), default = []),
        "foreign_paths": attrs.list(attrs.string(), default = []),
        "main": attrs.string(),
        "outs": attrs.list(attrs.string(), default = []),
        "srcs": attrs.list(attrs.source(), default = []),
    },
)

def _run(name, srcs, outs, cmd, **placed):
    """One step, known by its stamp. `placed` is `foreign` and `foreign_paths`."""
    _step(name = name, srcs = srcs, main = name + _STAMP, outs = outs, cmd = cmd, **placed)

def _outputs(out, outs):
    """The files a rule writes: the one it names, or the several."""
    return ([out] if out else []) + outs

def recorded(name, srcs = [], outs = [], cmd = [], **placed):
    """A recorded command or pipeline of no particular kind."""
    _run(name, srcs, outs, cmd, **placed)

def cc_object(name, src, hdrs = [], out = None, outs = [], data = [], cmd = [], **placed):
    """A C or C++ compile: one source and the headers it opened, to an object."""
    _run(name, [src] + hdrs + data, _outputs(out, outs), cmd, **placed)

def cc_library(name, objs = [], out = None, outs = [], data = [], cmd = [], **placed):
    """An `ar` that collects objects into a static library."""
    _run(name, objs + data, _outputs(out, outs), cmd, **placed)

def cc_binary(name, objs = [], deps = [], out = None, outs = [], data = [], cmd = [], **placed):
    """A link of objects and libraries into a program."""
    _run(name, objs + deps + data, _outputs(out, outs), cmd, **placed)

def cc_shared_library(name, objs = [], deps = [], out = None, outs = [], data = [], cmd = [], **placed):
    """A link of objects and libraries into a shared library."""
    _run(name, objs + deps + data, _outputs(out, outs), cmd, **placed)

def rust_library(name, srcs = [], deps = [], outs = [], data = [], cmd = [], **placed):
    """A rustc invocation that builds a library crate."""
    _run(name, srcs + deps + data, outs, cmd, **placed)

def rust_binary(name, srcs = [], deps = [], outs = [], data = [], cmd = [], **placed):
    """A rustc invocation that builds a program."""
    _run(name, srcs + deps + data, outs, cmd, **placed)

def rust_proc_macro(name, srcs = [], deps = [], outs = [], data = [], cmd = [], **placed):
    """A rustc invocation that builds a procedural macro."""
    _run(name, srcs + deps + data, outs, cmd, **placed)

def _watched_impl(ctx):
    """Writes one file once the stamps of the watched commands are there."""
    stamp = ctx.actions.declare_output(ctx.label.name + _STAMP)
    ctx.actions.run(
        cmd_args(
            ["/usr/bin/env", "bash", "-c", ': > "$1"', "--", stamp.as_output()],
            hidden = ctx.attrs.srcs,
        ),
        category = "watched",
        local_only = True,
    )
    return [DefaultInfo(default_output = stamp)]

_watched = rule(
    impl = _watched_impl,
    attrs = {
        "srcs": attrs.list(attrs.source()),
    },
)

def watched(name, srcs):
    """Stands for commands that are run to see that they still do what was recorded.

    A project's command that reads its sources is run again after an edit
    and fails when it ends another way than it did or leaves something
    else. The builder may have decided what to run next by how such a
    command ended. So whatever somebody builds waits for the commands of
    that kind recorded before the one that made it: `srcs` is the stamps
    of some of them and the target of this kind for the ones before. The
    archive and each file with a name of its own name one such target.
    """
    _watched(name = name, srcs = srcs)

def _named_impl(ctx):
    """Stands for one file of another rule, and runs it when it is a program."""

    # Whoever takes the file, builds it by name or runs it also waits for
    # the watched commands recorded before the one that made the file.
    checks = ctx.attrs.after
    file = ctx.attrs.file.with_associated_artifacts(checks) if checks else ctx.attrs.file
    providers = [DefaultInfo(default_output = file, other_outputs = checks)]
    if not ctx.attrs.runs:
        return providers
    if not ctx.attrs.libs:
        return providers + [RunInfo(args = cmd_args(file, hidden = checks))]

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
        file,
        hidden = checks,
    ))]

_named = rule(
    impl = _named_impl,
    attrs = {
        "after": attrs.list(attrs.source(), default = []),
        "file": attrs.source(),
        "lib_names": attrs.list(attrs.string(), default = []),
        "libs": attrs.list(attrs.source(), default = []),
        "runs": attrs.bool(default = False),
    },
)

def built_file(name, file, after = []):
    """Names a file a rule wrote after the file's place in the build tree.

    `after` is the `watched` target for the commands recorded before the
    one that wrote the file.
    """
    _named(name = name, file = file, after = after)

def program(name, file, libs = [], lib_names = [], after = []):
    """Names a program a rule linked, for `buck2 run`.

    The program is the recorded build's: it finds the libraries of other
    packages in the Nix store the recording used. A shared library its own
    project built is a file of another rule, so the target is given those,
    `libs`, with the name the loader asks for each by, `lib_names`. They
    are the libraries the program's link was given and the ones the link
    of each of those was given, and so on. A library the program asks for
    under a third name, such as a versioned soname no link read, is not
    found this way.
    """
    _named(name = name, file = file, libs = libs, lib_names = lib_names, after = after, runs = True)

def nix_archive(name, srcs, out, cmd, **placed):
    """Packs one derivation output as a NAR, the form Nix hashes."""
    _step(name = name, srcs = srcs, main = out, cmd = cmd, **placed)

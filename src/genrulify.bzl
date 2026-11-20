"""Rules for a build that genrulify converted from a recording.

Every rule in this file runs one recorded command, or one recorded
pipeline, through sandbox.sh, and fails unless each output has the hash
the recording saw. In a project converted in place, a command that reads
the project's sources is run with --trust and its outputs are taken as
they come. The rules differ in the names of their attributes and in
nothing else. `cc_object` says which input is the source and which are
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

    # One file stands for all of them, so that a chain of these targets
    # stays one file deep however long it is.
    native.genrule(
        name = name,
        srcs = srcs,
        outs = [name + _STAMP],
        cmd = ": > $@",
        tags = _TAGS,
    )

def _built_file_impl(ctx):
    """Hands on one file of another rule, and has the watched commands checked."""

    # The file is the other rule's own. The watched commands are a
    # validation: Bazel runs them when the target is built, by name or
    # as a dependency of another rule, and fails the build with them.
    return [
        DefaultInfo(files = depset([ctx.file.file])),
        OutputGroupInfo(_validation = depset(ctx.files.after)),
    ]

# Names a file a rule wrote after the file's place in the build tree.
# `after` is the `watched` target for the commands recorded before the one
# that wrote the file.
built_file = rule(
    implementation = _built_file_impl,
    attrs = {
        "after": attr.label_list(allow_files = True),
        "file": attr.label(allow_single_file = True, mandatory = True),
    },
)

# The script a program is started through. It puts the directory of the
# program's shared libraries ahead of the loader's other places and then
# becomes the program. The files are in the script's runfiles.
_LAUNCHER = """#!/bin/sh
root=${{RUNFILES_DIR:-$0.runfiles}}/{workspace}
LD_LIBRARY_PATH="$root/{libraries}${{LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}}" exec "$root/{program}" "$@"
"""

def _program_impl(ctx):
    """Starts the program with its project's shared libraries, for `bazel run`."""
    launcher = ctx.actions.declare_file(ctx.label.name)

    # Each library is linked into one directory under the name the link
    # knew it by, which is the name the loader asks for.
    libraries = ctx.label.name + ".libs"
    symlinks = {
        libraries + "/" + name: library
        for name, library in zip(ctx.attr.lib_names, ctx.files.libs)
    }
    ctx.actions.write(
        output = launcher,
        content = _LAUNCHER.format(
            workspace = ctx.workspace_name,
            libraries = libraries,
            program = ctx.file.file.short_path,
        ),
        is_executable = True,
    )

    # The target's file is the program itself, so that a rule which names
    # the target among its sources is given the program. The launcher is
    # what `bazel run` starts, and its runfiles exist only there. The
    # stamps of the watched commands are among the runfiles, so that the
    # program is not started before those commands have been checked,
    # and they are a validation for a rule that only takes the file.
    return [
        DefaultInfo(
            files = depset([ctx.file.file]),
            executable = launcher,
            runfiles = ctx.runfiles(
                files = [ctx.file.file] + ctx.files.after,
                symlinks = symlinks,
            ),
        ),
        OutputGroupInfo(_validation = depset(ctx.files.after)),
    ]

# Names a program a rule linked after the program's place in the build
# tree, and runs it. The program is the recorded build's: it finds the
# libraries of other packages in the Nix store the recording used. A
# shared library its own project built is a file of another rule, so the
# target is given those, `libs`, with the name the loader asks for each
# by, `lib_names`, and puts them on the loader's path. A library the
# program asks for under a third name, such as a versioned soname the
# link never read, is not found this way. `after` is the `watched` target
# for the commands recorded before the link.
program = rule(
    implementation = _program_impl,
    attrs = {
        "after": attr.label_list(allow_files = True),
        "file": attr.label(allow_single_file = True, mandatory = True),
        "lib_names": attr.string_list(),
        "libs": attr.label_list(allow_files = True),
    },
    executable = True,
)

def nix_archive(name, srcs, out, cmd):
    """Packs one derivation output as a NAR, the form Nix hashes."""
    native.genrule(
        name = name,
        srcs = srcs,
        outs = [out],
        cmd = " ".join(cmd),
        tags = _TAGS,
    )

# ---- the store paths of a workspace that many builds share ------------------
#
# A package below store/ stands for one store path. Its target `path` is
# the path's archive, a file named `nar`, and the file `nar.sha256` with
# the archive's digest. The three rules below differ in where the archive
# comes from.

# The files of a store path's package, and who may read them.
_ARCHIVE = "nar"
_DIGEST = "nar.sha256"
_PUBLIC = ["//visibility:public"]

# The script that unpacks a fetched archive.
_SCRIPT = "//genrulify:sandbox.sh"

def _store_path(name, archive):
    """The archive of a store path and the archive's digest, as one target."""
    native.filegroup(
        name = name,
        srcs = [archive, _DIGEST],
        visibility = _PUBLIC,
    )

def _archive_link_impl(ctx):
    """Gives another package's archive the name a store path's archive has."""
    link = ctx.actions.declare_file(_ARCHIVE)
    ctx.actions.symlink(output = link, target_file = ctx.file.archive)
    return [DefaultInfo(files = depset([link]))]

_archive_link = rule(
    implementation = _archive_link_impl,
    attrs = {
        "archive": attr.label(allow_single_file = True, mandatory = True),
    },
)

def nix_built(name, archive):
    """A store path a package of the workspace builds, from that package's archive."""
    _archive_link(name = name + "_archive", archive = archive)
    _store_path(name, ":" + name + "_archive")

def nix_fetched(name, src, compression, sha256):
    """A store path a binary cache has: the fetched file, unpacked and held to the cache's digest."""
    native.genrule(
        name = name + "_archive",
        srcs = [src, _SCRIPT],
        outs = [_ARCHIVE],
        cmd = "$(location {script}) import {compression} $(location {src}) $@ {sha256}".format(
            script = _SCRIPT,
            compression = compression,
            src = src,
            sha256 = sha256,
        ),
        tags = _TAGS,
    )
    _store_path(name, _ARCHIVE)

def nix_source(name):
    """A store path kept in the workspace as its archive: a source file, or a build no cache has."""
    _store_path(name, _ARCHIVE)

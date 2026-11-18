# A project to convert in place

Two C files, a header and a Makefile, built by the derivation in
`nix/project.nix`, whose `src` is this directory.

```console
$ genrulify convert --source examples/project .#project /tmp/project
$ cd examples/project && bazel build //:archive
```

The build files are written here, beside the sources they name, and
everything else genrulify needs goes under `genrulify/`. Edit `greet.c`,
build again, and only the rules that read it run.

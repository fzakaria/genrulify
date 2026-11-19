# A project to convert in place

Two C files, a header and a Makefile, built by the derivation in
`nix/project.nix`, whose `src` is this directory.

```console
$ genrulify convert --source examples/project .#project /tmp/project
$ cd examples/project && bazel build //:archive
```

The rules are written under `genrulify/`, and the `BUILD.bazel` and
`BUCK` written here load them. Edit `greet.c`, build again, and only the
rules that read it run. `bazel run //:greet` or `buck2 run //:greet`
starts the program that was just linked.

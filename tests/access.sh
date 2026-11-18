# Narrowing a group's inputs from a trace of its own replay. `accessed`
# reads the per-process files strace writes and prints the guest paths the
# processes touched. The fabricated trace has a path relative to a working
# directory, a path relative to an open directory, an absolute path given
# to a system call without a directory argument, a failed lookup, a path
# that needs normalizing, a directory opened for listing, and paths that
# are relative to a working directory the trace only implies.
set -eu
compiler=$1
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
mkdir "$scratch/trace"
cat > "$scratch/trace/0.101" <<'TRACE'
newfstatat(AT_FDCWD</build/src>, "Makefile.am", {st_mode=S_IFREG|0644, st_size=10, ...}, 0) = 0
openat(AT_FDCWD</build/src>, "../include/./api.h", O_RDONLY|O_CLOEXEC) = 3</build/include/api.h>
newfstatat(5</build/lib>, "util.c", {st_mode=S_IFREG|0644, st_size=1, ...}, AT_SYMLINK_NOFOLLOW) = 0
access("/build/config.site", R_OK) = 0
newfstatat(AT_FDCWD</build/src>, "missing.h", 0x7ffd, 0) = -1 ENOENT (No such file or directory)
openat(AT_FDCWD</build/src>, "/build/po", O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_DIRECTORY) = 4</build/po>
TRACE
cat > "$scratch/trace/1.202" <<'TRACE'
execve("/build/src/tool", ["tool"], 0x7ffd /* 3 vars */) = 0
readlink("/proc/self/exe", "/build/src/tool", 4095) = 15
TRACE

# An older call names a path with no directory argument. It is relative to
# the process's working directory, which the trace shows wherever a newer
# call prints AT_FDCWD and whenever the process changes directory.
cat > "$scratch/trace/2.303" <<'TRACE'
access("early", X_OK) = 0
openat(AT_FDCWD</build/pkg>, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3</etc/ld.so.cache>
access("build-aux/install-sh", X_OK) = 0
chdir("po") = 0
stat("LINGUAS", {st_mode=S_IFREG|0644, st_size=4, ...}) = 0
chdir("/build/pkg/doc") = 0
execve("./gen", ["./gen"], 0x7ffd /* 3 vars */) = 0
fchdir(7</build/pkg/tests>) = 0
lstat("data", {st_mode=S_IFDIR|0755, st_size=4, ...}) = 0
TRACE
"$compiler" accessed "$scratch/trace" | sort > "$scratch/paths"
cat > "$scratch/expected" <<'PATHS'
/build/config.site
/build/include/api.h
/build/lib/util.c
/build/pkg/build-aux/install-sh
/build/pkg/doc
/build/pkg/doc/gen
/build/pkg/po
/build/pkg/po/LINGUAS
/build/pkg/tests/data
/build/po/
/build/src/Makefile.am
/build/src/tool
/etc/ld.so.cache
/proc/self/exe
PATHS
diff "$scratch/expected" "$scratch/paths"

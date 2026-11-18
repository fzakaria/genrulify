include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

def SpliceDialect : Dialect {
  let name = "splice";
  let cppNamespace = "::splice_ir";
  let useDefaultTypePrinterParser = 1;
  let useDefaultAttributePrinterParser = 1;
  let summary = "A recorded build as versions of filesystem entries";
  let description = [{
    A build is a sequence of processes that change a filesystem. The splice
    dialect names every version of every path the build wrote and makes each
    process group a function from the versions the group observed to the
    versions the group left behind. A path that is written twice has two
    SSA values, so an in-place update is an ordinary def-use edge.
  }];
}

// One version of one path: a regular file with its bytes and mode, a
// directory with its mode, or a symlink with its target. A directory value
// does not include the directory's children.
def Entry : TypeDef<SpliceDialect, "Entry"> {
  let mnemonic = "entry";
  let summary = "One version of one filesystem path";
  let assemblyFormat = "";
}

// What the recording observed at a path when a process group or the
// builder left the path behind.
def EntryAttr : AttrDef<SpliceDialect, "Entry"> {
  let mnemonic = "entry";
  let summary = "The recorded kind, mode, mtime and content of a path";
  let parameters = (ins
    StringRefParameter<"guest path">:$path,
    "EntryKind":$kind,
    "uint32_t":$mode,
    "int64_t":$mtime,
    StringRefParameter<"SHA-256 of a file's bytes, or a symlink's target">:$content
  );
  let hasCustomAssemblyFormat = 1;
}

def EntryArrayAttr : TypedArrayAttrBase<EntryAttr, "recorded entries">;

class SpliceOp<string mnemonic, list<Trait> traits = []> : Op<SpliceDialect, mnemonic, traits>;

// Pure is the claim that replay checks: the entries a process group writes
// are a function of the entries the group was given and of the store closure.
// An exec whose results are unused is dead, and MLIR removes it.
def ExecOp : SpliceOp<"exec", [Pure]> {
  let summary = "Run a recorded process group against entry versions";
  let description = [{
    `step` is the Rewind step of the group's first execve and identifies the
    group in the recording, which holds every argv, environment, working
    directory and descriptor table. `name` is a label for people. Result `i`
    is the version of `writes[i].path` the group left behind.
  }];
  let arguments = (ins Variadic<Entry>:$inputs, StrAttr:$name, I64Attr:$step, EntryArrayAttr:$writes);
  let results = (outs Variadic<Entry>:$outputs);
  let assemblyFormat = "$name `at` $step `(` $inputs `)` custom<Writes>($writes, type($outputs)) attr-dict";
  let hasVerifier = 1;
}

// Bytes taken from the recording. Three things have no execve to replay:
// what Nix put in the sandbox before the builder started ("sandbox"), what
// the builder wrote in-process between process groups ("glue"), and a
// process group that replay could not reproduce, which is marked `demoted`
// and keeps the group's name.
def LiteralOp : SpliceOp<"literal", [Pure]> {
  let summary = "Entry versions copied from the recording";
  let arguments = (ins StrAttr:$name, I64Attr:$step, EntryArrayAttr:$writes, UnitAttr:$demoted);
  let results = (outs Variadic<Entry>:$outputs);
  let assemblyFormat = "$name `at` $step (`demoted` $demoted^)? custom<Writes>($writes, type($outputs)) attr-dict";
  let hasVerifier = 1;
}

// The root of the graph: the final version of every path below one
// derivation output.
def OutputOp : SpliceOp<"output"> {
  let summary = "Publish entry versions as a derivation output";
  let arguments = (ins Variadic<Entry>:$entries, StrAttr:$name, StrAttr:$path);
  let assemblyFormat = "$name $path `(` $entries `)` attr-dict";
  let hasVerifier = 1;
}

include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

def SpliceDialect : Dialect {
  let name = "splice";
  let cppNamespace = "::splice_ir";
  let useDefaultTypePrinterParser = 1;
}

def File : TypeDef<SpliceDialect, "File"> {
  let mnemonic = "file";
  let summary = "Bytes of an immutable, non-executable regular file";
  let assemblyFormat = "";
}

class SpliceOp<string mnemonic, list<Trait> traits = []> : Op<SpliceDialect, mnemonic, traits>;
def SourceOp : SpliceOp<"source", [Pure]> {
  let arguments = (ins StrAttr:$path);
  let results = (outs File:$file);
  let assemblyFormat = "$path attr-dict";
}
def CopyOp : SpliceOp<"copy", [Pure]> {
  let arguments = (ins File:$input, StrAttr:$path, I64Attr:$step, I64Attr:$version);
  let results = (outs File:$file);
  let assemblyFormat = "$input attr-dict";
  let hasFolder = 1;
}
def AppendOp : SpliceOp<"append", [Pure]> {
  let arguments = (ins File:$previous, File:$suffix, StrAttr:$path, I64Attr:$step, I64Attr:$version);
  let results = (outs File:$file);
  let assemblyFormat = "$previous `,` $suffix attr-dict";
}
def PublishOp : SpliceOp<"publish"> {
  let arguments = (ins File:$file);
  let assemblyFormat = "$file attr-dict";
}

// A NAR carries the tree shape, symlinks and executable bits across actions.
def Nar : TypeDef<SpliceDialect, "Nar"> {
  let mnemonic = "nar";
  let assemblyFormat = "";
}
def SeedOp : SpliceOp<"seed", [Pure]> {
  let arguments = (ins StrAttr:$asset);
  let results = (outs Nar:$tree);
  let assemblyFormat = "$asset attr-dict";
}
def ExecOp : SpliceOp<"exec"> {
  let arguments = (ins Nar:$input, StrAttr:$action, I64Attr:$step);
  let results = (outs Nar:$tree);
  let assemblyFormat = "$input $action attr-dict";
}
def PackageOp : SpliceOp<"package"> {
  let arguments = (ins Nar:$tree);
  let assemblyFormat = "$tree attr-dict";
}

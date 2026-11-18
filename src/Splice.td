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

#include "dialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/PatternMatch.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/Format.h"

namespace splice_ir {
namespace {
// The keyword that precedes an entry's modification time.
constexpr llvm::StringLiteral kMtimeKeyword = "mtime";

constexpr unsigned kOctalBase = 8;
constexpr unsigned kDecimalBase = 10;

// The IR prints a mode as its octal digits, the way `ls` and `chmod` write
// modes. MLIR lexes those digits as a decimal integer, so parsing reads the
// decimal digits back as octal ones.
std::optional<uint32_t> octalFromDigits(uint64_t digits) {
  uint32_t mode = 0;
  uint32_t place = 1;
  while (digits != 0) {
    const auto digit = digits % kDecimalBase;
    if (digit >= kOctalBase) {
      return std::nullopt;
    }
    mode += digit * place;
    place *= kOctalBase;
    digits /= kDecimalBase;
  }
  return mode;
}

// Parses `-> [entry, ...]` and gives the operation one result per entry.
mlir::ParseResult parseWrites(mlir::OpAsmParser &parser, mlir::ArrayAttr &writes,
                              llvm::SmallVectorImpl<mlir::Type> &types) {
  llvm::SmallVector<mlir::Attribute> entries;
  const auto parseEntry = [&]() -> mlir::ParseResult {
    EntryAttr entry;
    if (parser.parseAttribute(entry)) {
      return mlir::failure();
    }
    entries.push_back(entry);
    return mlir::success();
  };
  if (parser.parseArrow() ||
      parser.parseCommaSeparatedList(mlir::AsmParser::Delimiter::Square,
                                     parseEntry)) {
    return mlir::failure();
  }
  writes = parser.getBuilder().getArrayAttr(entries);
  types.assign(entries.size(), EntryType::get(parser.getContext()));
  return mlir::success();
}

// Prints one entry per line so a reader can scan the paths a group wrote.
void printWrites(mlir::OpAsmPrinter &printer, mlir::Operation *,
                 mlir::ArrayAttr writes, mlir::TypeRange) {
  printer << "-> [";
  printer.increaseIndent();
  printer.increaseIndent();
  llvm::interleave(
      writes, [&](mlir::Attribute entry) {
        printer.printNewline();
        printer << entry;
      },
      [&] { printer << ","; });
  printer.decreaseIndent();
  printer.decreaseIndent();
  if (!writes.empty()) {
    printer.printNewline();
  }
  printer << "]";
}

// Checks that an operation has one result per written entry and that no
// path is written twice by the same operation.
mlir::LogicalResult verifyWrites(mlir::Operation *op, mlir::ArrayAttr writes) {
  if (writes.size() != op->getNumResults()) {
    return op->emitOpError("expected one result per written entry");
  }
  llvm::StringSet<> paths;
  for (const auto entry : writes.getAsRange<EntryAttr>()) {
    if (!paths.insert(entry.getPath()).second) {
      return op->emitOpError("writes ") << entry.getPath() << " twice";
    }
  }
  return mlir::success();
}
} // namespace
} // namespace splice_ir

#include "Dialect.cpp.inc"
#define GET_TYPEDEF_CLASSES
#include "Types.cpp.inc"
#define GET_ATTRDEF_CLASSES
#include "Attrs.cpp.inc"
#define GET_OP_CLASSES
#include "Ops.cpp.inc"

namespace splice_ir {
void SpliceDialect::initialize() {
  addTypes<EntryType>();
  addAttributes<EntryAttr>();
  addOperations<ExecOp, LiteralOp, CopyOp, OutputOp>();
}

// Syntax: <"/build/a.out" file 755 mtime 1 "sha256 or link target">. The
// trailing string is absent for a directory.
mlir::Attribute EntryAttr::parse(mlir::AsmParser &parser, mlir::Type) {
  std::string path;
  std::string content;
  llvm::StringRef kind;
  uint64_t modeDigits = 0;
  int64_t mtime = 0;
  const auto kindLocation = parser.getCurrentLocation();
  if (parser.parseLess() || parser.parseString(&path) ||
      parser.parseKeyword(&kind) || parser.parseInteger(modeDigits) ||
      parser.parseKeyword(kMtimeKeyword) || parser.parseInteger(mtime)) {
    return {};
  }
  (void)parser.parseOptionalString(&content);
  if (parser.parseGreater()) {
    return {};
  }

  // Reject a kind or mode the recording could not have produced.
  const auto mode = octalFromDigits(modeDigits);
  const std::string_view name = kind;
  const bool knownKind = name == kindName(EntryKind::File) ||
                         name == kindName(EntryKind::Directory) ||
                         name == kindName(EntryKind::Symlink);
  if (!knownKind || !mode) {
    parser.emitError(kindLocation, "expected file, dir or link and an octal mode");
    return {};
  }
  return EntryAttr::get(parser.getContext(), path, kindFromName(name), *mode,
                        mtime, content);
}

void EntryAttr::print(mlir::AsmPrinter &printer) const {
  printer << "<";
  printer.printString(getPath());
  printer << " " << kindName(getKind()) << " "
          << llvm::format("%o", getMode()) << " " << kMtimeKeyword << " "
          << getMtime();
  if (!getContent().empty()) {
    printer << " ";
    printer.printString(getContent());
  }
  printer << ">";
}

Entry fromAttribute(EntryAttr attribute) {
  return {attribute.getPath().str(), attribute.getKind(), attribute.getMode(),
          attribute.getMtime(), attribute.getContent().str()};
}

EntryAttr toAttribute(mlir::MLIRContext *context, const Entry &entry) {
  return EntryAttr::get(context, entry.path, entry.kind, entry.mode,
                        entry.mtime, entry.content);
}

mlir::ArrayAttr toAttributes(mlir::MLIRContext *context,
                             const std::vector<Entry> &entries) {
  llvm::SmallVector<mlir::Attribute> attributes;
  for (const auto &entry : entries) {
    attributes.push_back(toAttribute(context, entry));
  }
  return mlir::ArrayAttr::get(context, attributes);
}

std::optional<Entry> entryOf(mlir::Value value) {
  auto *op = value.getDefiningOp();
  if (op == nullptr) {
    return std::nullopt;
  }
  if (auto copy = llvm::dyn_cast<CopyOp>(op)) {
    return fromAttribute(copy.getEntry());
  }
  if (!llvm::isa<ExecOp, LiteralOp>(op)) {
    return std::nullopt;
  }
  const auto writes = op->getAttrOfType<mlir::ArrayAttr>("writes");
  const auto index = llvm::cast<mlir::OpResult>(value).getResultNumber();
  return fromAttribute(llvm::cast<EntryAttr>(writes[index]));
}

// The name MLIR prints for the values of an operation: the operation's
// label with every character an identifier cannot hold replaced, then the
// step. MLIR adds a suffix when two operations ask for the same name, as
// the copies raised from one group do.
template <typename Op>
void nameResults(Op op, mlir::OpAsmSetValueNameFn setName) {
  if (op->getNumResults() == 0) {
    return;
  }
  std::string name;
  for (const char c : op.getName()) {
    name.push_back(std::isalnum(static_cast<unsigned char>(c)) != 0 ? c : '_');
  }
  setName(op->getResult(0), name + "_" + std::to_string(op.getStep()));
}

void ExecOp::getAsmResultNames(mlir::OpAsmSetValueNameFn setName) {
  nameResults(*this, setName);
}

void LiteralOp::getAsmResultNames(mlir::OpAsmSetValueNameFn setName) {
  nameResults(*this, setName);
}

void CopyOp::getAsmResultNames(mlir::OpAsmSetValueNameFn setName) {
  nameResults(*this, setName);
}

mlir::Value contentSource(mlir::Value value) {
  while (auto copy = llvm::dyn_cast_or_null<CopyOp>(value.getDefiningOp())) {
    value = copy.getSource();
  }
  return value;
}

bool isConstant(const Entry &entry) {
  return entry.kind != EntryKind::File || entry.content == kEmptyDigest;
}

// A copy has the bytes of its source, and only a regular file has bytes.
mlir::LogicalResult CopyOp::verify() {
  const auto source = entryOf(getSource());
  if (!source) {
    return emitOpError("expected a source defined by a splice operation");
  }
  const auto entry = fromAttribute(getEntry());
  if (source->kind != EntryKind::File || entry.kind != EntryKind::File) {
    return emitOpError("copies something other than a regular file");
  }
  if (source->content != entry.content) {
    return emitOpError("does not have the content of its source");
  }
  return mlir::success();
}

// A copy of a copy has the bytes of the original, so the copy names the
// original and the copy in between can die if nothing else reads it.
mlir::LogicalResult CopyOp::canonicalize(CopyOp op,
                                         mlir::PatternRewriter &rewriter) {
  const auto original = contentSource(op.getSource());
  if (original == op.getSource()) {
    return mlir::failure();
  }
  rewriter.modifyOpInPlace(op, [&] { op.getSourceMutable().assign(original); });
  return mlir::success();
}

// A process group sees one filesystem, so the group cannot be given two
// versions of the same path.
mlir::LogicalResult ExecOp::verify() {
  if (mlir::failed(verifyWrites(getOperation(), getWrites()))) {
    return mlir::failure();
  }

  // A kind names a rule in the files a workspace ships, so it has a name.
  if (const auto kind = getRule(); kind && kind->empty()) {
    return emitOpError("has an empty rule kind");
  }
  llvm::StringSet<> paths;
  for (const auto input : getInputs()) {
    const auto entry = entryOf(input);
    if (!entry) {
      return emitOpError("expected inputs defined by splice operations");
    }
    if (!paths.insert(entry->path).second) {
      return emitOpError("reads two versions of ") << entry->path;
    }
  }
  return mlir::success();
}

mlir::LogicalResult LiteralOp::verify() {
  return verifyWrites(getOperation(), getWrites());
}

// Every published entry lies below the output path, and each path is
// published once.
mlir::LogicalResult OutputOp::verify() {
  llvm::StringSet<> paths;
  for (const auto value : getEntries()) {
    const auto entry = entryOf(value);
    if (!entry) {
      return emitOpError("expected entries defined by splice operations");
    }
    if (!isBelow(entry->path, getPath().str())) {
      return emitOpError("publishes ") << entry->path << " outside " << getPath();
    }
    if (!paths.insert(entry->path).second) {
      return emitOpError("publishes two versions of ") << entry->path;
    }
  }
  return mlir::success();
}
} // namespace splice_ir

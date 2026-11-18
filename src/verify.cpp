#include "compiler.h"
#include "llvm/Support/raw_ostream.h"

namespace splice_ir {
void verifyArchives(const std::filesystem::path &reference,
                    const std::filesystem::path &archive) {
  const auto expected = digest(reference);
  const auto actual = digest(archive);
  check(expected == actual,
        "NAR mismatch\nreference " + expected + "\nbuild     " + actual);
  llvm::outs() << "sha256:" << expected << "\n";
}
} // namespace splice_ir

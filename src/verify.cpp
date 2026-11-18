#include "compiler.h"
#include "nlohmann/json.hpp"
#include "process.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/SHA256.h"
#include <fstream>

namespace splice_ir {

std::string fileHash(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("cannot open " + path.string());
  }
  llvm::SHA256 hash;
  constexpr size_t chunk = 64 * 1024;
  std::array<char, chunk> bytes;
  while (file.read(bytes.data(), bytes.size()) || file.gcount() != 0) {
    hash.update(llvm::StringRef(bytes.data(), file.gcount()));
  }
  if (!file.eof()) {
    throw std::runtime_error("cannot read " + path.string());
  }
  return "sha256:" + llvm::toHex(hash.final(), /*LowerCase=*/true);
}

void verifyArchives(const std::filesystem::path &reportPath,
                    const std::filesystem::path &reference,
                    const std::filesystem::path &archive) {
  std::ifstream reportFile(reportPath);
  auto report = nlohmann::json::parse(reportFile);
  if (report.at("ending") != "exited:0" || report.at("outputs").size() != 1) {
    throw std::runtime_error(
        "expected a successful Rewind run with one output");
  }
  const std::string recorded = report.at("outputs").at(0).at("hash");
  const auto nix = fileHash(reference);
  const auto build = fileHash(archive);
  if (recorded != nix || nix != build) {
    throw std::runtime_error("NAR mismatch\nRewind " + recorded + "\nNix    " +
                             nix + "\nbuild  " + build);
  }
  llvm::outs() << nix << "\n";
}
} // namespace splice_ir

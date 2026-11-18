#pragma once
#include <filesystem>
namespace splice_ir {
void capturePackage(const std::filesystem::path &report,
                    const std::filesystem::path &drv,
                    const std::filesystem::path &bundle);
void replayAction(const std::filesystem::path &spec,
                  const std::filesystem::path &input,
                  const std::filesystem::path &output);
} // namespace splice_ir

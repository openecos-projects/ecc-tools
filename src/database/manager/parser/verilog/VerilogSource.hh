// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once

#include <string_view>
#include <unordered_map>
#include <vector>

#include "VerilogTypes.hh"

namespace idb::verilog {
struct SourceOptions
{
  std::vector<std::string> include_paths;
  std::unordered_map<std::string, std::string> defines;
  LanguageMode language = LanguageMode::verilog2005;
};
}  // namespace idb::verilog

namespace idb::verilog::detail {
struct SourceText
{
  std::string text;
  std::vector<std::string> files;
  std::vector<SourceLocation> lines;
  std::vector<Diagnostic> diagnostics;
};
SourceText preprocess(std::string_view text, const std::string& source, const SourceOptions& options);
SourceText preprocessFiles(const std::vector<std::string>& paths, const SourceOptions& options);
std::string readSource(const std::string& path);
}  // namespace idb::verilog::detail

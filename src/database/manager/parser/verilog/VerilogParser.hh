// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once

#include <memory>
#include <string_view>

#include "VerilogAst.hh"

namespace idb::verilog {
struct ParseResult
{
  std::unique_ptr<Design> design;
  std::vector<Diagnostic> diagnostics;
  explicit operator bool() const { return design != nullptr; }
};

// No logging, process termination, mutable global state or borrowed AST handles.
// The structural Verilog-2005 frontend rejects unsupported constructs explicitly.
ParseResult parse(std::string_view text, std::string source = "<memory>");
ParseResult readFile(const std::string& path);  // Plain text or gzip, without temporary files.

}  // namespace idb::verilog

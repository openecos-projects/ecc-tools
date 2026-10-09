// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once

#include "VerilogNetlist.hh"
#include "VerilogSource.hh"

namespace idb::verilog {
struct SyntaxTree;
// Every successful result owns its names, source map and connectivity. Neither
// source text, syntax objects nor library metadata are borrowed by the result.
NetlistResult compile(const SyntaxTree& syntax, std::string_view top = {}, const LibraryLookup& library = {});
NetlistResult compile(std::string_view text, std::string source = "<memory>", std::string_view top = {}, const SourceOptions& options = {},
                      const LibraryLookup& library = {});
NetlistResult compileFiles(const std::vector<std::string>& paths, std::string_view top = {}, const SourceOptions& options = {},
                           const LibraryLookup& library = {});
}  // namespace idb::verilog

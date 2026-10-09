// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "VerilogAst.hh"
#include "VerilogNetlist.hh"

namespace idb::verilog {
struct ElaborateResult
{
  std::unique_ptr<FlatDesign> design;
  std::vector<Diagnostic> diagnostics;
  explicit operator bool() const { return design != nullptr; }
};
struct PortShape
{
  Direction direction;
  uint32_t width;
};
using PortResolver = std::function<std::optional<PortShape>(std::string_view cell, std::string_view port)>;
// An empty top selects the unique uninstantiated module. AST remains unchanged.
// Optional library metadata supplies the context width of unresolved cell ports.
ElaborateResult elaborate(const Design& design, std::string_view top = {}, const PortResolver& resolve_port = {});

}  // namespace idb::verilog

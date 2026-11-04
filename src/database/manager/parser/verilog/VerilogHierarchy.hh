// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once

#include <functional>
#include <optional>
#include <unordered_map>

#include "VerilogExpression.hh"

namespace idb::verilog {
// Resolved packed object type: expressions and default port-kind rules have
// already been evaluated. This model deliberately has no net IDs or signals.
struct ObjectType
{
  std::optional<std::pair<int32_t, int32_t>> range;
  bool is_signed = false;
  bool two_state = false;
  bool is_variable = false;
  NetType net_type = NetType::wire;
  uint32_t width() const { return range ? uint32_t(std::abs(int64_t(range->first) - range->second) + 1) : 1; }
};
struct BoundSymbol
{
  ObjectType type;
  Direction direction = Direction::none;
  bool complete = true;
  SourceLocation location;
};
struct BoundScope;
struct ModuleInterface
{
  const Module& module;
  std::unordered_map<std::string_view, BoundSymbol> symbols;
};
struct ScopeExpansion
{
  std::string name;
  size_t array_count = 0;
  size_t offset = 0;
  std::unique_ptr<BoundScope> scope;
  std::shared_ptr<const ModuleInterface> interface;
};
struct BoundScope
{
  const ScopeSyntax& syntax;
  const Module* module = nullptr;  // A generate scope has no module interface.
  std::string prefix;
  detail::Constants constants;
  std::unordered_map<std::string_view, BoundSymbol> symbols;
  std::vector<std::string_view> declaration_order;
  const BoundScope* parent = nullptr;  // Lexical parent, never the caller of a module.
  std::string restricted_prefix;
  // Only expanded scopes, defined module interfaces and arrays need records.
  // Scalar external cells remain references to syntax, including million-cell netlists.
  std::unordered_map<const Statement*, std::vector<ScopeExpansion>> expansions;
};
struct Hierarchy
{
  const SyntaxTree& syntax;
  std::unique_ptr<BoundScope> top;
  std::unordered_map<std::string_view, const LibraryCell*> library;
};
struct HierarchyResult
{
  std::unique_ptr<Hierarchy> design;
  std::vector<Diagnostic> diagnostics;
  explicit operator bool() const { return design != nullptr; }
};
// The syntax and library interfaces must outlive the hierarchy. Normal clients
// use compile()/compileFiles(), whose owned netlist has no borrowed references.
HierarchyResult bindHierarchy(const SyntaxTree& syntax, std::string_view top = {}, const LibraryLookup& library = {});
}  // namespace idb::verilog

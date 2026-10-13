// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************

#include "VerilogNetlistImportService.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "VerilogReader.hh"
#include "connectivity/ConnectivityService.h"
#include "design/DesignStore.h"
#include "design/global/model/DesignGlobalComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "instance/InstanceService.h"
#include "library/LibraryStore.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_term/model/MasterTermComponents.h"

namespace eccdb {
namespace {

using ParsedFileGuard = std::unique_ptr<void, void (*)(void*)>;

[[nodiscard]] std::string cleanName(const char* raw)
{
  if (raw == nullptr) {
    return {};
  }
  std::string name(raw);
  if (!name.empty() && name.front() == '\\') {
    name.erase(name.begin());
  }
  while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) {
    name.pop_back();
  }
  return name;
}

[[nodiscard]] std::string idName(void* id)
{
  if (id == nullptr) {
    return {};
  }
  if (verilog_is_id(id)) {
    return cleanName(verilog_convert_id(id)->id);
  }
  if (verilog_is_bus_index_id(id)) {
    const auto* value = verilog_convert_index_id(id);
    return cleanName(value->base_id) + "[" + std::to_string(value->index) + "]";
  }
  if (verilog_is_bus_slice_id(id)) {
    const auto* value = verilog_convert_slice_id(id);
    return cleanName(value->base_id) + "[" + std::to_string(value->range_base) + ":"
           + std::to_string(value->range_max) + "]";
  }
  return {};
}

[[nodiscard]] std::vector<std::string> expandId(void* id)
{
  if (id == nullptr) {
    return {};
  }
  if (verilog_is_bus_slice_id(id)) {
    const auto* value = verilog_convert_slice_id(id);
    std::vector<std::string> result;
    const auto lower = std::min(value->range_base, value->range_max);
    const auto upper = std::max(value->range_base, value->range_max);
    result.reserve(static_cast<std::size_t>(upper - lower + 1));
    for (int index = lower; index <= upper; ++index) {
      result.push_back(cleanName(value->base_id) + "[" + std::to_string(index) + "]");
    }
    return result;
  }
  const auto name = idName(id);
  return name.empty() ? std::vector<std::string>{} : std::vector<std::string>{name};
}

[[nodiscard]] std::vector<std::string> expandExpr(void* expression, std::size_t line)
{
  if (expression == nullptr || verilog_is_constant(expression)) {
    return {};
  }
  if (verilog_is_id_expr(expression)) {
    const auto* value = verilog_convert_net_id_expr(expression);
    auto result = expandId(const_cast<void*>(value->verilog_id));
    if (result.empty()) {
      throw VerilogNetlistImportError(line, "invalid net identifier expression");
    }
    return result;
  }
  if (verilog_is_concat_expr(expression)) {
    auto* value = verilog_convert_net_concat_expr(expression);
    std::vector<std::string> result;
    void* child = nullptr;
    FOREACH_VERILOG_VEC_ELEM(&value->verilog_id_concat, void, child)
    {
      auto items = expandExpr(child, value->line_no);
      result.insert(result.end(), items.begin(), items.end());
    }
    if (result.empty()) {
      throw VerilogNetlistImportError(line, "empty concatenation is not a net expression");
    }
    return result;
  }
  throw VerilogNetlistImportError(line, "unsupported Verilog net expression");
}

struct Declaration
{
  DclType type = DclType::KWire;
  std::size_t line = 0;
  std::vector<std::string> names;
};

struct UnionFind
{
  std::unordered_map<std::string, std::string> parent;

  void add(const std::string& name)
  {
    parent.emplace(name, name);
  }

  [[nodiscard]] std::string find(const std::string& name)
  {
    auto it = parent.find(name);
    if (it == parent.end()) {
      add(name);
      return name;
    }
    if (it->second == name) {
      return name;
    }
    it->second = find(it->second);
    return it->second;
  }

  void join(const std::string& lhs, const std::string& rhs)
  {
    const auto left = find(lhs);
    const auto right = find(rhs);
    if (left != right) {
      parent[right] = left;
    }
  }
};

[[nodiscard]] std::vector<std::string> expandDeclarationName(const ParsedVerilogDcl& declaration)
{
  const auto base = cleanName(declaration.dcl_name);
  if (!declaration.range.has_value) {
    return base.empty() ? std::vector<std::string>{} : std::vector<std::string>{base};
  }
  const auto lower = std::min(declaration.range.start, declaration.range.end);
  const auto upper = std::max(declaration.range.start, declaration.range.end);
  std::vector<std::string> result;
  result.reserve(static_cast<std::size_t>(upper - lower + 1));
  for (int index = lower; index <= upper; ++index) {
    result.push_back(base + "[" + std::to_string(index) + "]");
  }
  return result;
}

// Parser module names retain their lexical escaped-identifier spelling.  The
// importer API accepts the corresponding semantic name, so compare after
// removing only the lexical leading escape marker and its terminator.
[[nodiscard]] ParsedVerilogModule* findModule(ParsedVerilogFile* file, std::string_view semantic_name)
{
  if (file == nullptr) {
    return nullptr;
  }
  void* module_ref = nullptr;
  FOREACH_VERILOG_VEC_ELEM(&file->verilog_modules, void, module_ref)
  {
    auto* module = verilog_convert_module(verilog_convert_module_ref(module_ref));
    if (module != nullptr && semantic_name == cleanName(module->module_name)) {
      return module;
    }
  }
  return nullptr;
}

[[nodiscard]] DesignIoPinDirection toDirection(DclType type)
{
  switch (type) {
    case DclType::KInput:
      return DesignIoPinDirection::kInput;
    case DclType::KOutput:
      return DesignIoPinDirection::kOutput;
    case DclType::KInout:
      return DesignIoPinDirection::kInOut;
    default:
      return DesignIoPinDirection::kNone;
  }
}

[[nodiscard]] DesignSignalUse declarationUse(DclType type)
{
  if (type == DclType::KSupply0) {
    return DesignSignalUse::kGround;
  }
  if (type == DclType::KSupply1) {
    return DesignSignalUse::kPower;
  }
  return DesignSignalUse::kSignal;
}

[[nodiscard]] std::vector<std::string> masterTermNames(const LibraryStore& library, LibraryCellMasterId master)
{
  std::vector<std::string> names;
  const auto& value = library.cellMasterStorage().cellMaster(master);
  names.reserve(value.terms.size());
  for (const auto term : value.terms) {
    names.push_back(library.masterTermStorage().masterTerm(term).name);
  }
  return names;
}

[[nodiscard]] std::vector<std::string> resolvePortNames(const LibraryStore& library,
                                                         LibraryCellMasterId master,
                                                         const std::vector<std::string>& requested,
                                                         std::size_t line)
{
  const auto terms = masterTermNames(library, master);
  std::vector<std::string> result;
  for (const auto& name : requested) {
    if (std::find(terms.begin(), terms.end(), name) != terms.end()) {
      result.push_back(name);
      continue;
    }
    const auto prefix = name + "[";
    std::vector<std::string> bus;
    for (const auto& term : terms) {
      if (term.starts_with(prefix) && !term.empty() && term.back() == ']') {
        bus.push_back(term);
      }
    }
    if (bus.empty()) {
      throw VerilogNetlistImportError(line, "instance port '" + name + "' is absent from the cell master");
    }
    std::sort(bus.begin(), bus.end());
    result.insert(result.end(), bus.begin(), bus.end());
  }
  return result;
}

}  // namespace

VerilogNetlistImportError::VerilogNetlistImportError(std::size_t line, std::string message)
    : std::runtime_error([&] {
        if (line == 0) {
          return message;
        }
        return "Verilog import error at line " + std::to_string(line) + ": " + message;
      }()),
      _line(line)
{
}

VerilogNetlistImportResult VerilogNetlistImportService::import(const std::filesystem::path& file, std::string_view top_module)
{
  VerilogNetlistImportOptions options{.file = file, .top_module = std::string(top_module)};
  return import(options);
}

VerilogNetlistImportResult VerilogNetlistImportService::import(const VerilogNetlistImportOptions& options)
{
  if (options.file.empty()) {
    throw VerilogNetlistImportError(0, "Verilog input path is empty");
  }
  ParsedFileGuard file_guard(verilog_parse_file(options.file.c_str()), &verilog_free_file);
  if (!file_guard) {
    throw VerilogNetlistImportError(0, "failed to parse Verilog file; parser diagnostics were emitted");
  }
  auto* parsed_file = verilog_convert_file(file_guard.get());
  if (parsed_file == nullptr || parsed_file->verilog_modules.len == 0u) {
    throw VerilogNetlistImportError(0, "Verilog file contains no module");
  }

  std::string top_name = options.top_module;
  if (top_name.empty()) {
    if (parsed_file->verilog_modules.len != 1u) {
      throw VerilogNetlistImportError(0, "top module is required when Verilog contains multiple modules");
    }
    void* module_ref = nullptr;
    FOREACH_VERILOG_VEC_ELEM(&parsed_file->verilog_modules, void, module_ref)
    {
      const auto* module = verilog_convert_module(verilog_convert_module_ref(module_ref));
      top_name = module == nullptr ? std::string{} : cleanName(module->module_name);
      break;
    }
  }
  if (top_name.empty()) {
    throw VerilogNetlistImportError(0, "top module name is empty");
  }
  auto* top = findModule(parsed_file, top_name);
  if (top == nullptr) {
    throw VerilogNetlistImportError(0, "top module '" + top_name + "' was not found");
  }
  if (options.flatten_hierarchy) {
    // The parser's flatten API looks up its module map by raw lexer spelling,
    // not by EccDB's semantic API name.  Passing top_name would silently skip
    // a top such as `module \\top ;`.
    verilog_flatten_module(file_guard.get(), top->module_name);
    parsed_file = verilog_convert_file(file_guard.get());
    top = findModule(parsed_file, top_name);
    if (top == nullptr) {
      throw VerilogNetlistImportError(0, "top module disappeared during flattening");
    }
  }

  auto& design = _context.design();
  auto* library = _context.library();
  if (library == nullptr) {
    throw VerilogNetlistImportError(0, "Verilog import requires a loaded EccDB library");
  }
  if (!design.globalStorage().hasInfo()) {
    design.globalStorage().setInfo(DesignInfo{.name = cleanName(top->module_name), .database_units_per_micron = 1000});
  }

  std::vector<Declaration> declarations;
  std::unordered_map<std::string, DclType> declaration_types;
  std::unordered_map<std::string, std::vector<std::string>> buses;
  UnionFind aliases;
  auto addSignal = [&](const std::string& name) {
    if (!name.empty()) aliases.add(name);
  };

  void* statement = nullptr;
  FOREACH_VERILOG_VEC_ELEM(&top->module_stmts, void, statement)
  {
    if (!verilog_is_dcls_stmt(statement)) {
      continue;
    }
    auto* group = verilog_convert_dcls(statement);
    void* declaration_handle = nullptr;
    FOREACH_VERILOG_VEC_ELEM(&group->verilog_dcls, void, declaration_handle)
    {
      const auto* declaration = verilog_convert_dcl(declaration_handle);
      if (declaration == nullptr) {
        throw VerilogNetlistImportError(group->line_no, "invalid declaration");
      }
      Declaration current{.type = declaration->dcl_type, .line = declaration->line_no,
                          .names = expandDeclarationName(*declaration)};
      for (const auto& name : current.names) {
        addSignal(name);
        declaration_types.emplace(name, current.type);
        const auto bracket = name.find('[');
        if (bracket != std::string::npos) {
          buses[name.substr(0, bracket)].push_back(name);
        }
      }
      declarations.push_back(std::move(current));
    }
  }

  // Collect aliases before creating EccDB nets so an assign does not require a
  // destructive net merge after endpoints have been connected.
  std::size_t alias_count = 0;
  FOREACH_VERILOG_VEC_ELEM(&top->module_stmts, void, statement)
  {
    if (!verilog_is_module_assign_stmt(statement)) {
      continue;
    }
    const auto* assign = verilog_convert_assign(statement);
    auto left = expandExpr(const_cast<void*>(assign->left_net_expr), assign->line_no);
    auto right = expandExpr(const_cast<void*>(assign->right_net_expr), assign->line_no);
    if (left.empty() || right.empty()) {
      continue;  // Constant tie-offs have no EccDB connectivity endpoint.
    }
    if (left.size() != right.size()) {
      throw VerilogNetlistImportError(assign->line_no, "assign alias widths do not match");
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
      addSignal(left[index]);
      addSignal(right[index]);
      aliases.join(left[index], right[index]);
      ++alias_count;
    }
  }

  // A scalar reference to a declared bus denotes the whole bus in structural
  // netlists. Expand it using declaration information before connectivity.
  const auto expandSignal = [&](const std::string& name) {
    if (const auto found = buses.find(name); found != buses.end()) {
      return found->second;
    }
    return std::vector<std::string>{name};
  };
  for (const auto& declaration : declarations) {
    for (const auto& name : declaration.names) {
      for (const auto& expanded : expandSignal(name)) {
        addSignal(expanded);
      }
    }
  }

  // Validate all instance references and collect implicit structural nets
  // before mutating EccDB.  This makes the common failure cases (missing
  // master, missing port, width mismatch) fail before any net or term is
  // created, and also supports Verilog's implicit scalar-wire convention.
  std::unordered_set<std::string> instance_names;
  FOREACH_VERILOG_VEC_ELEM(&top->module_stmts, void, statement)
  {
    if (!verilog_is_module_inst_stmt(statement)) {
      continue;
    }
    auto* parsed_instance = verilog_convert_inst(statement);
    const auto instance_name = cleanName(parsed_instance->inst_name);
    if (instance_name.empty() || !instance_names.emplace(instance_name).second) {
      throw VerilogNetlistImportError(parsed_instance->line_no,
                                      "duplicate or empty instance name '" + instance_name + "'");
    }
    const auto master_name = cleanName(parsed_instance->cell_name);
    const auto master = library->cellMasterStorage().findCellMaster(master_name);
    if (!master) {
      throw VerilogNetlistImportError(parsed_instance->line_no, "cell master '" + master_name + "' was not found");
    }

    void* connection_handle = nullptr;
    FOREACH_VERILOG_VEC_ELEM(&parsed_instance->port_connections, void, connection_handle)
    {
      auto* connection = verilog_convert_port_ref_port_connect(connection_handle);
      if (connection == nullptr || connection->port_id == nullptr) {
        throw VerilogNetlistImportError(parsed_instance->line_no, "invalid named port connection");
      }
      const auto port_names = resolvePortNames(*library, master,
                                               expandId(const_cast<void*>(connection->port_id)),
                                               parsed_instance->line_no);
      if (connection->net_expr == nullptr) {
        continue;
      }
      const auto net_names = expandExpr(connection->net_expr, parsed_instance->line_no);
      std::vector<std::string> expanded_nets;
      for (const auto& net_name : net_names) {
        auto expanded = expandSignal(net_name);
        for (const auto& signal : expanded) {
          addSignal(signal);
        }
        expanded_nets.insert(expanded_nets.end(), expanded.begin(), expanded.end());
      }
      if (port_names.size() != expanded_nets.size()) {
        throw VerilogNetlistImportError(parsed_instance->line_no, "named port connection widths do not match");
      }
    }
  }

  ConnectivityService connectivity(_context);
  InstanceService instances(_context);
  auto& netlist = design.netlistStorage();
  std::unordered_map<std::string, DesignNetId> net_ids;
  for (const auto& [name, unused] : aliases.parent) {
    static_cast<void>(unused);
    const auto root = aliases.find(name);
    if (net_ids.contains(root)) {
      continue;
    }
    DesignSignalUse use = DesignSignalUse::kSignal;
    if (const auto type = declaration_types.find(name); type != declaration_types.end()) {
      use = declarationUse(type->second);
    }
    net_ids.emplace(root, connectivity.createNet(DesignNet{.name = root, .use = use, .source = DesignNetSource::kNetlist}));
  }
  const auto netFor = [&](const std::string& name) -> DesignNetId {
    const auto root = aliases.find(name);
    const auto found = net_ids.find(root);
    if (found == net_ids.end()) {
      throw VerilogNetlistImportError(0, "internal net index is missing for '" + name + "'");
    }
    return found->second;
  };

  std::unordered_map<std::string, DesignBlockTermId> block_terms;
  for (const auto& declaration : declarations) {
    if (declaration.type != DclType::KInput && declaration.type != DclType::KOutput && declaration.type != DclType::KInout) {
      continue;
    }
    for (const auto& declared_name : declaration.names) {
      for (const auto& name : expandSignal(declared_name)) {
        if (block_terms.contains(name)) {
          continue;
        }
        const auto term = connectivity.createBlockTerm(DesignBlockTerm{.name = name, .direction = toDirection(declaration.type),
                                                                         .use = DesignSignalUse::kSignal});
        connectivity.connect(term, netFor(name));
        block_terms.emplace(name, term);
      }
    }
  }

  std::size_t instance_count = 0;
  std::size_t term_count = 0;
  FOREACH_VERILOG_VEC_ELEM(&top->module_stmts, void, statement)
  {
    if (!verilog_is_module_inst_stmt(statement)) {
      continue;
    }
    auto* parsed_instance = verilog_convert_inst(statement);
    const auto instance_name = cleanName(parsed_instance->inst_name);
    const auto master_name = cleanName(parsed_instance->cell_name);
    const auto master = library->cellMasterStorage().findCellMaster(master_name);
    if (!master) {
      throw VerilogNetlistImportError(parsed_instance->line_no, "cell master '" + master_name + "' was not found");
    }
    const auto instance = instances.create(DesignInstance{.name = instance_name,
                                                            .master = master,
                                                            .source = DesignInstanceSource::kNetlist});
    ++instance_count;
    void* connection_handle = nullptr;
    FOREACH_VERILOG_VEC_ELEM(&parsed_instance->port_connections, void, connection_handle)
    {
    auto* connection = verilog_convert_port_ref_port_connect(connection_handle);
      if (connection == nullptr || connection->port_id == nullptr) {
        throw VerilogNetlistImportError(parsed_instance->line_no, "invalid named port connection");
      }
      const auto port_names = resolvePortNames(*library, master, expandId(const_cast<void*>(connection->port_id)), parsed_instance->line_no);
      if (connection->net_expr == nullptr) {
        continue;
      }
      const auto net_names = expandExpr(connection->net_expr, parsed_instance->line_no);
      std::vector<std::string> expanded_nets;
      for (const auto& net_name : net_names) {
        auto expanded = expandSignal(net_name);
        expanded_nets.insert(expanded_nets.end(), expanded.begin(), expanded.end());
      }
      if (port_names.size() != expanded_nets.size()) {
        throw VerilogNetlistImportError(parsed_instance->line_no, "named port connection widths do not match");
      }
      for (std::size_t index = 0; index < port_names.size(); ++index) {
        const auto term = netlist.findInstanceTerm(instance, port_names[index]);
        if (!term) {
          throw VerilogNetlistImportError(parsed_instance->line_no, "materialized instance terminal '" + port_names[index] + "' is missing");
        }
        connectivity.connect(term, netFor(expanded_nets[index]));
        ++term_count;
      }
    }
  }

  return VerilogNetlistImportResult{.top_module = top_name,
                                    .instances = instance_count,
                                    .instance_terms = term_count,
                                    .block_terms = block_terms.size(),
                                    .nets = net_ids.size(),
                                    .aliases = alias_count};
}

}  // namespace eccdb

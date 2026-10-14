// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include "VerilogNetlist.hh"

#include <algorithm>
#include <unordered_set>

#include "VerilogHierarchy.hh"

namespace idb::verilog {
namespace {
using detail::Constant;
using detail::Constants;
using detail::Error;
using detail::integer;
using detail::rangeWidth;
struct ConnectedSymbol
{
  const BoundSymbol& declaration;
  Signal signal;
};
struct ConnectedScope
{
  const BoundScope& bound;
  const std::string& prefix;
  const Constants& constants;
  const ConnectedScope* parent;
  std::unordered_map<std::string_view, ConnectedSymbol> symbols;
  explicit ConnectedScope(const BoundScope& scope, const ConnectedScope* enclosing = nullptr)
      : bound(scope), prefix(scope.prefix), constants(scope.constants), parent(enclosing)
  {
  }
};
struct VariableDriver
{
  bool driven = false;
  bool input = false;
  SourceLocation location;
};
NetId constantBit(char bit)
{
  if (bit == '0')
    return zeroBit;
  if (bit == '1')
    return oneBit;
  return bit == 'x' ? xBit : zBit;
}
std::string indexed(const std::string& name, int64_t index)
{
  return name + "[" + std::to_string(index) + "]";
}
PortShape shapeOf(const BoundSymbol& symbol)
{
  const auto& type = symbol.type;
  return {symbol.direction, type.width(), type.is_signed, type.two_state, type.net_type, type.is_variable, type.range};
}
class ConnectivityLowerer
{
 public:
  explicit ConnectivityLowerer(const Hierarchy& hierarchy) : _hierarchy(hierarchy), _ast(hierarchy.syntax), _constants(_ast)
  {
    _flat.source = _ast.source;
    _flat.source_files = _ast.source_files;
    _flat.top = hierarchy.top->module->name;
  }
  Netlist run()
  {
    ConnectedScope top(*_hierarchy.top);
    allocate(top);
    for (const auto& port : top.bound.module->ports) {
      const auto& symbol = top.symbols.at(port);
      _flat.ports.push_back({port, symbol.declaration.direction, symbol.declaration.type.range, symbol.signal});
      if (symbol.declaration.type.is_variable && symbol.declaration.direction == Direction::input)
        drive(symbol.signal, top.bound.syntax.location, true);
    }
    connect(top);
    for (const auto& [bit, driver] : _variable_drivers)
      if (!driver.driven)
        _flat.assignments.push_back({bit, xBit, driver.location});
    return std::move(_flat);
  }

 private:
  std::optional<PortShape> libraryPort(std::string_view cell, std::string_view port) const
  {
    const auto found = _hierarchy.library.find(cell);
    if (found == _hierarchy.library.end() || !found->second)
      return {};
    const auto entry = found->second->ports.find(std::string(port));
    return entry == found->second->ports.end() ? std::nullopt : std::optional<PortShape>(entry->second);
  }
  bool hasLibrary(std::string_view cell) const
  {
    const auto found = _hierarchy.library.find(cell);
    return found != _hierarchy.library.end() && found->second;
  }
  const ConnectedSymbol* symbol(const ConnectedScope& scope, std::string_view name) const
  {
    for (auto* current = &scope; current; current = current->parent) {
      const auto found = current->symbols.find(name);
      if (found != current->symbols.end())
        return &found->second;
      if (current->constants.count(name))
        return nullptr;
    }
    return nullptr;
  }
  const Expression& expr(ExprId id) const { return _ast.expressions.at(id); }
  NetId net(const std::string& name, NetType type, SourceLocation location)
  {
    if (_flat.nets.size() >= zBit)
      throw Error(location, "net count exceeds frontend limit");
    const auto id = static_cast<NetId>(_flat.nets.size());
    if (!_net_names.emplace(name).second)
      throw Error(location, "flattened net name collision: " + name);
    _flat.nets.push_back({name, type});
    return id;
  }
  Signal makeSignal(ConnectedScope& scope, const std::string& name, NetType type, std::optional<std::pair<int32_t, int32_t>> range,
                    bool sign, SourceLocation location)
  {
    Signal signal;
    signal.is_signed = sign;
    const auto full = scope.prefix + name;
    if (!range)
      signal.bits.push_back(net(full, type, location));
    else {
      const auto [left, right] = *range;
      const auto width = rangeWidth(left, right, location);
      signal.bits.reserve(width);
      for (size_t i = 0; i < width; ++i)
        signal.bits.push_back(net(indexed(full, int64_t(left) + (left <= right ? int64_t(i) : -int64_t(i))), type, location));
      _flat.buses.push_back({full, left, right, signal.bits});
    }
    return signal;
  }
  void allocate(ConnectedScope& scope)
  {
    scope.symbols.reserve(scope.bound.symbols.size());
    for (auto name : scope.bound.declaration_order) {
      const auto& declaration = scope.bound.symbols.at(name);
      const auto& type = declaration.type;
      auto signal = makeSignal(scope, std::string(name), type.net_type, type.range, type.is_signed, declaration.location);
      if (type.is_variable)
        for (auto bit : signal.bits)
          _variable_drivers.emplace(bit, VariableDriver{false, declaration.direction == Direction::input, declaration.location});
      scope.symbols.emplace(name, ConnectedSymbol{declaration, std::move(signal)});
    }
  }
  Signal signal(ExprId id, ConnectedScope& scope, bool lvalue = false, uint32_t context_width = 0)
  {
    const auto& e = expr(id);
    if (e.kind == ExpressionKind::reference) {
      if (const auto* found = symbol(scope, e.text)) {
        if (found->declaration.type.is_variable && found->declaration.location.order >= e.location.order)
          throw Error(e.location, "variable reference requires a prior declaration: " + e.text);
        return found->signal;
      }
      if (!scope.constants.count(e.text))
        throw Error(e.location, "undeclared net or parameter: " + e.text);
    }
    if (e.kind == ExpressionKind::concatenate || e.kind == ExpressionKind::repeat) {
      Signal result;
      size_t count = 1;
      const auto* parts = &e.operands;
      if (e.kind == ExpressionKind::repeat) {
        if (lvalue)
          throw Error(e.location, "repetition is not a net lvalue");
        const auto repeat = integer(_constants.requiredConstant(e.operands[0], scope.constants), e.location);
        if (repeat <= 0 || repeat > BitVector::kMaxWidth)
          throw Error(e.location, "unsupported zero or excessive repetition count");
        count = static_cast<size_t>(repeat);
        parts = &expr(e.operands[1]).operands;
      }
      for (auto operand : *parts) {
        if (auto value = _constants.constant(operand, scope.constants); value && value->isUnsized())
          throw Error(expr(operand).location, "unsized constant in concatenation");
        auto part = signal(operand, scope, lvalue);
        if (result.bits.size() + part.bits.size() > BitVector::kMaxWidth)
          throw Error(e.location, "concatenation width exceeds frontend limit");
        result.bits.insert(result.bits.end(), part.bits.begin(), part.bits.end());
      }
      if (result.bits.empty() || count > BitVector::kMaxWidth / result.bits.size())
        throw Error(e.location, "concatenation width exceeds frontend limit");
      const auto width = result.bits.size();
      result.bits.reserve(width * count);
      for (size_t i = 1; i < count; ++i)
        for (size_t bit = 0; bit < width; ++bit)
          result.bits.push_back(result.bits[bit]);
      return result;
    }
    if (e.kind == ExpressionKind::select || e.kind == ExpressionKind::slice || e.kind == ExpressionKind::indexedUp
        || e.kind == ExpressionKind::indexedDown) {
      const auto& base = expr(e.operands[0]);
      const auto* found = symbol(scope, base.text);
      if (found) {
        if (found->declaration.type.is_variable && found->declaration.location.order >= e.location.order)
          throw Error(e.location, "variable reference requires a prior declaration: " + base.text);
        if (!found->declaration.type.range)
          throw Error(e.location, "cannot select a bit from a scalar net: " + base.text);
        const auto range = *found->declaration.type.range;
        Signal result;
        for (auto index : _constants.selectIndices(e, scope.constants, range)) {
          const auto offset = range.first >= range.second ? int64_t(range.first) - index : index - range.first;
          if (offset < 0 || offset >= int64_t(found->signal.bits.size())) {
            if (lvalue)
              throw Error(e.location, "out-of-range net lvalue is not supported");
            result.bits.push_back(xBit);
          } else
            result.bits.push_back(found->signal.bits[offset]);
        }
        return result;
      }
    }
    if (!lvalue && e.kind == ExpressionKind::call && (e.text == "$signed" || e.text == "$unsigned")) {
      auto result = signal(e.operands[0], scope);
      result.is_signed = e.text == "$signed";
      result.unsized = false;
      return result;
    }
    if (lvalue)
      throw Error(e.location, "expected net lvalue");
    auto value = _constants.constant(id, scope.constants, context_width);
    if (!value)
      throw Error(e.location, "nonconstant operator expression requires synthesis before physical netlist import");
    Signal result;
    result.is_signed = value->isSigned();
    result.unsized = value->isUnsized();
    for (auto bit : value->bits())
      result.bits.push_back(constantBit(bit));
    return result;
  }
  void drive(const Signal& signal, SourceLocation location, bool port_binding = false)
  {
    if (_variable_drivers.empty())
      return;
    for (auto bit : signal.bits) {
      const auto found = _variable_drivers.find(bit);
      if (found != _variable_drivers.end()) {
        if (found->second.input && !port_binding)
          throw Error(location, "assignment to input variable is not allowed: " + _flat.nets[bit].name);
        if (found->second.driven)
          throw Error(location, "multiple structural drivers of variable: " + _flat.nets[bit].name);
        found->second.driven = true;
      }
    }
  }
  void requireNets(const Signal& signal, SourceLocation location)
  {
    for (auto bit : signal.bits)
      if (_variable_drivers.count(bit))
        throw Error(location, "inout connection requires nets, found variable: " + _flat.nets[bit].name);
  }
  void assign(const Signal& left, Signal right, SourceLocation location, bool port_binding = false)
  {
    drive(left, location, port_binding);
    right = resized(std::move(right), left.bits.size());
    for (size_t i = 0; i < left.bits.size(); ++i) {
      if (isConstant(left.bits[i]))
        throw Error(location, "expected net lvalue");
      _flat.assignments.push_back({left.bits[i], right.bits[i], location});
    }
  }
  Signal arrayConnection(Signal signal, size_t width, size_t count, size_t offset, SourceLocation location)
  {
    if (count == 0 || signal.bits.empty())
      return signal;
    if (signal.bits.size() == width)
      return signal;
    if (width > BitVector::kMaxWidth / count || signal.bits.size() != width * count)
      throw Error(location, "instance array port width must equal one port or the complete array width");
    const auto begin = signal.bits.begin() + width * offset;
    signal.bits = std::vector<NetId>(begin, begin + width);
    signal.is_signed = false;
    return signal;
  }
  // Wildcards need a Verilog interface: LEF pin order is not a module port list.
  std::optional<std::vector<Connection>> wildcardPorts(const Instance& instance, const Module* module)
  {
    std::vector<Connection> ports;
    const auto wildcard
        = std::find_if(instance.ports.begin(), instance.ports.end(), [](const auto& p) { return p.kind == ConnectionKind::wildcard; });
    if (wildcard == instance.ports.end())
      return std::nullopt;
    if (!module)
      throw Error(wildcard->location, "wildcard connection requires a Verilog module interface");
    std::unordered_set<std::string_view> explicit_names;
    for (const auto& port : instance.ports)
      if (port.kind != ConnectionKind::wildcard) {
        explicit_names.insert(port.name);
        ports.push_back(port);
      }
    for (const auto& name : module->ports)
      if (!explicit_names.count(name))
        ports.push_back({wildcard->location, name, noExpr, ConnectionKind::implicit});
    return ports;
  }
  Signal actualSignal(const Connection& port, ConnectedScope& scope, const std::optional<PortShape>& shape, uint32_t context_width)
  {
    const bool output = shape && shape->direction != Direction::input;
    if (port.kind == ConnectionKind::expression)
      return port.expression == noExpr ? Signal{} : signal(port.expression, scope, output, context_width);
    if (!shape)
      throw Error(port.location, "implicit connection requires a known module or library port interface");
    Signal value;
    bool two_state = false;
    SourceLocation declaration;
    if (const auto* found = symbol(scope, port.name)) {
      value = found->signal;
      declaration = found->declaration.location;
      if (!found->declaration.type.is_variable && !shape->is_variable
          && ((found->declaration.type.net_type == NetType::wand && shape->net_type == NetType::wor)
              || (found->declaration.type.net_type == NetType::wor && shape->net_type == NetType::wand)))
        throw Error(port.location, "implicit connection between dissimilar net types: " + port.name);
    } else if (const auto found = scope.constants.find(port.name); found != scope.constants.end()) {
      if (output)
        throw Error(port.location, "output connection requires an lvalue: " + port.name);
      declaration = found->second.location;
      two_state = found->second.two_state;
      value.is_signed = found->second.value.isSigned();
      for (auto bit : found->second.value.bits())
        value.bits.push_back(constantBit(bit));
    } else
      throw Error(port.location, "implicit connection name has not been declared: " + port.name);
    if (declaration.order >= port.location.order)
      throw Error(port.location, "implicit connection requires a prior declaration: " + port.name);
    if (value.bits.size() != shape->width || value.is_signed != shape->is_signed || two_state != shape->two_state)
      throw Error(port.location, "implicit connection requires equivalent data types: " + port.name);
    return value;
  }
  void externalInstance(const Instance& instance, ConnectedScope& scope, const std::string& name, size_t array_count, size_t offset)
  {
    if (!instance.parameters.empty())
      throw Error(instance.location, "cannot apply parameter overrides to unresolved cell: " + instance.type);
    // Validates unknown-interface wildcards even when no ports would be emitted.
    wildcardPorts(instance, nullptr);
    CellInstance cell{instance.location, instance.type, scope.prefix + name, {}};
    for (const auto& port : instance.ports) {
      const auto shape = libraryPort(instance.type, port.name);
      if (array_count && !shape)
        throw Error(port.location, "instance array requires a known module or library port width");
      if (shape && (shape->width == 0 || shape->width > BitVector::kMaxWidth))
        throw Error(port.location, "invalid external port width");
      const auto width = !array_count && shape && shape->direction == Direction::input ? shape->width : 0;
      auto actual = actualSignal(port, scope, shape, width);
      if (shape)
        actual = arrayConnection(std::move(actual), shape->width, array_count, offset, port.location);
      if (shape && shape->direction == Direction::inout)
        requireNets(actual, port.location);
      if (shape && shape->direction == Direction::output)
        drive(actual, port.location);
      if (!shape)
        for (auto bit : actual.bits)
          if (_variable_drivers.count(bit))
            throw Error(port.location, "variable connection requires a known module or library port direction");
      cell.ports.push_back({port.location, port.name, std::move(actual)});
    }
    appendCell(std::move(cell));
  }
  void appendCell(CellInstance cell)
  {
    if (!_instance_names.insert(cell.name).second)
      throw Error(cell.location, "flattened instance name collision: " + cell.name);
    _flat.instances.push_back(std::move(cell));
  }
  void moduleInstance(const Instance& instance, const ScopeExpansion& expansion, ConnectedScope& parent)
  {
    const auto& module = expansion.interface ? expansion.interface->module : *expansion.scope->module;
    const auto expanded = wildcardPorts(instance, &module);
    const auto& ports = expanded ? *expanded : instance.ports;
    if (expansion.interface) {
      libraryInstance(instance, expansion.name, ports, *expansion.interface, parent, expansion.array_count, expansion.offset);
      return;
    }
    ConnectedScope child(*expansion.scope);
    allocate(child);
    std::unordered_set<std::string_view> formal_names(module.ports.begin(), module.ports.end());
    for (size_t i = 0; i < ports.size(); ++i) {
      const auto& actual = ports[i];
      std::string_view name = actual.name;
      if (name.empty()) {
        if (i >= module.ports.size())
          throw Error(actual.location, "too many positional port connections");
        name = module.ports[i];
      }
      if (!formal_names.count(name))
        throw Error(actual.location, "unknown module port: " + std::string(name));
      if (actual.expression == noExpr && actual.kind == ConnectionKind::expression)
        continue;
      const auto& formal = child.symbols.at(name);
      const auto shape = shapeOf(formal.declaration);
      auto value = actualSignal(actual, parent, shape, !expansion.array_count && shape.direction == Direction::input ? shape.width : 0);
      value = arrayConnection(std::move(value), shape.width, expansion.array_count, expansion.offset, actual.location);
      if (shape.direction == Direction::input)
        assign(formal.signal, std::move(value), actual.location, true);
      else if (shape.direction == Direction::output)
        assign(value, formal.signal, actual.location);
      else {
        requireNets(value, actual.location);
        if (value.bits.size() != shape.width)
          throw Error(actual.location, "unequal-width inout connections are not supported");
        for (size_t bit = 0; bit < value.bits.size(); ++bit)
          _flat.inouts.push_back({formal.signal.bits[bit], value.bits[bit], actual.location});
      }
    }
    connect(child);
  }
  void libraryInstance(const Instance& instance, const std::string& name, const std::vector<Connection>& ports,
                       const ModuleInterface& interface, ConnectedScope& parent, size_t array_count, size_t offset)
  {
    const auto& module = interface.module;
    // Validate the declaration even when an instance leaves its ports open.
    if (hasLibrary(instance.type))
      for (const auto& port : module.ports) {
        const auto shape = libraryPort(instance.type, port);
        const auto& formal = interface.symbols.at(port);
        if (!shape || shape->direction != formal.direction || shape->width != formal.type.width())
          throw Error(instance.location, "module interface does not match library port: " + instance.type + "." + port);
        if (shape->range
            && (!formal.type.range
                || std::minmax(formal.type.range->first, formal.type.range->second)
                       != std::minmax(shape->range->first, shape->range->second)))
          throw Error(instance.location, "module interface indices do not match library port: " + instance.type + "." + port);
      }
    CellInstance cell{instance.location, instance.type, parent.prefix + name, {}};
    for (size_t i = 0; i < ports.size(); ++i) {
      auto actual = ports[i];
      if (actual.name.empty()) {
        if (i >= module.ports.size())
          throw Error(actual.location, "too many positional port connections");
        actual.name = module.ports[i];
      }
      const auto found = interface.symbols.find(actual.name);
      if (found == interface.symbols.end() || found->second.direction == Direction::none)
        throw Error(actual.location, "unknown module port: " + actual.name);
      const auto& formal = found->second;
      const PortShape shape{formal.direction,     uint32_t(formal.type.width()), formal.type.is_signed, formal.type.two_state,
                            formal.type.net_type, formal.type.is_variable,       formal.type.range};
      auto value = actualSignal(actual, parent, shape, !array_count && shape.direction == Direction::input ? shape.width : 0);
      value = arrayConnection(std::move(value), shape.width, array_count, offset, actual.location);
      if (!value.bits.empty()) {
        if (shape.direction == Direction::input)
          value = resized(std::move(value), shape.width);
        else if (value.bits.size() != shape.width)
          throw Error(actual.location, "library output/inout connection width differs from module interface");
        if (shape.direction == Direction::output)
          drive(value, actual.location);
        else if (shape.direction == Direction::inout)
          requireNets(value, actual.location);
        // Values follow the Verilog declaration's left-to-right order; LEF may have the opposite index order.
        const auto physical = libraryPort(instance.type, actual.name);
        if (physical && physical->range && formal.type.range && physical->range->first != formal.type.range->first)
          std::reverse(value.bits.begin(), value.bits.end());
      }
      cell.ports.push_back({actual.location, std::move(actual.name), std::move(value)});
    }
    appendCell(std::move(cell));
  }
  void connect(ConnectedScope& scope)
  {
    for (const auto& declaration : scope.bound.syntax.declarations)
      if (declaration.initializer != noExpr) {
        const auto& left = scope.symbols.at(declaration.name).signal;
        assign(left, signal(declaration.initializer, scope, false, left.bits.size()), declaration.location);
      }
    for (const auto& statement : scope.bound.syntax.statements) {
      if (const auto* assignment = std::get_if<Assignment>(&statement)) {
        const auto left = signal(assignment->left, scope, true);
        assign(left, signal(assignment->right, scope, false, left.bits.size()), assignment->location);
        continue;
      }
      const auto* instance = std::get_if<Instance>(&statement);
      const auto found = scope.bound.expansions.find(&statement);
      if (found == scope.bound.expansions.end()) {
        if (instance)
          externalInstance(*instance, scope, instance->name, 0, 0);
        continue;
      }
      for (const auto& expansion : found->second) {
        if (!instance) {
          ConnectedScope block(*expansion.scope, &scope);
          allocate(block);
          connect(block);
        } else if (expansion.scope || expansion.interface)
          moduleInstance(*instance, expansion, scope);
        else
          externalInstance(*instance, scope, expansion.name, expansion.array_count, expansion.offset);
      }
    }
  }
  const Hierarchy& _hierarchy;
  const SyntaxTree& _ast;
  Netlist _flat;
  std::unordered_set<std::string> _net_names;
  std::unordered_set<std::string> _instance_names;
  std::unordered_map<NetId, VariableDriver> _variable_drivers;
  detail::ExpressionEvaluator _constants;
};
}  // namespace
NetlistResult lower(const Hierarchy& hierarchy)
{
  NetlistResult result;
  try {
    result.design = std::make_unique<Netlist>(ConnectivityLowerer(hierarchy).run());
  } catch (const Error& error) {
    const auto& syntax = hierarchy.syntax;
    result.diagnostics.push_back(
        {error.location.file < syntax.source_files.size() ? syntax.source_files[error.location.file] : syntax.source, error.location,
         error.what()});
  }
  return result;
}
}  // namespace idb::verilog

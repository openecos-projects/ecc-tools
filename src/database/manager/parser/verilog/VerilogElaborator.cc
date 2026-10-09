// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include "VerilogElaborator.hh"

#include <algorithm>
#include <limits>
#include <numeric>
#include <unordered_set>

#include "VerilogConstEval.hh"

namespace idb::verilog {
namespace {
using detail::Constant;
using detail::Constants;
using detail::Error;
using detail::integer;
using detail::rangeWidth;
struct Symbol
{
  Signal signal;
  std::optional<std::pair<int32_t, int32_t>> range;
  Direction direction;
  NetType type;
  bool net_declaration;
};
struct Scope
{
  const Module& module;
  std::string prefix;
  Constants constants;
  std::unordered_map<std::string_view, Symbol> symbols;
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
class Elaborator
{
 public:
  explicit Elaborator(const Design& ast, const PortResolver& resolve) : _ast(ast), _resolve_port(resolve), _constants(ast)
  {
    _flat.source = ast.source;
  }
  FlatDesign run(std::string_view requested)
  {
    for (const auto& module : _ast.modules) {
      if (!_modules.emplace(module.name, &module).second)
        throw Error(module.location, "duplicate module: " + module.name);
    }
    if (requested.empty()) {
      std::unordered_set<std::string_view> instantiated;
      for (const auto& module : _ast.modules)
        for (const auto& statement : module.statements)
          if (const auto* instance = std::get_if<Instance>(&statement))
            instantiated.insert(instance->type);
      for (const auto& module : _ast.modules)
        if (!instantiated.count(module.name)) {
          if (!requested.empty())
            throw Error(module.location, "ambiguous top: multiple uninstantiated modules");
          requested = module.name;
        }
      if (requested.empty())
        throw Error({}, "no top module (recursive hierarchy)");
    }
    const auto found = _modules.find(requested);
    if (found == _modules.end())
      throw Error({}, "top module not found: " + std::string(requested));
    _flat.top = std::string(requested);
    Scope top{*found->second, {}, {}, {}};
    prepare(top, {});
    for (const auto& port : top.module.ports) {
      const auto& symbol = top.symbols.at(port);
      _flat.ports.push_back({port, symbol.direction, symbol.range, symbol.signal});
    }
    expand(top);
    return std::move(_flat);
  }

 private:
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
  Signal makeSignal(Scope& scope, const std::string& name, NetType type, std::optional<std::pair<int32_t, int32_t>> range, bool sign,
                    SourceLocation location)
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
  void prepare(Scope& scope, const std::unordered_map<std::string_view, ExprId>& overrides, const Constants* actual_context = nullptr)
  {
    for (const auto& parameter : scope.module.parameters) {
      if (scope.constants.count(parameter.name))
        throw Error(parameter.location, "duplicate parameter: " + parameter.name);
      auto range = _constants.bounds(parameter.range, scope.constants);
      const auto override = overrides.find(parameter.name);
      if (parameter.integer)
        range = std::pair<int32_t, int32_t>{31, 0};
      const auto width = range ? rangeWidth(range->first, range->second, parameter.location) : 0;
      auto value = override != overrides.end() ? _constants.requiredConstant(override->second, *actual_context, width)
                                               : _constants.requiredConstant(parameter.value, scope.constants, width);
      if (range)
        value = value.resized(rangeWidth(range->first, range->second, parameter.location));
      if (range || parameter.is_signed)
        value = BitVector(std::string(value.bits()), parameter.is_signed);
      value = BitVector(std::string(value.bits()), value.isSigned());
      scope.constants.emplace(parameter.name, Constant{std::move(value), range});
    }
    scope.symbols.reserve(scope.module.declarations.size());
    for (const auto& declaration : scope.module.declarations) {
      if (scope.constants.count(declaration.name))
        throw Error(declaration.location, "net conflicts with parameter: " + declaration.name);
      const auto range = _constants.bounds(declaration.range, scope.constants);
      auto found = scope.symbols.find(declaration.name);
      if (found != scope.symbols.end()) {
        auto& symbol = found->second;
        if (scope.module.ansi_ports || (declaration.direction == Direction::none && symbol.net_declaration)
            || (declaration.direction != Direction::none && declaration.explicit_type && symbol.net_declaration)
            || (declaration.direction != Direction::none && symbol.direction != Direction::none))
          throw Error(declaration.location, "duplicate declaration: " + declaration.name);
        if (range != symbol.range)
          throw Error(declaration.location, "inconsistent port/net declaration range: " + declaration.name);
        if (declaration.direction != Direction::none)
          symbol.direction = declaration.direction;
        else {
          symbol.net_declaration = true;
          symbol.type = declaration.type;
          for (auto bit : symbol.signal.bits) {
            _flat.nets[bit].type = declaration.type;
          }
        }
        symbol.signal.is_signed = symbol.signal.is_signed || declaration.is_signed;
      } else {
        auto signal = makeSignal(scope, declaration.name, declaration.type, range, declaration.is_signed, declaration.location);
        scope.symbols.emplace(declaration.name, Symbol{std::move(signal), range, declaration.direction, declaration.type,
                                                       declaration.direction == Direction::none || declaration.explicit_type});
      }
    }
    std::unordered_set<std::string_view> ports(scope.module.ports.begin(), scope.module.ports.end());
    for (const auto& name : scope.module.ports) {
      const auto found = scope.symbols.find(name);
      if (found == scope.symbols.end() || found->second.direction == Direction::none)
        throw Error(scope.module.location, "module port lacks direction declaration: " + name);
    }
    for (const auto& [name, symbol] : scope.symbols)
      if (symbol.direction != Direction::none && !ports.count(name))
        throw Error(scope.module.location, "declared port is absent from module header: " + std::string(name));
  }
  void implicit(ExprId id, Scope& scope)
  {
    if (id == noExpr)
      return;
    const auto& e = expr(id);
    if (e.kind == ExpressionKind::reference && !scope.constants.count(e.text) && !scope.symbols.count(e.text)) {
      if (!scope.module.implicit_nets)
        throw Error(e.location, "undeclared net with default_nettype none: " + e.text);
      auto signal = makeSignal(scope, e.text, NetType::wire, {}, false, e.location);
      scope.symbols.emplace(e.text, Symbol{std::move(signal), {}, Direction::none, NetType::wire, true});
    } else if (e.kind == ExpressionKind::concatenate)
      for (auto part : e.operands)
        implicit(part, scope);
    // A select cannot infer a vector declaration; only simple net identifiers imply scalar nets.
  }
  Signal signal(ExprId id, Scope& scope, bool lvalue = false, uint32_t context_width = 0)
  {
    const auto& e = expr(id);
    if (e.kind == ExpressionKind::reference) {
      const auto found = scope.symbols.find(e.text);
      if (found != scope.symbols.end())
        return found->second.signal;
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
      const auto found = scope.symbols.find(base.text);
      if (found != scope.symbols.end()) {
        if (!found->second.range)
          throw Error(e.location, "cannot select a bit from a scalar net: " + base.text);
        const auto range = *found->second.range;
        Signal result;
        for (auto index : _constants.selectIndices(e, scope.constants, range)) {
          const auto offset = range.first >= range.second ? int64_t(range.first) - index : index - range.first;
          if (offset < 0 || offset >= int64_t(found->second.signal.bits.size())) {
            if (lvalue)
              throw Error(e.location, "out-of-range net lvalue is not supported");
            result.bits.push_back(xBit);
          } else
            result.bits.push_back(found->second.signal.bits[offset]);
        }
        return result;
      }
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
  void assign(const Signal& left, Signal right, SourceLocation location)
  {
    right = resized(std::move(right), left.bits.size());
    for (size_t i = 0; i < left.bits.size(); ++i) {
      if (isConstant(left.bits[i]))
        throw Error(location, "expected net lvalue");
      _flat.assignments.push_back({left.bits[i], right.bits[i], location});
    }
  }
  void instance(const Instance& instance, Scope& scope)
  {
    const auto type = _modules.find(instance.type);
    if (type == _modules.end()) {
      if (!instance.parameters.empty())
        throw Error(instance.location, "cannot apply parameter overrides to unresolved cell: " + instance.type);
      FlatInstance cell{instance.location, instance.type, scope.prefix + instance.name, {}};
      for (const auto& port : instance.ports) {
        const auto shape = _resolve_port ? _resolve_port(instance.type, port.name) : std::nullopt;
        if (shape && (shape->width == 0 || shape->width > BitVector::kMaxWidth))
          throw Error(port.location, "invalid external port width");
        const bool output = shape && (shape->direction == Direction::output || shape->direction == Direction::inout);
        const auto width = shape && shape->direction == Direction::input ? shape->width : 0;
        cell.ports.push_back(
            {port.location, port.name, port.expression == noExpr ? Signal{} : signal(port.expression, scope, output, width)});
      }
      if (!_instance_names.insert(cell.name).second)
        throw Error(instance.location, "flattened instance name collision: " + cell.name);
      _flat.instances.push_back(std::move(cell));
      return;
    }
    const auto& child_module = *type->second;
    std::vector<const Parameter*> parameters;
    std::unordered_map<std::string_view, const Parameter*> parameter_names;
    for (const auto& parameter : child_module.parameters)
      if (!parameter.local) {
        parameters.push_back(&parameter);
        parameter_names.emplace(parameter.name, &parameter);
      }
    std::unordered_map<std::string_view, ExprId> overrides;
    for (size_t i = 0; i < instance.parameters.size(); ++i) {
      const auto& actual = instance.parameters[i];
      const Parameter* parameter = nullptr;
      if (actual.name.empty()) {
        if (i < parameters.size())
          parameter = parameters[i];
      } else {
        auto it = parameter_names.find(actual.name);
        if (it != parameter_names.end())
          parameter = it->second;
      }
      if (!parameter)
        throw Error(actual.location, "unknown or excess parameter override in " + instance.name);
      overrides.emplace(parameter->name, actual.expression);
    }
    if (_active.size() >= 256 || _active.count(&child_module))
      throw Error(instance.location, "recursive or excessively deep module hierarchy at " + instance.name);
    Scope child{child_module, scope.prefix + instance.name + "/", {}, {}};
    prepare(child, overrides, &scope.constants);
    std::unordered_set<std::string_view> formal_names(child_module.ports.begin(), child_module.ports.end());
    for (size_t i = 0; i < instance.ports.size(); ++i) {
      const auto& actual = instance.ports[i];
      std::string_view name = actual.name;
      if (name.empty()) {
        if (i >= child_module.ports.size())
          throw Error(actual.location, "too many positional port connections");
        name = child_module.ports[i];
      }
      if (!formal_names.count(name))
        throw Error(actual.location, "unknown module port: " + std::string(name));
      if (actual.expression == noExpr)
        continue;
      const auto& formal = child.symbols.at(name);
      auto value = signal(actual.expression, scope, formal.direction != Direction::input,
                          formal.direction == Direction::input ? formal.signal.bits.size() : 0);
      if (formal.direction == Direction::input)
        assign(formal.signal, std::move(value), actual.location);
      else if (formal.direction == Direction::output)
        assign(value, formal.signal, actual.location);
      else {
        if (value.bits.size() != formal.signal.bits.size())
          throw Error(actual.location, "unequal-width inout connections are not supported");
        for (size_t bit = 0; bit < value.bits.size(); ++bit)
          _flat.inouts.push_back({formal.signal.bits[bit], value.bits[bit], actual.location});
      }
    }
    expand(child);
  }
  void expand(Scope& scope)
  {
    _active.insert(&scope.module);
    std::unordered_set<std::string_view> instances;
    for (const auto& statement : scope.module.statements) {
      if (const auto* cell = std::get_if<Instance>(&statement)) {
        if (scope.symbols.count(cell->name) || scope.constants.count(cell->name) || !instances.insert(cell->name).second)
          throw Error(cell->location, "duplicate instance or conflicting module item name: " + cell->name);
        for (const auto& port : cell->ports)
          implicit(port.expression, scope);
      } else
        implicit(std::get<Assignment>(statement).left, scope);
    }
    for (auto name : instances)
      if (scope.symbols.count(name))
        throw Error(scope.module.location, "instance conflicts with implicit net: " + std::string(name));
    for (const auto& statement : scope.module.statements) {
      if (const auto* cell = std::get_if<Instance>(&statement))
        instance(*cell, scope);
      else {
        const auto& assignment = std::get<Assignment>(statement);
        const auto left = signal(assignment.left, scope, true);
        assign(left, signal(assignment.right, scope, false, left.bits.size()), assignment.location);
      }
    }
    _active.erase(&scope.module);
  }
  const Design& _ast;
  const PortResolver& _resolve_port;
  FlatDesign _flat;
  std::unordered_map<std::string_view, const Module*> _modules;
  std::unordered_set<const Module*> _active;
  std::unordered_set<std::string> _net_names;
  std::unordered_set<std::string> _instance_names;
  detail::ConstEvaluator _constants;
};
}  // namespace
ElaborateResult elaborate(const Design& design, std::string_view top, const PortResolver& resolve_port)
{
  ElaborateResult result;
  try {
    result.design = std::make_unique<FlatDesign>(Elaborator(design, resolve_port).run(top));
  } catch (const Error& error) {
    result.diagnostics.push_back({design.source, error.location, error.what()});
  }
  return result;
}
}  // namespace idb::verilog

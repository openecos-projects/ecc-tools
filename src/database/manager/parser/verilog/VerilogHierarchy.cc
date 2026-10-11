// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include "VerilogHierarchy.hh"

#include <algorithm>
#include <unordered_set>

namespace idb::verilog {
namespace {
using detail::Constant;
using detail::Constants;
using detail::Error;
using detail::integer;
using detail::rangeWidth;
struct OverrideValue
{
  BitVector value;
  SourceLocation location;
  ExprId order;
  Constants context;
};
using Overrides = std::unordered_map<std::string, OverrideValue>;
bool equalOverrides(const Overrides& left, const Overrides& right)
{
  if (left.size() != right.size())
    return false;
  for (const auto& [name, value] : left) {
    const auto found = right.find(name);
    if (found == right.end() || found->second.value.bits() != value.value.bits()
        || found->second.value.isSigned() != value.value.isSigned())
      return false;
    if (value.context.size() != found->second.context.size())
      return false;
    for (const auto& [parameter, constant] : value.context) {
      const auto other = found->second.context.find(parameter);
      if (other == found->second.context.end() || other->second.value.bits() != constant.value.bits()
          || other->second.value.isSigned() != constant.value.isSigned() || other->second.range != constant.range)
        return false;
    }
  }
  return true;
}
std::string indexed(const std::string& name, int64_t index)
{
  return name + "[" + std::to_string(index) + "]";
}
void branchNames(const Generate& generate, std::unordered_set<std::string_view>& names)
{
  for (const auto& branch : generate.branches) {
    if (branch.body->direct_conditional)
      branchNames(*std::get<std::unique_ptr<Generate>>(branch.body->scope.statements.front()), names);
    else if (!branch.body->name.empty())
      names.insert(branch.body->name);
  }
}
class HierarchyBinder
{
 public:
  HierarchyBinder(const SyntaxTree& ast, const LibraryLookup& library, const Overrides& overrides, bool discovery,
                  const std::unordered_set<std::string>& known_scopes, const std::unordered_map<std::string, bool>& known_parameters)
      : _ast(ast),
        _lookup(library),
        _result{ast},
        _constants(ast),
        _overrides(overrides),
        _discovery(discovery),
        _known_scopes(known_scopes),
        _known_parameters(known_parameters)
  {
  }

  Hierarchy run(std::string_view requested)
  {
    for (const auto& module : _ast.modules) {
      if (!_modules.emplace(module.name, &module).second)
        throw Error(module.location, "duplicate module: " + module.name);
      if (blackboxAttribute(module))
        _blackboxes.insert(&module);
    }
    if (requested.empty()) {
      std::unordered_set<std::string_view> instantiated;
      auto collect = [&](auto&& self, const ScopeSyntax& scope) -> void {
        for (const auto& statement : scope.statements) {
          if (const auto* instance = std::get_if<Instance>(&statement))
            instantiated.insert(instance->type);
          else if (const auto* generate = std::get_if<std::unique_ptr<Generate>>(&statement))
            for (const auto& branch : (*generate)->branches)
              self(self, branch.body->scope);
        }
      };
      for (const auto& module : _ast.modules)
        collect(collect, module.scope);
      for (const auto& module : _ast.modules)
        if (!instantiated.count(module.name) && !libraryCell(module)) {
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
    _top_name = requested;
    auto top = std::make_unique<BoundScope>(BoundScope{found->second->scope, found->second});
    prepareScope(*top, {});
    expand(*top);
    _result.top = std::move(top);
    return std::move(_result);
  }
  const Overrides& overrides() const { return _pending; }
  const auto& scopes() const { return _scope_names; }
  const auto& parameters() const { return _parameter_targets; }
  void validateOverrides() const
  {
    for (const auto& [name, value] : _pending) {
      const auto found = _parameter_targets.find(name);
      if (found == _parameter_targets.end() || found->second)
        throw Error(value.location, "defparam target is unknown or not an overridable parameter: " + name);
    }
  }

 private:
  const LibraryCell* library(std::string_view name)
  {
    auto [found, inserted] = _result.library.try_emplace(name, nullptr);
    if (inserted && _lookup)
      found->second = _lookup(name);
    return found->second;
  }
  bool blackboxAttribute(const Module& module)
  {
    bool blackbox = false;
    for (const auto& attribute : module.attributes) {
      if (attribute.name != "blackbox" && attribute.name != "syn_black_box")
        continue;
      bool enabled = true;
      if (attribute.string_value) {
        const auto& value = *attribute.string_value;
        if (value == "\"true\"" || value == "\"1\"")
          enabled = true;
        else if (value == "\"false\"" || value == "\"0\"")
          enabled = false;
        else
          throw Error(attribute.location, "blackbox attribute requires a boolean value");
      } else if (attribute.value != noExpr) {
        const auto value = _constants.requiredConstant(attribute.value, {});
        if (!value.known())
          throw Error(attribute.location, "blackbox attribute requires a known constant");
        enabled = value.truth() == '1';
      }
      blackbox = enabled;
    }
    return blackbox;
  }
  static bool localParameter(const Parameter& parameter, const Module* module)
  {
    return !module || parameter.local || (module->parameter_ports && !parameter.header);
  }
  bool libraryCell(const Module& module)
  {
    return _blackboxes.count(&module)
           || (module.scope.statements.empty() && module.scope.overrides.empty() && module.scope.genvars.empty()
               && std::none_of(module.scope.declarations.begin(), module.scope.declarations.end(),
                               [](const auto& d) { return d.initializer != noExpr; })
               && library(module.name));
  }
  const BoundSymbol* symbol(const BoundScope& scope, std::string_view name) const
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
  void prepareScope(BoundScope& scope, const std::unordered_map<std::string_view, ExprId>& overrides, const Constants* actual = nullptr)
  {
    _scope_names.insert(scope.prefix);
    if (!_discovery) {
      prepareParameters(scope, overrides, actual);
      prepareDeclarations(scope);
      return;
    }
    // Defparam discovery can visit a provisional specialization that is invalid.
    // Only parameter discovery tolerates that; the final hierarchy binds strictly.
    try {
      prepareParameters(scope, overrides, actual);
    } catch (const Error&) {
    }
  }
  void prepareParameters(BoundScope& scope, const std::unordered_map<std::string_view, ExprId>& overrides, const Constants* actual_context)
  {
    std::unordered_set<std::string_view> declared;
    for (const auto& parameter : scope.syntax.parameters) {
      if (!declared.insert(parameter.name).second)
        throw Error(parameter.location, "duplicate parameter: " + parameter.name);
      auto range = _constants.bounds(parameter.range, scope.constants);
      const auto override = overrides.find(parameter.name);
      const bool integer = parameter.data_type == DeclaredDataType::integer || parameter.data_type == DeclaredDataType::intType;
      const bool two_state = parameter.data_type == DeclaredDataType::intType;
      const bool sign = parameter.signing.value_or(integer);
      if (!range && parameter.data_type != DeclaredDataType::implicit)
        range = std::pair<int32_t, int32_t>{integer ? 31 : 0, 0};
      const auto width = range ? rangeWidth(range->first, range->second, parameter.location) : 0;
      const auto target = scope.prefix + parameter.name;
      _parameter_targets.emplace(target, localParameter(parameter, scope.module));
      const auto pending = _pending.find(target);
      const auto prior = _overrides.find(target);
      const OverrideValue* forced = prior != _overrides.end() ? &prior->second : nullptr;
      if (pending != _pending.end() && (!forced || pending->second.order >= forced->order))
        forced = &pending->second;
      auto value = forced                        ? _constants.requiredConstant(forced->order, forced->context, width)
                   : override != overrides.end() ? _constants.requiredConstant(override->second, *actual_context, width)
                                                 : _constants.requiredConstant(parameter.value, scope.constants, width);
      if (range)
        value = value.resized(rangeWidth(range->first, range->second, parameter.location));
      if (range || parameter.signing.has_value())
        value = BitVector(std::string(value.bits()), sign);
      auto bits = std::string(value.bits());
      if (two_state)
        for (auto& bit : bits)
          if (bit == 'x' || bit == 'z')
            bit = '0';
      value = BitVector(std::move(bits), value.isSigned());
      scope.constants.insert_or_assign(parameter.name, Constant{std::move(value), range, parameter.location, two_state});
    }
  }
  void prepareDeclarations(BoundScope& scope)
  {
    scope.symbols.reserve(scope.syntax.declarations.size());
    for (const auto& declaration : scope.syntax.declarations) {
      if (std::any_of(scope.syntax.parameters.begin(), scope.syntax.parameters.end(),
                      [&](const auto& p) { return p.name == declaration.name; }))
        throw Error(declaration.location, "net conflicts with parameter: " + declaration.name);
      scope.constants.erase(declaration.name);
      ObjectType type{_constants.bounds(declaration.range, scope.constants), declaration.is_signed, false,
                      declaration.object_kind == DeclaredObjectKind::variable
                          || (declaration.data_type == DeclaredDataType::logic && declaration.object_kind == DeclaredObjectKind::implicit
                              && (declaration.direction == Direction::none || declaration.direction == Direction::output)),
                      declaration.type};
      const auto found = scope.symbols.find(declaration.name);
      if (found != scope.symbols.end()) {
        auto& symbol = found->second;
        if ((scope.module && scope.module->ansi_ports) || (declaration.direction == Direction::none && symbol.complete)
            || (declaration.direction != Direction::none && declaration.complete() && symbol.complete)
            || (declaration.direction != Direction::none && symbol.direction != Direction::none))
          throw Error(declaration.location, "duplicate declaration: " + declaration.name);
        if (type.range != symbol.type.range)
          throw Error(declaration.location, "inconsistent port/net declaration range: " + declaration.name);
        if (declaration.direction != Direction::none)
          symbol.direction = declaration.direction;
        else {
          symbol.complete = true;
          symbol.type.net_type = type.net_type;
        }
        symbol.type.is_signed |= type.is_signed;
        symbol.type.is_variable |= type.is_variable;
      } else {
        scope.symbols.emplace(declaration.name,
                              BoundSymbol{type, declaration.direction, declaration.direction == Direction::none || declaration.complete(),
                                          declaration.location});
        scope.declaration_order.push_back(declaration.name);
      }
      if (declaration.initializer != noExpr && (declaration.direction != Direction::none || type.is_variable))
        throw Error(declaration.location, "port or variable declaration initializer requires procedural initialization");
    }
    std::unordered_set<std::string_view> ports;
    if (scope.module) {
      ports.insert(scope.module->ports.begin(), scope.module->ports.end());
      for (const auto& name : scope.module->ports) {
        const auto found = scope.symbols.find(name);
        if (found == scope.symbols.end() || found->second.direction == Direction::none)
          throw Error(scope.syntax.location, "module port lacks direction declaration: " + name);
      }
    }
    for (const auto& [name, symbol] : scope.symbols) {
      if (symbol.direction != Direction::none && !ports.count(name))
        throw Error(scope.syntax.location, "declared port is absent from module header: " + std::string(name));
      if (symbol.type.is_variable && symbol.direction == Direction::inout)
        throw Error(symbol.location, "inout ports must be nets: " + std::string(name));
    }
  }
  void implicit(ExprId id, BoundScope& scope)
  {
    if (id == noExpr)
      return;
    const auto& e = expr(id);
    if (e.kind == ExpressionKind::reference && !scope.constants.count(e.text) && !symbol(scope, e.text)) {
      if (!scope.syntax.implicit_nets)
        throw Error(e.location, "undeclared net with default_nettype none: " + e.text);
      scope.symbols.emplace(e.text, BoundSymbol{{}, Direction::none, true, e.location});
      scope.declaration_order.push_back(e.text);
    } else if (e.kind == ExpressionKind::concatenate)
      for (auto part : e.operands)
        implicit(part, scope);
  }
  void instance(const Instance& instance, const Statement& statement, BoundScope& scope)
  {
    const auto bounds = _constants.bounds(instance.range, scope.constants);
    const size_t count = bounds ? rangeWidth(bounds->first, bounds->second, instance.location) : 1;
    const auto found = _modules.find(instance.type);
    library(instance.type);
    if (found == _modules.end() && !bounds) {
      if (_discovery)
        _scope_names.insert(scope.prefix + instance.name + "/");
      return;
    }
    for (size_t offset = 0; offset < count; ++offset) {
      ScopeExpansion expansion;
      expansion.name
          = bounds ? indexed(instance.name, int64_t(bounds->first) + (bounds->first <= bounds->second ? int64_t(offset) : -int64_t(offset)))
                   : instance.name;
      expansion.array_count = bounds ? count : 0;
      expansion.offset = offset;
      if (found == _modules.end()) {
        _scope_names.insert(scope.prefix + expansion.name + "/");
      } else {
        const auto& module = *found->second;
        if (_active.size() >= 256 || _active.count(&module.scope))
          throw Error(instance.location, "recursive or excessively deep module hierarchy at " + expansion.name);
        std::vector<const Parameter*> parameters;
        std::unordered_map<std::string_view, const Parameter*> parameter_names;
        for (const auto& parameter : module.scope.parameters)
          if (!localParameter(parameter, &module)) {
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
          } else if (const auto it = parameter_names.find(actual.name); it != parameter_names.end())
            parameter = it->second;
          if (!parameter)
            throw Error(actual.location, "unknown or excess parameter override in " + instance.name);
          overrides.emplace(parameter->name, actual.expression);
        }
        auto child = std::make_unique<BoundScope>(BoundScope{module.scope, &module, scope.prefix + expansion.name + "/"});
        child->restricted_prefix = bounds ? child->prefix : scope.restricted_prefix;
        const bool leaf = libraryCell(module);
        // Bind parameters before computing an interface specialization's cache key.
        _scope_names.insert(child->prefix);
        if (leaf && !_discovery) {
          prepareParameters(*child, overrides, &scope.constants);
          std::string key;
          for (const auto& parameter : module.scope.parameters) {
            const auto& constant = child->constants.at(parameter.name);
            key += ':';
            key += constant.value.isSigned() ? 's' : 'u';
            key += constant.value.bits();
            if (constant.range)
              key += '[' + std::to_string(constant.range->first) + ':' + std::to_string(constant.range->second) + ']';
          }
          auto& specializations = _interfaces[&module];
          auto it = specializations.find(key);
          if (it == specializations.end()) {
            prepareDeclarations(*child);
            it = specializations
                     .emplace(std::move(key), std::make_shared<ModuleInterface>(ModuleInterface{module, std::move(child->symbols)}))
                     .first;
          }
          expansion.interface = it->second;
        } else {
          prepareScope(*child, overrides, &scope.constants);
          if (!leaf)
            expand(*child);
          expansion.scope = std::move(child);
        }
      }
      if (!_discovery)
        scope.expansions[&statement].push_back(std::move(expansion));
    }
  }
  void parameterOverrides(BoundScope& scope)
  {
    for (const auto& override : scope.syntax.overrides) {
      try {
        std::string relative;
        std::string first;
        const bool absolute = override.path.front().name == _top_name && override.path.size() > 1;
        for (size_t i = absolute ? 1 : 0; i < override.path.size(); ++i) {
          const auto& part = override.path[i];
          const auto name = part.index == noExpr
                                ? part.name
                                : indexed(part.name, integer(_constants.requiredConstant(part.index, scope.constants), override.location));
          if (first.empty())
            first = name;
          if (!relative.empty())
            relative += '/';
          relative += name;
        }
        std::string prefix = absolute ? "" : scope.prefix;
        if (!absolute) {
          for (auto candidate = prefix;;) {
            const auto base = candidate + first;
            const bool found = override.path.size() == 1 ? _parameter_targets.count(base) || _known_parameters.count(base)
                                                         : _scope_names.count(base + "/") || _known_scopes.count(base + "/");
            if (found) {
              prefix = candidate;
              break;
            }
            if (candidate.empty())
              break;
            candidate.pop_back();
            const auto slash = candidate.rfind('/');
            candidate = slash == candidate.npos ? "" : candidate.substr(0, slash + 1);
          }
        }
        const auto target = prefix + relative;
        if (!scope.restricted_prefix.empty() && target.compare(0, scope.restricted_prefix.size(), scope.restricted_prefix) != 0)
          throw Error(override.location, "defparam cannot escape a generate or instance-array hierarchy");
        if (override.path.back().index != noExpr)
          throw Error(override.location, "defparam target must be a parameter name");
        OverrideValue value{_constants.requiredConstant(override.value, scope.constants), override.location, override.value,
                            scope.constants};
        auto found = _pending.find(target);
        if (found == _pending.end() || found->second.order <= value.order)
          _pending.insert_or_assign(target, std::move(value));
      } catch (const Error&) {
        if (!_discovery)
          throw;
      }
    }
  }
  std::string blockName(const Generate& generate, const GenerateBlock& block, const BoundScope& parent, unsigned number)
  {
    if (!block.name.empty())
      return block.name;
    std::string digits = std::to_string(number ? number : generate.number);
    auto conflict = [&](const std::string& name) {
      for (const auto& declaration : parent.syntax.declarations)
        if (declaration.name == name)
          return true;
      for (const auto& parameter : parent.syntax.parameters)
        if (parameter.name == name)
          return true;
      if (std::find(parent.syntax.genvars.begin(), parent.syntax.genvars.end(), name) != parent.syntax.genvars.end())
        return true;
      for (const auto& statement : parent.syntax.statements) {
        if (const auto* cell = std::get_if<Instance>(&statement)) {
          if (cell->name == name)
            return true;
        } else if (const auto* other = std::get_if<std::unique_ptr<Generate>>(&statement)) {
          std::unordered_set<std::string_view> names;
          branchNames(**other, names);
          if (names.count(name))
            return true;
        }
      }
      return false;
    };
    while (conflict("genblk" + digits))
      digits = "0" + digits;
    return "genblk" + digits;
  }
  void generateBlock(const Generate& generate, const GenerateBlock& block, BoundScope& parent, std::vector<ScopeExpansion>& expansions,
                     std::optional<int64_t> index = {}, unsigned number = 0)
  {
    if (block.direct_conditional) {
      const auto& nested = *std::get<std::unique_ptr<Generate>>(block.scope.statements.front());
      this->generate(nested, parent, expansions, number ? number : generate.number);
      return;
    }
    const auto name = blockName(generate, block, parent, number);
    ScopeExpansion expansion;
    expansion.name = index ? indexed(name, *index) : name;
    auto child = std::make_unique<BoundScope>(BoundScope{block.scope, nullptr, parent.prefix + expansion.name + "/", parent.constants});
    child->parent = &parent;
    child->restricted_prefix = child->prefix;
    if (index)
      child->constants.insert_or_assign(generate.variable, Constant{BitVector::fromInteger(*index), {}});
    prepareScope(*child, {});
    expand(*child);
    expansion.scope = std::move(child);
    if (!_discovery)
      expansions.push_back(std::move(expansion));
  }
  void generate(const Generate& generate, BoundScope& scope, std::vector<ScopeExpansion>& expansions, unsigned number = 0)
  {
    if (generate.kind == GenerateKind::block) {
      generateBlock(generate, *generate.branches[0].body, scope, expansions, {}, number);
    } else if (generate.kind == GenerateKind::conditional) {
      const auto condition = _constants.requiredConstant(generate.condition, scope.constants).truth();
      const size_t branch = condition == '1' ? 0 : 1;
      if (branch < generate.branches.size())
        generateBlock(generate, *generate.branches[branch].body, scope, expansions, {}, number);
    } else if (generate.kind == GenerateKind::selection) {
      const auto condition = _constants.requiredConstant(generate.condition, scope.constants);
      size_t width = condition.width();
      bool sign = condition.isSigned();
      for (const auto& branch : generate.branches)
        for (auto match : branch.matches) {
          const auto value = _constants.requiredConstant(match, scope.constants);
          width = std::max(width, size_t(value.width()));
          sign &= value.isSigned();
        }
      const GenerateBlock* selected = nullptr;
      const GenerateBlock* fallback = nullptr;
      for (const auto& branch : generate.branches) {
        if (branch.matches.empty())
          fallback = branch.body.get();
        for (auto match : branch.matches)
          if (!selected
              && _constants.requiredConstant(generate.condition, scope.constants, width).resized(width, sign).bits()
                     == _constants.requiredConstant(match, scope.constants, width).resized(width, sign).bits())
            selected = branch.body.get();
      }
      if (!selected)
        selected = fallback;
      if (selected)
        generateBlock(generate, *selected, scope, expansions, {}, number);
    } else {
      bool declared = false;
      for (const BoundScope* current = &scope; current; current = current->parent) {
        if (std::find(current->syntax.genvars.begin(), current->syntax.genvars.end(), generate.variable) != current->syntax.genvars.end()) {
          declared = true;
          break;
        }
        if (current->symbols.count(generate.variable) || current->constants.count(generate.variable))
          break;
      }
      if (!declared)
        throw Error(generate.location, "generate loop requires an unshadowed genvar: " + generate.variable);
      auto values = scope.constants;
      values.erase(generate.variable);
      auto value = _constants.requiredConstant(generate.initial, values).resized(32);
      std::unordered_set<int64_t> seen;
      for (;;) {
        const auto index = integer(BitVector(std::string(value.bits()), true), generate.location);
        values.insert_or_assign(generate.variable, Constant{BitVector::fromInteger(index), {}});
        const auto condition = _constants.requiredConstant(generate.condition, values).truth();
        if (condition == 'x')
          throw Error(generate.location, "unknown genvar loop condition");
        if (condition == '0')
          break;
        if (!seen.insert(index).second || seen.size() > BitVector::kMaxWidth)
          throw Error(generate.location, "repeated genvar value or excessive generate iterations");
        generateBlock(generate, *generate.branches[0].body, scope, expansions, index, number);
        value = _constants.requiredConstant(generate.step, values).resized(32);
      }
    }
  }
  void expand(BoundScope& scope)
  {
    if (_active.size() >= 256)
      throw Error(scope.syntax.location, "excessively deep generate hierarchy");
    _active.insert(&scope.syntax);
    struct ActiveScope
    {
      std::unordered_set<const ScopeSyntax*>& active;
      const ScopeSyntax* module;
      ~ActiveScope() { active.erase(module); }
    } guard{_active, &scope.syntax};
    parameterOverrides(scope);
    if (_discovery) {
      for (const auto& statement : scope.syntax.statements) {
        try {
          if (const auto* cell = std::get_if<Instance>(&statement))
            instance(*cell, statement, scope);
          else if (const auto* node = std::get_if<std::unique_ptr<Generate>>(&statement)) {
            std::vector<ScopeExpansion> provisional;
            generate(**node, scope, provisional);
          }
        } catch (const Error&) { /* Final elaboration validates after parameter discovery converges. */
        }
      }
      return;
    }
    std::unordered_set<std::string_view> names;
    auto declare = [&](std::string_view name, SourceLocation location) {
      if (scope.symbols.count(name)
          || std::any_of(scope.syntax.parameters.begin(), scope.syntax.parameters.end(), [&](const auto& p) { return p.name == name; })
          || !names.insert(name).second)
        throw Error(location, "duplicate instance or conflicting module item name: " + std::string(name));
    };
    for (const auto& name : scope.syntax.genvars)
      declare(name, scope.syntax.location);
    for (const auto& statement : scope.syntax.statements) {
      if (const auto* cell = std::get_if<Instance>(&statement)) {
        declare(cell->name, cell->location);
        for (const auto& port : cell->ports)
          if (port.kind == ConnectionKind::expression)
            implicit(port.expression, scope);
      } else if (const auto* assignment = std::get_if<Assignment>(&statement))
        implicit(assignment->left, scope);
      else {
        const auto& node = *std::get<std::unique_ptr<Generate>>(statement);
        std::unordered_set<std::string_view> branch_names;
        branchNames(node, branch_names);
        for (auto name : branch_names)
          declare(name, node.location);
      }
    }
    for (auto name : names)
      if (scope.symbols.count(name))
        throw Error(scope.syntax.location, "instance conflicts with implicit net: " + std::string(name));
    for (const auto& statement : scope.syntax.statements) {
      if (const auto* cell = std::get_if<Instance>(&statement))
        instance(*cell, statement, scope);
      else if (const auto* node = std::get_if<std::unique_ptr<Generate>>(&statement))
        generate(**node, scope, scope.expansions[&statement]);
    }
  }
  const SyntaxTree& _ast;
  const LibraryLookup& _lookup;
  Hierarchy _result;
  std::string _top_name;
  std::unordered_map<std::string_view, const Module*> _modules;
  std::unordered_set<const ScopeSyntax*> _active;
  std::unordered_set<const Module*> _blackboxes;
  std::unordered_map<const Module*, std::unordered_map<std::string, std::shared_ptr<const ModuleInterface>>> _interfaces;
  detail::ExpressionEvaluator _constants;
  const Overrides& _overrides;
  Overrides _pending;
  std::unordered_map<std::string, bool> _parameter_targets;
  bool _discovery;
  const std::unordered_set<std::string>& _known_scopes;
  const std::unordered_map<std::string, bool>& _known_parameters;
  std::unordered_set<std::string> _scope_names;
};
}  // namespace
HierarchyResult bindHierarchy(const SyntaxTree& design, std::string_view top, const LibraryLookup& library)
{
  HierarchyResult result;
  try {
    Overrides overrides;
    std::unordered_set<std::string> scopes;
    std::unordered_map<std::string, bool> parameters;
    auto hasOverrides = [&](auto&& self, const ScopeSyntax& module) -> bool {
      if (!module.overrides.empty())
        return true;
      for (const auto& statement : module.statements)
        if (const auto* node = std::get_if<std::unique_ptr<Generate>>(&statement))
          for (const auto& branch : (*node)->branches)
            if (self(self, branch.body->scope))
              return true;
      return false;
    };
    const bool discover = std::any_of(design.modules.begin(), design.modules.end(),
                                      [&](const auto& module) { return hasOverrides(hasOverrides, module.scope); });
    if (discover) {
      for (unsigned pass = 0;; ++pass) {
        if (pass == 64)
          throw Error({}, "defparam elaboration did not converge within 64 passes");
        HierarchyBinder discovery(design, library, overrides, true, scopes, parameters);
        discovery.run(top);
        const bool stable
            = equalOverrides(overrides, discovery.overrides()) && scopes == discovery.scopes() && parameters == discovery.parameters();
        overrides = discovery.overrides();
        scopes = discovery.scopes();
        parameters = discovery.parameters();
        if (stable)
          break;
      }
    }
    HierarchyBinder elaborator(design, library, overrides, false, scopes, parameters);
    result.design = std::make_unique<Hierarchy>(elaborator.run(top));
    if (!equalOverrides(overrides, elaborator.overrides()))
      throw Error({}, "defparam changed after parameter resolution");
    elaborator.validateOverrides();
  } catch (const Error& error) {
    result.design.reset();
    result.diagnostics.push_back(
        {error.location.file < design.source_files.size() ? design.source_files[error.location.file] : design.source, error.location,
         error.what()});
  }
  return result;
}
}  // namespace idb::verilog

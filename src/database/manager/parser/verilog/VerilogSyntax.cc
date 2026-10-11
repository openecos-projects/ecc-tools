// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include "VerilogSyntax.hh"

#include <algorithm>
#include <cstring>

#include "VerilogParseContext.hh"
#include "VerilogParser.hh"
#include "VerilogValue.hh"

namespace idb::verilog {
namespace {
ParseResult parseInput(detail::SourceText input, const std::string& source, const SourceOptions& options)
{
  if (!input.diagnostics.empty())
    return {nullptr, std::move(input.diagnostics)};
  ParseResult result;
  auto design = std::make_unique<SyntaxTree>();
  design->source = source;
  design->source_files = std::move(input.files);
  try {
    detail::ParseContext context(*design, input.text, input.lines, options.language);
    grammar::Parser parser(context);
    if (parser.parse() != 0)
      throw detail::Error({}, "Verilog syntax parsing failed");
    context.finish();
    result.design = std::move(design);
  } catch (const detail::Error& error) {
    const auto& file = error.location.file < design->source_files.size() ? design->source_files[error.location.file] : source;
    result.diagnostics.push_back({file, error.location, error.what()});
  }
  return result;
}
}  // namespace
ParseResult parse(std::string_view text, std::string source, const SourceOptions& options)
{
  return parseInput(detail::preprocess(text, source, options), source, options);
}
ParseResult readFiles(const std::vector<std::string>& paths, const SourceOptions& options)
{
  if (paths.empty())
    return {nullptr, {{"<input>", {}, "empty Verilog source file list"}}};
  if (paths.size() == 1)
    return readFile(paths.front(), options);
  return parseInput(detail::preprocessFiles(paths, options), paths.front(), options);
}
ParseResult readFile(const std::string& path, const SourceOptions& options)
{
  try {
    return parse(detail::readSource(path), path, options);
  } catch (const std::exception& error) {
    return {nullptr, {{path, {}, error.what()}}};
  }
}
}  // namespace idb::verilog

namespace idb::verilog::detail {
ParseContext::ParseContext(SyntaxTree& tree, std::string_view text, const std::vector<SourceLocation>& lines, LanguageMode language)
    : _tree(tree), _text(text), _lines(lines), _language(language)
{
  _scanner = createScanner(*this);
}
ParseContext::~ParseContext()
{
  destroyScanner(_scanner);
}
void ParseContext::requireSv(SourceLocation location) const
{
  if (!sv())
    throw Error(location, "SystemVerilog syntax requires SystemVerilog language mode");
}
size_t ParseContext::read(char* destination, size_t capacity)
{
  const auto count = std::min(capacity, _text.size() - _read_offset);
  std::memcpy(destination, _text.data() + _read_offset, count);
  _read_offset += count;
  return count;
}
void ParseContext::match(std::string_view text)
{
  _match_start = _cursor;
  const char* start = text.data();
  const char* end = start + text.size();
  while (const auto* newline = static_cast<const char*>(std::memchr(start, '\n', end - start))) {
    ++_cursor.line;
    _cursor.column = 1;
    start = newline + 1;
  }
  _cursor.column += static_cast<uint32_t>(end - start);
}
void ParseContext::rewindMatch()
{
  _cursor = _match_start;
}
SourceLocation ParseContext::mapped(SourceLocation location) const
{
  if (!_lines.empty()) {
    const auto origin = _lines[std::min(size_t(location.line - 1), _lines.size() - 1)];
    location.line = origin.line;
    location.file = origin.file;
    location.column += origin.column - 1;
  }
  return location;
}
SourceLocation ParseContext::location() const
{
  return mapped(_match_start);
}
SourceLocation ParseContext::token(SourceLocation start)
{
  if (_order == UINT32_MAX)
    throw Error(start, "token count exceeds frontend limit");
  start.order = ++_order;
  _last_token = start;
  return start;
}
void ParseContext::startLiteral(std::string_view text)
{
  _literal.assign(text);
  _literal_location = location();
  _literal_end = _cursor;
}
bool ParseContext::adjacentLiteral() const
{
  return _match_start.line == _literal_end.line && _match_start.column == _literal_end.column;
}
std::string ParseContext::finishNumber()
{
  try {
    (void) BitVector::parse(_literal);
  } catch (const std::exception& error) {
    throw Error(_literal_location, error.what());
  }
  return std::move(_literal);
}
void ParseContext::beginModule(std::string name, std::vector<Attribute> attributes, SourceLocation location)
{
  if (!_module_names.insert(name).second)
    throw Error(location, "duplicate module: " + name);
  _module = std::make_unique<Module>();
  _module->location = location;
  _module->scope.location = location;
  _module->scope.implicit_nets = _implicit;
  _module->name = std::move(name);
  _module->attributes = std::move(attributes);
  _declaration = {};
}
void ParseContext::endModule()
{
  std::unordered_set<std::string_view> names;
  for (const auto& port : _module->ports)
    if (!names.insert(port).second)
      throw Error(_module->location, "duplicate module port: " + port);
  _tree.modules.push_back(std::move(*_module));
  _module.reset();
}
void ParseContext::finish()
{
  if (_tree.modules.empty())
    throw Error({}, "input contains no modules");
}
ScopeSyntax& ParseContext::scope()
{
  return _blocks.empty() ? _module->scope : _blocks.back()->scope;
}
void ParseContext::beginBlock(std::string name, SourceLocation location)
{
  auto block = std::make_unique<GenerateBlock>();
  block->name = std::move(name);
  block->scope.location = location;
  block->scope.implicit_nets = scope().implicit_nets;
  _blocks.push_back(std::move(block));
}
std::unique_ptr<GenerateBlock> ParseContext::endBlock(bool direct_conditional)
{
  auto block = std::move(_blocks.back());
  _blocks.pop_back();
  block->direct_conditional = direct_conditional;
  return block;
}
std::unique_ptr<Generate> ParseContext::beginGenerate(GenerateKind kind, SourceLocation location)
{
  if (++_generate_depth > 128)
    throw Error(location, "generate nesting exceeds limit");
  auto node = std::make_unique<Generate>();
  node->kind = kind;
  node->location = location;
  node->number = ++scope().generate_count;
  return node;
}
void ParseContext::emitGenerate(std::unique_ptr<Generate> node)
{
  scope().statements.emplace_back(std::move(node));
  --_generate_depth;
}
void ParseContext::beginRegion(SourceLocation location)
{
  if (_generate_region || _generate_depth)
    throw Error(location, "nested generate regions are not allowed");
  _generate_region = true;
}
void ParseContext::beginParameterHeader()
{
  _module->parameter_ports = true;
  _parameter_header = true;
}
void ParseContext::parameterSpec(Parameter spec)
{
  if (spec.local && _parameter_header && !sv())
    throw Error(spec.location, "localparam is not allowed in a Verilog-2005 parameter port list");
  _parameter = std::move(spec);
  _parameter.header = _parameter_header;
}
void ParseContext::parameter(std::string name, ExprId value, SourceLocation location)
{
  auto parameter = _parameter;
  parameter.name = std::move(name);
  parameter.location = location;
  parameter.value = value;
  scope().parameters.push_back(std::move(parameter));
}
void ParseContext::declaration(std::string name, ExprId initializer, SourceLocation location)
{
  auto declaration = _declaration;
  declaration.name = std::move(name);
  declaration.location = location;
  declaration.initializer = initializer;
  scope().declarations.push_back(std::move(declaration));
}
void ParseContext::portSpec(Declaration spec, SourceLocation location)
{
  if (spec.direction == Direction::none) {
    requireSv(location);
    spec.direction = _declaration.direction == Direction::none ? Direction::inout : _declaration.direction;
  }
  _declaration = std::move(spec);
  _module->ansi_ports = true;
}
void ParseContext::port(std::string name, SourceLocation location, bool typed)
{
  if (typed || _module->ansi_ports)
    declaration(name, noExpr, location);
  _module->ports.push_back(std::move(name));
}
void ParseContext::validateConnections(const std::vector<Connection>& connections, bool parameters) const
{
  std::unordered_set<std::string_view> names;
  bool wildcard = false;
  for (const auto& connection : connections) {
    if (parameters && (connection.expression == noExpr || connection.kind != ConnectionKind::expression))
      throw Error(connection.location, "empty parameter override is not supported");
    if (connection.kind != ConnectionKind::expression)
      requireSv(connection.location);
    if (connection.kind == ConnectionKind::wildcard) {
      if (wildcard)
        throw Error(connection.location, "duplicate wildcard connection");
      wildcard = true;
    } else if (!connection.name.empty() && !names.insert(connection.name).second)
      throw Error(connection.location, "duplicate named connection: " + connection.name);
  }
}
void ParseContext::instanceType(std::string name, std::vector<Connection> parameters)
{
  validateConnections(parameters, true);
  _instance_type = std::move(name);
  _instance_parameters = std::move(parameters);
}
void ParseContext::instance(std::string name, Range range, std::vector<Connection> ports, SourceLocation location)
{
  validateConnections(ports, false);
  scope().statements.emplace_back(Instance{location, _instance_type, std::move(name), _instance_parameters, std::move(ports), range});
}
ExprId ParseContext::add(ExpressionKind kind, SourceLocation location, std::string text, std::vector<ExprId> operands)
{
  if (_tree.expressions.size() == noExpr)
    throw Error(location, "expression count exceeds frontend limit");
  unsigned depth = 1;
  for (auto child : operands)
    depth = std::max(depth, unsigned(_expression_depth.at(child)) + 1);
  if (depth > 256)
    throw Error(location, "expression AST depth exceeds frontend limit (256)");
  _expression_depth.push_back(static_cast<uint16_t>(depth));
  const auto id = static_cast<ExprId>(_tree.expressions.size());
  _tree.expressions.push_back({kind, location, std::move(text), std::move(operands)});
  return id;
}
void ParseContext::openExpression(SourceLocation location)
{
  if (++_expression_nesting > 256)
    throw Error(location, "expression nesting depth exceeds frontend limit (256)");
}
}  // namespace idb::verilog::detail

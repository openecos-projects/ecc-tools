// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include "VerilogParser.hh"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <optional>
#include <unordered_set>

#include "VerilogIdentifier.hh"
#include "VerilogLexer.hh"

namespace idb::verilog {
namespace {
using detail::Error;
using detail::Lexer;
using detail::Token;
using detail::TokenKind;
int precedence(std::string_view op)
{
  if (op == "||")
    return 1;
  if (op == "&&")
    return 2;
  if (op == "|")
    return 3;
  if (op == "^" || op == "^~" || op == "~^")
    return 4;
  if (op == "&")
    return 5;
  if (op == "==" || op == "!=" || op == "===" || op == "!==")
    return 6;
  if (op == "<" || op == "<=" || op == ">" || op == ">=")
    return 7;
  if (op == "<<" || op == ">>" || op == "<<<" || op == ">>>")
    return 8;
  if (op == "+" || op == "-")
    return 9;
  if (op == "*" || op == "/" || op == "%")
    return 10;
  if (op == "**")
    return 11;
  return 0;
}
class Parser
{
 public:
  Parser(Design& design, std::string_view source) : _design(design), _lexer(source) { next(); }
  void run()
  {
    std::unordered_set<std::string> names;
    while (_token.kind != TokenKind::end) {
      if (_token.kind == TokenKind::directive) {
        directive();
        continue;
      }
      attributes();
      const auto start = _token.location;
      expect("module");
      Module module;
      module.location = start;
      module.name = name();
      module.implicit_nets = _implicit;
      if (!names.insert(module.name).second)
        throw Error(start, "duplicate module: " + module.name);
      if (take("#")) {
        module.parameter_ports = true;
        expect("(");
        parameters(module, true);
        expect(")");
      }
      if (take("(")) {
        if (!is(")")) {
          module.ansi_ports = direction() != Direction::none;
          if (module.ansi_ports)
            ansiPorts(module);
          else {
            do {
              module.ports.push_back(name());
            } while (take(","));
          }
        }
        expect(")");
      }
      expect(";");
      while (!is("endmodule")) {
        if (_token.kind == TokenKind::end)
          fail("expected endmodule");
        attributes();
        if (take(";"))
          continue;
        if (is("parameter") || is("localparam")) {
          parameters(module, false);
          expect(";");
        } else if (direction() != Direction::none || netType())
          declaration(module);
        else if (take("assign")) {
          do {
            const auto location = _token.location;
            const auto left = expression();
            expect("=");
            const auto right = expression();
            module.statements.emplace_back(Assignment{location, left, right});
          } while (take(","));
          expect(";");
        } else if (_token.kind == TokenKind::identifier && (_token.escaped || (!isKeyword(_token.text) && _token.text.front() != '$'))) {
          instances(module);
        } else
          fail("unsupported structural Verilog construct: " + std::string(_token.text));
      }
      next();
      std::unordered_set<std::string> ports;
      for (const auto& port : module.ports)
        if (!ports.insert(port).second)
          throw Error(start, "duplicate module port: " + port);
      _design.modules.push_back(std::move(module));
    }
    if (_design.modules.empty())
      fail("input contains no modules");
  }

 private:
  bool is(std::string_view text) const { return !_token.escaped && _token.text == text; }
  void next() { _token = _lexer.next(); }
  bool take(std::string_view text)
  {
    if (!is(text))
      return false;
    next();
    return true;
  }
  [[noreturn]] void fail(std::string message) const { throw Error(_token.location, std::move(message)); }
  void expect(std::string_view text)
  {
    if (!take(text))
      fail("expected '" + std::string(text) + "', found '" + std::string(_token.text) + "'");
  }
  std::string name()
  {
    if (_token.kind != TokenKind::identifier || (!_token.escaped && (isKeyword(_token.text) || _token.text.front() == '$')))
      fail("expected identifier, found '" + std::string(_token.text) + "'");
    std::string result(_token.text);
    next();
    return result;
  }
  ExprId add(ExpressionKind kind, SourceLocation location, std::string text = {}, std::vector<ExprId> operands = {})
  {
    if (_design.expressions.size() == noExpr)
      throw Error(location, "expression count exceeds frontend limit");
    unsigned depth = 1;
    for (const auto child : operands)
      depth = std::max(depth, unsigned(_expression_depth.at(child)) + 1);
    if (depth > 256)
      throw Error(location, "expression AST depth exceeds frontend limit (256)");
    _expression_depth.push_back(static_cast<uint16_t>(depth));
    const auto id = static_cast<ExprId>(_design.expressions.size());
    _design.expressions.push_back({kind, location, std::move(text), std::move(operands)});
    return id;
  }
  ExprId expression(int min = 0)
  {
    if (++_depth > 256)
      fail("expression nesting exceeds frontend limit (256)");
    const auto start = _token.location;
    ExprId left;
    if (is("+") || is("-") || is("!") || is("~") || is("&") || is("~&") || is("|") || is("~|") || is("^") || is("~^") || is("^~")) {
      std::string op(_token.text);
      next();
      left = add(ExpressionKind::unary, start, std::move(op), {expression(12)});
    } else if (take("(")) {
      left = expression();
      expect(")");
    } else if (take("{")) {
      std::vector<ExprId> values{expression()};
      if (take("{")) {
        std::vector<ExprId> repeated{expression()};
        while (take(","))
          repeated.push_back(expression());
        expect("}");
        expect("}");
        left = add(ExpressionKind::repeat, start, {}, {values.front(), add(ExpressionKind::concatenate, start, {}, std::move(repeated))});
      } else {
        while (take(","))
          values.push_back(expression());
        expect("}");
        left = add(ExpressionKind::concatenate, start, {}, std::move(values));
      }
    } else if (_token.kind == TokenKind::number) {
      left = add(ExpressionKind::number, start, std::string(_token.text));
      next();
    } else {
      left = add(ExpressionKind::reference, start, name());
      if (take("[")) {
        const auto index = expression();
        if (take(":")) {
          const auto right = expression();
          left = add(ExpressionKind::slice, start, {}, {left, index, right});
        } else if (is("+:") || is("-:")) {
          const auto kind = is("+:") ? ExpressionKind::indexedUp : ExpressionKind::indexedDown;
          next();
          const auto width = expression();
          left = add(kind, start, {}, {left, index, width});
        } else
          left = add(ExpressionKind::select, start, {}, {left, index});
        expect("]");
      }
    }
    for (;;) {
      const int level = _token.kind == TokenKind::symbol ? precedence(_token.text) : 0;
      if (level == 0 || level < min)
        break;
      std::string op(_token.text);
      next();
      // IEEE 1364-2005 Table 5-4: binary operators, including power, associate left to right.
      const auto right = expression(level + 1);
      left = add(ExpressionKind::binary, start, std::move(op), {left, right});
    }
    if (min == 0 && take("?")) {
      const auto yes = expression();
      expect(":");
      const auto no = expression();
      left = add(ExpressionKind::conditional, start, {}, {left, yes, no});
    }
    --_depth;
    return left;
  }
  Range range()
  {
    if (!take("["))
      return {};
    const auto left = expression();
    expect(":");
    const auto right = expression();
    expect("]");
    return {left, right};
  }
  Direction direction() const
  {
    if (is("input"))
      return Direction::input;
    if (is("output"))
      return Direction::output;
    if (is("inout"))
      return Direction::inout;
    return Direction::none;
  }
  std::optional<NetType> netType() const
  {
    if (is("wire"))
      return NetType::wire;
    if (is("tri"))
      return NetType::tri;
    if (is("wand"))
      return NetType::wand;
    if (is("wor"))
      return NetType::wor;
    if (is("supply0"))
      return NetType::supply0;
    if (is("supply1"))
      return NetType::supply1;
    return std::nullopt;
  }
  Declaration spec()
  {
    Declaration result;
    result.location = _token.location;
    result.direction = direction();
    if (result.direction != Direction::none)
      next();
    if (auto type = netType()) {
      result.type = *type;
      result.explicit_type = true;
      next();
    }
    result.is_signed = take("signed");
    result.range = range();
    return result;
  }
  void ansiPorts(Module& module)
  {
    Declaration current;
    do {
      if (direction() != Direction::none)
        current = spec();
      current.location = _token.location;
      current.name = name();
      module.ports.push_back(current.name);
      module.declarations.push_back(current);
    } while (take(","));
  }
  void declaration(Module& module)
  {
    auto current = spec();
    if (module.ansi_ports && current.direction != Direction::none)
      throw Error(current.location, "port declaration repeated after ANSI port list");
    do {
      current.location = _token.location;
      current.name = name();
      module.declarations.push_back(current);
      if (take("=")) {
        if (current.direction != Direction::none)
          throw Error(current.location, "port declaration initializer is not supported");
        const auto lhs = add(ExpressionKind::reference, current.location, current.name);
        module.statements.emplace_back(Assignment{current.location, lhs, expression()});
      }
    } while (take(","));
    expect(";");
  }
  void parameters(Module& module, bool header)
  {
    bool local = false;
    bool sign = false;
    bool integer = false;
    Range bounds;
    bool first = true;
    do {
      if (first || is("parameter") || is("localparam")) {
        local = take("localparam");
        if (local && header)
          fail("localparam is not allowed in a Verilog-2005 parameter port list");
        if (!local)
          expect("parameter");
        integer = take("integer");
        sign = integer || take("signed");
        bounds = integer ? Range{} : range();
      }
      const auto location = _token.location;
      auto key = name();
      expect("=");
      const auto value = expression();
      module.parameters.push_back({location, std::move(key), value, bounds, sign, local || (!header && module.parameter_ports), integer});
      first = false;
    } while (take(","));
  }
  std::vector<Connection> connections(bool parameter)
  {
    std::vector<Connection> result;
    if (is(")"))
      return result;
    const bool named = is(".");
    std::unordered_set<std::string> names;
    do {
      Connection connection{_token.location, {}, noExpr};
      if (named) {
        expect(".");
        connection.name = name();
        if (!names.insert(connection.name).second)
          throw Error(connection.location, "duplicate named connection: " + connection.name);
        expect("(");
        if (!is(")"))
          connection.expression = expression();
        expect(")");
      } else if (!is(",") && !is(")"))
        connection.expression = expression();
      if (parameter && connection.expression == noExpr)
        throw Error(connection.location, "empty parameter override is not supported");
      result.push_back(std::move(connection));
    } while (take(","));
    return result;
  }
  void instances(Module& module)
  {
    const auto type = name();
    std::vector<Connection> overrides;
    if (take("#")) {
      expect("(");
      overrides = connections(true);
      expect(")");
    }
    do {
      Instance instance;
      instance.location = _token.location;
      instance.type = type;
      instance.name = name();
      instance.parameters = overrides;
      expect("(");
      instance.ports = connections(false);
      expect(")");
      module.statements.emplace_back(std::move(instance));
    } while (take(","));
    expect(";");
  }
  void attributes()
  {
    while (take("(*")) {
      do {
        (void) name();
        if (take("=")) {
          if (_token.kind == TokenKind::stringliteral)
            next();
          else
            (void) expression();
        }
      } while (take(","));
      expect("*)");
    }
  }
  void directive()
  {
    if (take("`default_nettype")) {
      if (take("wire"))
        _implicit = true;
      else if (take("none"))
        _implicit = false;
      else
        fail("only default_nettype wire/none are supported");
    } else if (take("`resetall"))
      _implicit = true;
    else
      fail("unsupported compiler directive or macro: " + std::string(_token.text) + "; preprocess the source before importing");
  }
  Design& _design;
  Lexer _lexer;
  Token _token;
  unsigned _depth = 0;
  std::vector<uint16_t> _expression_depth;
  bool _implicit = true;
};
}  // namespace
ParseResult parse(std::string_view text, std::string source)
{
  ParseResult result;
  auto design = std::make_unique<Design>();
  design->source = source;
  try {
    Parser(*design, text).run();
    result.design = std::move(design);
  } catch (const detail::Error& error) {
    result.diagnostics.push_back({std::move(source), error.location, error.what()});
  }
  return result;
}
ParseResult readFile(const std::string& path)
{
  // zlib's transparent reader handles plain text and gzip with the same RAII lifetime.
  struct Close
  {
    void operator()(gzFile_s* file) const { gzclose(file); }
  };
  std::unique_ptr<gzFile_s, Close> file(gzopen(path.c_str(), "rb"));
  if (!file)
    return {nullptr, {{path, {}, "cannot open Verilog input"}}};
  gzbuffer(file.get(), 1U << 20);
  std::string source;
  std::array<char, 1U << 16> block;
  for (;;) {
    const int count = gzread(file.get(), block.data(), static_cast<unsigned>(block.size()));
    if (count < 0) {
      int code = 0;
      const std::string message = gzerror(file.get(), &code);
      return {nullptr, {{path, {}, "cannot read Verilog input: " + message}}};
    }
    if (count == 0)
      break;
    source.append(block.data(), count);
  }
  int code = Z_OK;
  const std::string message = gzerror(file.get(), &code);
  if (code != Z_OK && code != Z_STREAM_END)
    return {nullptr, {{path, {}, "incomplete Verilog input: " + message}}};
  return parse(source, path);
}
}  // namespace idb::verilog

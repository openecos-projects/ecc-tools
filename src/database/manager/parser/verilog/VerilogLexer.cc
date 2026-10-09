// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include "VerilogLexer.hh"

#include <cctype>

#include "VerilogValue.hh"

namespace idb::verilog::detail {
namespace {
bool space(char c)
{
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}
bool letter(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool digit(char c)
{
  return c >= '0' && c <= '9';
}
bool identifier(char c)
{
  return letter(c) || digit(c) || c == '$';
}
}  // namespace
char Lexer::peek(size_t ahead) const
{
  return _offset + ahead < _source.size() ? _source[_offset + ahead] : '\0';
}
void Lexer::advance()
{
  if (_source[_offset++] == '\n') {
    ++_location.line;
    _location.column = 1;
  } else {
    ++_location.column;
  }
}
void Lexer::trivia()
{
  for (;;) {
    while (_offset < _source.size() && space(peek()))
      advance();
    if (peek() == '/' && peek(1) == '/') {
      while (_offset < _source.size() && peek() != '\n')
        advance();
    } else if (peek() == '/' && peek(1) == '*') {
      const auto start = _location;
      advance();
      advance();
      while (_offset < _source.size() && !(peek() == '*' && peek(1) == '/'))
        advance();
      if (_offset == _source.size())
        throw Error(start, "unterminated block comment");
      advance();
      advance();
    } else {
      return;
    }
  }
}
Token Lexer::next()
{
  trivia();
  auto start = _offset;
  auto location = _location;
  if (_offset == _source.size())
    return {TokenKind::end, {}, location};
  if (peek() == '\\') {
    advance();
    start = _offset;
    while (_offset < _source.size() && !space(peek())) {
      const auto c = static_cast<unsigned char>(peek());
      if (c < 33 || c > 126)
        throw Error(location, "invalid character in escaped identifier");
      advance();
    }
    if (start == _offset || _offset == _source.size())
      throw Error(location, "escaped identifier requires a name and terminating whitespace");
    return {TokenKind::identifier, _source.substr(start, _offset - start), location, true};
  }
  if (letter(peek()) || peek() == '$' || peek() == '`') {
    const bool directive = peek() == '`';
    advance();
    while (identifier(peek()))
      advance();
    return {directive ? TokenKind::directive : TokenKind::identifier, _source.substr(start, _offset - start), location};
  }
  if (digit(peek()) || peek() == '\'') {
    while (digit(peek()) || peek() == '_')
      advance();
    // IEEE 1364-2005 3.5.1 allows whitespace separating size, base and digits.
    const auto after_size = _offset;
    const auto after_size_location = _location;
    trivia();
    if (peek() == '\'') {
      const auto base_start = _offset;
      advance();
      if (peek() == 's' || peek() == 'S')
        advance();
      if (peek() != 'b' && peek() != 'B' && peek() != 'o' && peek() != 'O' && peek() != 'd' && peek() != 'D' && peek() != 'h'
          && peek() != 'H')
        throw Error(location, "expected Verilog number base after apostrophe");
      advance();
      const auto base_end = _offset;
      trivia();
      const auto value_start = _offset;
      while (identifier(peek()) || peek() == '?')
        advance();
      _literal.assign(_source.substr(start, after_size - start));
      _literal += _source.substr(base_start, base_end - base_start);
      _literal += ' ';
      _literal += _source.substr(value_start, _offset - value_start);
    } else {
      _offset = after_size;
      _location = after_size_location;
      if (identifier(peek()) || peek() == '.')
        throw Error(location, "invalid integer or unsupported real literal");
    }
    auto text = _offset != after_size ? std::string_view(_literal) : _source.substr(start, _offset - start);
    try {
      (void) BitVector::parse(text);
    } catch (const std::exception& error) {
      throw Error(location, error.what());
    }
    return {TokenKind::number, text, location};
  }
  if (peek() == '"') {
    advance();
    while (_offset < _source.size() && peek() != '"') {
      if (peek() == '\n' || peek() == '\r')
        throw Error(location, "newline in string literal");
      if (peek() == '\\') {
        advance();
        if (_offset == _source.size())
          break;
      }
      advance();
    }
    if (_offset == _source.size())
      throw Error(location, "unterminated string literal");
    advance();
    return {TokenKind::stringliteral, _source.substr(start, _offset - start), location};
  }
  for (std::string_view op :
       {"<<<", ">>>", "===", "!==", "**", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "~&", "~|", "~^", "^~", "+:", "-:", "(*", "*)"}) {
    if (_source.substr(_offset, op.size()) == op) {
      for (size_t i = 0; i < op.size(); ++i)
        advance();
      return {TokenKind::symbol, op, location};
    }
  }
  if (std::string_view("()[]{};:,.#=+-*/%&|^~!<>").find(peek()) != std::string_view::npos || peek() == '?') {
    advance();
    return {TokenKind::symbol, _source.substr(start, 1), location};
  }
  throw Error(location, "invalid character or unsupported token");
}
}  // namespace idb::verilog::detail

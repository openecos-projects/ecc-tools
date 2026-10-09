// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once

#include <string_view>

#include "VerilogTypes.hh"

namespace idb::verilog::detail {
enum class TokenKind
{
  end,
  identifier,
  number,
  symbol,
  directive,
  stringliteral
};
struct Token
{
  TokenKind kind;
  std::string_view text;
  SourceLocation location;
  bool escaped = false;
};
class Lexer
{
 public:
  explicit Lexer(std::string_view source) : _source(source) {}
  Token next();

 private:
  char peek(size_t ahead = 0) const;
  void advance();
  void trivia();
  std::string_view _source;
  size_t _offset = 0;
  std::string _literal;
  SourceLocation _location;
};

}  // namespace idb::verilog::detail

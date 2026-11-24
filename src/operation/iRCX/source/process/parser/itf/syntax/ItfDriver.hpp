#pragma once

#include <utility>

#include "ItfDocument.hpp"

namespace ircx::itf::syntax::detail {

class Driver
{
 public:
  Driver(std::string source_name, ParseOptions parse_options) : document{std::move(source_name), {}}, options(parse_options) {}

  void advance(std::string_view token);
  SourceRange eof_range() const { return {cursor, cursor}; }
  bool open_delimiter();
  void close_delimiter();
  bool fail(SourceRange range, std::string message, std::string code = "syntax_error");
  std::optional<Node> number(std::string text, SourceRange range);

  Document document;
  ParseOptions options;
  Position cursor;
  SourceRange token_range;
  std::vector<Diagnostic> diagnostics;

 private:
  std::size_t nesting_ = 0;
};

Node make_node(NodeKind kind, std::string text, SourceRange range, std::vector<Node> children = {});
std::vector<Node> append(std::vector<Node> prefix, std::vector<Node> suffix);

}  // namespace ircx::itf::syntax::detail

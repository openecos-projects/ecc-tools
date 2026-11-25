#pragma once

#include <utility>

#include "ProcessResult.hpp"
#include "TechDocument.hpp"

namespace ircx::tech::syntax::detail {

struct Failure
{
  SourceRange range;
  std::string message;
};
template <class T>
using Result = ProcessResult<T, Failure>;
using Status = Result<void>;

struct Expression {
  Node node;
  std::size_t depth = 1;
};

class Driver {
public:
  explicit Driver(std::string name, ParseOptions parse_options)
      : document{std::move(name), {}}, options(parse_options) {}
  Document document;
  ParseOptions options;
  Position cursor;
  SourceRange token_range;
  std::vector<Diagnostic> diagnostics;
  std::size_t nesting = 0;
  std::size_t polynomial_nesting = 0;
  std::size_t expression_nodes = 0;
  bool pending_polynomial = false;

  void advance(std::string_view token);
  SourceRange eof_range() const { return {cursor, cursor}; }
  bool open_delimiter();
  bool fail(SourceRange range, std::string message);
  bool close_delimiter(); // True when leaving a polynomial's lexical state.
  std::optional<Node> number(std::string_view text);
  Node identifier(std::string_view text) const;
  Node keyword(const KeywordInfo& info, std::string_view spelling);
  Expression expression(Node node, std::vector<Expression> operands = {});
};

inline Node finish(Node node, std::vector<Node> children, SourceRange range,
                   bool braced = false) {
  node.children = std::move(children);
  node.range = range;
  node.braced = braced;
  return node;
}

inline Node container(NodeKind kind, std::vector<Node> children, SourceRange range,
                      std::string text = {}) {
  Node node;
  node.kind = kind;
  node.text = std::move(text);
  return finish(std::move(node), std::move(children), range);
}

inline Node signed_number(Node node, char sign, SourceRange range) {
  node.text.insert(node.text.begin(), sign);
  node.quantity->spelling = node.text;
  if (sign == '-') node.quantity->value = -node.quantity->value;
  node.range = range;
  return node;
}

Status validate(const Document& document, const ParseOptions& options);

} // namespace ircx::tech::syntax::detail

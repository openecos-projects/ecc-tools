#include "ProcessSyntax.hpp"
#include "TechDocument.hpp"

namespace ircx::process_detail {
namespace {
namespace syntax = ircx::tech::syntax;
ProcessPosition position(const syntax::Position& p, std::size_t base = 0) { return {p.line, p.column, p.offset + base}; }
void values(const syntax::Node& node, Field& field, std::size_t base)
{
  if (node.quantity) field.values.push_back({node.quantity->value, node.quantity->suffix, position(node.range.begin, base)});
  if (node.kind == syntax::NodeKind::list)
    for (const auto& child : node.children) values(child, field, base);
}
Field convert(const syntax::Node& node, std::size_t base)
{
  Field field;
  field.key = upper(node.text);
  field.name = node.name();
  field.position = position(node.range.begin, base);
  for (const auto& child : node.children) {
    if (child.kind == syntax::NodeKind::keyword) {
      if (child.qualifier) field.modifiers.push_back(upper(child.text));
      else field.children.push_back(convert(child, base));
    } else if (child.kind == syntax::NodeKind::identifier) field.references.push_back(child.text);
    else values(child, field, base);
  }
  return field;
}
}
Result<Document> parseTech(std::string_view text, const ProcessParseOptions& options)
{
  const bool bom = text.starts_with("\xef\xbb\xbf");
  if (bom) text.remove_prefix(3);
  syntax::ParseOptions syntax_options;
  syntax_options.max_nesting = options.max_nesting;
  syntax_options.max_expression_nodes = options.max_expression_nodes;
  auto result = syntax::parse(text, "<input>", syntax_options);
  if (!result) {
    const auto& diagnostic = result.diagnostics.front();
    auto where = position(diagnostic.range.begin);
    if (bom) where.offset += 3;
    return Failure("syntax_error", where, {}, diagnostic.message);
  }
  Document document;
  for (const auto& node : result.document->statements) document.fields.push_back(convert(node, bom ? 3 : 0));
  return document;
}
}  // namespace ircx::process_detail

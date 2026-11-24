#include "ItfDocument.hpp"
#include "ProcessSyntax.hpp"

namespace ircx::process_detail {
namespace {

namespace syntax = ircx::itf::syntax;

ProcessPosition position(const syntax::Position& source)
{
  return {source.line, source.column, source.offset};
}

void collectValues(const syntax::Node& node, Field& field)
{
  if (node.kind == syntax::NodeKind::kNumber) {
    field.values.push_back({node.number, {}, position(node.range.begin)});
  } else if (node.kind == syntax::NodeKind::kList) {
    for (const auto& child : node.children)
      collectValues(child, field);
  }
}

Field convert(const syntax::Node& node)
{
  Field field;
  field.key = upper(node.text);
  field.name = node.name;
  field.position = position(node.range.begin);
  for (const auto& child : node.children) {
    switch (child.kind) {
      case syntax::NodeKind::kField:
        field.children.push_back(convert(child));
        break;
      case syntax::NodeKind::kReference:
        field.references.push_back(child.text);
        break;
      case syntax::NodeKind::kModifier:
        field.modifiers.push_back(upper(child.text));
        break;
      case syntax::NodeKind::kNumber:
      case syntax::NodeKind::kList:
        collectValues(child, field);
        break;
    }
  }
  return field;
}

}  // namespace

Result<Document> parseItf(std::string_view text, const ProcessParseOptions& options)
{
  auto parsed = syntax::parse(text, "<input>", {options.max_nesting});
  if (!parsed) {
    const auto& diagnostic = parsed.diagnostics.front();
    return Failure{diagnostic.code, position(diagnostic.range.begin), {}, diagnostic.message};
  }
  Document document;
  document.fields.reserve(parsed.document->statements.size());
  for (const auto& node : parsed.document->statements)
    document.fields.push_back(convert(node));
  return document;
}

}  // namespace ircx::process_detail

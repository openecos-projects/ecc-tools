#pragma once

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ircx::tech::syntax {

enum class Keyword {
  unknown,
#define IRCX_TECH_KEYWORD(id, spelling, category, feature) id,
#include "TechKeywords.inc"
#undef IRCX_TECH_KEYWORD
};

enum class Syntax { block, named, model, scalar, flexible, reference, density,
                    table, polynomial, imax, flag, lower };
enum class Feature
{
  standard,
  width_si_table
};
struct KeywordInfo {
  Keyword keyword;
  std::string_view spelling;
  Syntax syntax;
  Feature feature;
};
std::span<const KeywordInfo> keywords() noexcept;
const KeywordInfo* keyword_info(Keyword keyword) noexcept;
const KeywordInfo* lookup_keyword(std::string_view spelling) noexcept;

struct Position {
  std::size_t line = 1;
  std::size_t column = 1;
  std::size_t offset = 0;
};
struct SourceRange { Position begin; Position end; }; // End is exclusive.
std::ostream& operator<<(std::ostream&, const SourceRange&);

struct Quantity {
  double value = 0;             // Numeric part, without unit-prefix conversion.
  std::string suffix;           // Case sensitive, including prefixes/base units.
  std::string spelling;         // Original literal, including any leading sign.
};

enum class NodeKind {
  keyword, number, identifier, list, separator, rectangle, polynomial,
  unary_expression, binary_expression, call_expression
};

// Owning tree; child order is source order, including repeated keywords.
// A named block's first child is its name. Numeric rows are list nodes.
struct Node {
  NodeKind kind = NodeKind::keyword;
  Keyword keyword = Keyword::unknown;
  std::string text;
  SourceRange range;
  bool braced = false;
  bool qualifier = false;       // An option between a table keyword and its '{'.
  std::optional<Quantity> quantity;
  std::vector<Node> children;

  const Node* find(Keyword key) const noexcept; // First immediate keyword child.
  std::string_view name() const noexcept;      // Named block/rectangle only.
};

struct Document {
  std::string source_name;
  std::vector<Node> statements;
};
struct Diagnostic {
  std::string source_name;
  SourceRange range;
  std::string message;
};
std::ostream& operator<<(std::ostream&, const Diagnostic&);

struct ParseOptions {
  bool allow_width_si_table = false;
  std::size_t max_nesting = 128;
  std::size_t max_expression_nodes = 4096;
};
struct ParseResult {
  std::optional<Document> document;
  std::vector<Diagnostic> diagnostics;
  explicit operator bool() const noexcept { return document.has_value(); }
};

ParseResult parse(std::string_view input, std::string source_name = "<string>",
                  ParseOptions options = {});
ParseResult parse_file(const std::string& path, ParseOptions options = {});

} // namespace ircx::tech::syntax

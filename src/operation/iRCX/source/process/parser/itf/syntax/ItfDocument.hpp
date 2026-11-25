#pragma once

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ircx::itf::syntax {

struct Position
{
  std::size_t line = 1;
  std::size_t column = 1;
  std::size_t offset = 0;
};

// Byte offsets include a leading BOM; line/column count starts after it.
// The end position is exclusive, including for tokens spanning multiple lines.
struct SourceRange
{
  Position begin;
  Position end;
};

enum class NodeKind
{
  kField,
  kNumber,
  kReference,
  kList,
  kModifier
};

// Syntax owns its strings and preserves numeric grouping and source ranges.
// Physical interpretation belongs to the shared process model builder.
struct Node
{
  NodeKind kind = NodeKind::kField;
  std::string text;
  std::string name;
  double number = 0;
  SourceRange range;
  std::vector<Node> children;
};

struct Document
{
  std::string source_name;
  std::vector<Node> statements;
};

struct Diagnostic
{
  std::string source_name;
  SourceRange range;
  std::string code;
  std::string message;
};

struct ParseOptions
{
  std::size_t max_nesting = 128;
};

struct [[nodiscard]] ParseResult
{
  std::optional<Document> document;
  std::vector<Diagnostic> diagnostics;

  explicit operator bool() const noexcept { return document.has_value(); }
};

std::ostream& operator<<(std::ostream& stream, const SourceRange& range);
[[nodiscard]] ParseResult parse(std::string_view input, std::string source_name = "<string>", ParseOptions options = {});
[[nodiscard]] ParseResult parse_file(const std::string& path, ParseOptions options = {});

}  // namespace ircx::itf::syntax

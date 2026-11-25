#include <algorithm>
#include <array>
#include <charconv>
#include <climits>
#include <cmath>
#include <fstream>
#include <ostream>

#include "ProcessInput.hpp"
#include "TechDocument.hpp"
#include "TechDriver.hpp"
#include "tech_parser.hpp"

struct yy_buffer_state;
using YY_BUFFER_STATE = yy_buffer_state*;
int ircx_tech_lex_init_extra(ircx::tech::syntax::detail::Driver*, yyscan_t*);
int ircx_tech_lex_destroy(yyscan_t);
YY_BUFFER_STATE ircx_tech__scan_bytes(const char*, int, yyscan_t);
void ircx_tech__delete_buffer(YY_BUFFER_STATE, yyscan_t);

namespace ircx::tech::syntax {
namespace {
constexpr KeywordInfo registry[] = {
#define IRCX_TECH_KEYWORD(id, spelling, category, feature) \
  {Keyword::id, spelling, Syntax::category, Feature::feature},
#include "TechKeywords.inc"
#undef IRCX_TECH_KEYWORD
};

bool equal_keyword(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const char upper = a[i] >= 'a' && a[i] <= 'z' ? static_cast<char>(a[i] - 'a' + 'A') : a[i];
    if (upper != b[i]) return false;
  }
  return true;
}

bool valid_suffix(std::string_view suffix) {
  constexpr std::string_view prefixes[] = {"", "t", "g", "M", "me", "k", "m", "u", "n", "p", "f"};
  constexpr std::string_view bases[] = {"F", "f", "H", "h", "ohm", "m", "A", "a", "V", "v", "W", "w", "s", "Hz"};
  if (suffix == "mil" || suffix == "inch" || suffix == "micron") return true;
  for (auto prefix : prefixes) {
    if (suffix == prefix) return true;
    if (suffix.starts_with(prefix)) {
      const auto base = suffix.substr(prefix.size());
      if (std::ranges::find(bases, base) != std::end(bases)) return true;
    }
  }
  return false;
}

class Scanner {
public:
  Scanner(detail::Driver& driver, std::string_view input) {
    if (ircx_tech_lex_init_extra(&driver, &handle_) != 0) {
      driver.fail({}, "cannot initialize technology scanner");
      return;
    }
    buffer_ = ircx_tech__scan_bytes(input.empty() ? "" : input.data(), static_cast<int>(input.size()), handle_);
  }
  explicit operator bool() const { return handle_ && buffer_; }
  ~Scanner() {
    if (buffer_) ircx_tech__delete_buffer(buffer_, handle_);
    if (handle_) ircx_tech_lex_destroy(handle_);
  }
  Scanner(const Scanner&) = delete;
  Scanner& operator=(const Scanner&) = delete;
  yyscan_t handle() const noexcept { return handle_; }
private:
  yyscan_t handle_ = nullptr;
  YY_BUFFER_STATE buffer_ = nullptr;
};
} // namespace

std::span<const KeywordInfo> keywords() noexcept { return registry; }
const KeywordInfo* keyword_info(Keyword keyword) noexcept {
  for (const auto& info : registry) if (info.keyword == keyword) return &info;
  return nullptr;
}
const KeywordInfo* lookup_keyword(std::string_view spelling) noexcept {
  for (const auto& info : registry) if (equal_keyword(spelling, info.spelling)) return &info;
  return nullptr;
}
const Node* Node::find(Keyword key) const noexcept {
  for (const auto& child : children)
    if (child.kind == NodeKind::keyword && child.keyword == key) return &child;
  return nullptr;
}
std::string_view Node::name() const noexcept {
  if (!children.empty() && children.front().kind == NodeKind::identifier &&
      (kind == NodeKind::rectangle || keyword == Keyword::metal || keyword == Keyword::dielectric ||
       keyword == Keyword::via || keyword == Keyword::viamodel || keyword == Keyword::substrate))
    return children.front().text;
  return {};
}
std::ostream& operator<<(std::ostream& out, const SourceRange& range) {
  return out << range.begin.line << ':' << range.begin.column;
}
std::ostream& operator<<(std::ostream& out, const Diagnostic& diagnostic) {
  return out << diagnostic.source_name << ':' << diagnostic.range << ": " << diagnostic.message;
}

ParseResult parse(std::string_view input, std::string source_name, ParseOptions options) {
  detail::Driver driver(std::move(source_name), options);
  if (input.size() > static_cast<std::size_t>(INT_MAX - 2)) {
    driver.fail({}, "input exceeds scanner size limit");
    return {std::nullopt, std::move(driver.diagnostics)};
  }
  Scanner scanner(driver, input);
  if (!scanner) {
    driver.fail({}, "cannot initialize technology scanner");
    return {std::nullopt, std::move(driver.diagnostics)};
  }
  detail::Parser parser(driver, scanner.handle());
  if (parser.parse() == 0 && driver.diagnostics.empty()) {
    auto checked = detail::validate(driver.document, options);
    if (checked)
      return {std::move(driver.document), {}};
    driver.fail(checked.error.range, checked.error.message);
  }
  return {std::nullopt, std::move(driver.diagnostics)};
}

ParseResult parse_file(const std::string& path, ParseOptions options) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return {std::nullopt, {{path, {}, "cannot open input file"}}};
  std::string input;
  if (!process_detail::readText(file, input))
    return {std::nullopt, {{path, {}, "cannot read input file"}}};
  return parse(input, path, options);
}
} // namespace ircx::tech::syntax

namespace ircx::tech::syntax::detail {
void Driver::advance(std::string_view token) {
  token_range.begin = cursor;
  for (const char character : token) {
    ++cursor.offset;
    if (character == '\n') { ++cursor.line; cursor.column = 1; }
    else ++cursor.column;
  }
  token_range.end = cursor;
}
bool Driver::fail(SourceRange range, std::string message)
{
  if (diagnostics.empty())
    diagnostics.push_back({document.source_name, range, std::move(message)});
  return false;
}
bool Driver::open_delimiter()
{
  if (++nesting > options.max_nesting)
    return fail(token_range, "nesting limit exceeded");
  return true;
}
bool Driver::close_delimiter() {
  const bool done = polynomial_nesting != 0 && nesting == polynomial_nesting;
  if (nesting != 0) --nesting;
  if (done) polynomial_nesting = 0;
  return done;
}
std::optional<Node> Driver::number(std::string_view text)
{
  double value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
  const std::string_view suffix(result.ptr, static_cast<std::size_t>(text.data() + text.size() - result.ptr));
  if (result.ec != std::errc{} || !std::isfinite(value) || !valid_suffix(suffix)) {
    fail(token_range, "invalid number or unit suffix: " + std::string(text));
    return std::nullopt;
  }
  Node node;
  node.kind = NodeKind::number;
  node.text = text;
  node.range = token_range;
  node.quantity = Quantity{value, std::string(suffix), std::string(text)};
  return node;
}
Node Driver::identifier(std::string_view text) const {
  Node node;
  node.kind = NodeKind::identifier;
  node.text = text;
  node.range = token_range;
  return node;
}
Node Driver::keyword(const KeywordInfo& info, std::string_view spelling) {
  Node node;
  node.keyword = info.keyword;
  node.text = spelling;
  node.range = token_range;
  return node;
}
Expression Driver::expression(Node node, std::vector<Expression> operands) {
  std::size_t depth = 1;
  for (auto& operand : operands) {
    depth = std::max(depth, operand.depth + 1);
    node.children.push_back(std::move(operand.node));
  }
  if (++expression_nodes > options.max_expression_nodes)
    fail(node.range, "expression node limit exceeded");
  if (depth > options.max_nesting)
    fail(node.range, "expression depth limit exceeded");
  return {std::move(node), depth};
}
} // namespace ircx::tech::syntax::detail

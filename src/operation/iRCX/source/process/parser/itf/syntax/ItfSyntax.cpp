#include <charconv>
#include <climits>
#include <cmath>
#include <fstream>
#include <iterator>
#include <ostream>
#include <utility>

#include "ItfDocument.hpp"
#include "ItfDriver.hpp"
#include "ProcessInput.hpp"
#include "itf_parser.hpp"

struct yy_buffer_state;
using YY_BUFFER_STATE = yy_buffer_state*;
int ircx_itf_lex_init_extra(ircx::itf::syntax::detail::Driver*, yyscan_t*);
int ircx_itf_lex_destroy(yyscan_t);
YY_BUFFER_STATE ircx_itf__scan_bytes(const char*, int, yyscan_t);
void ircx_itf__delete_buffer(YY_BUFFER_STATE, yyscan_t);

namespace ircx::itf::syntax {
namespace {

// Each parse owns its scanner and buffer, including on early error returns.
class Scanner
{
 public:
  Scanner(detail::Driver& driver, std::string_view input)
  {
    if (ircx_itf_lex_init_extra(&driver, &handle_) != 0) {
      driver.fail({}, "cannot initialize ITF scanner");
      return;
    }
    buffer_ = ircx_itf__scan_bytes(input.empty() ? "" : input.data(), static_cast<int>(input.size()), handle_);
  }
  ~Scanner()
  {
    if (buffer_)
      ircx_itf__delete_buffer(buffer_, handle_);
    if (handle_)
      ircx_itf_lex_destroy(handle_);
  }
  Scanner(const Scanner&) = delete;
  Scanner& operator=(const Scanner&) = delete;

  explicit operator bool() const noexcept { return handle_ && buffer_; }
  yyscan_t handle() const noexcept { return handle_; }

 private:
  yyscan_t handle_ = nullptr;
  YY_BUFFER_STATE buffer_ = nullptr;
};

}  // namespace

std::ostream& operator<<(std::ostream& stream, const SourceRange& range)
{
  return stream << range.begin.line << ':' << range.begin.column;
}

ParseResult parse(std::string_view input, std::string source_name, ParseOptions options)
{
  detail::Driver driver(std::move(source_name), options);
  if (input.size() > static_cast<std::size_t>(INT_MAX - 2)) {
    driver.fail({}, "input exceeds scanner size limit");
    return {std::nullopt, std::move(driver.diagnostics)};
  }
  if (input.starts_with("\xef\xbb\xbf")) {
    input.remove_prefix(3);
    driver.cursor.offset = 3;
  }
  Scanner scanner(driver, input);
  if (!scanner) {
    driver.fail({}, "cannot initialize ITF scanner");
    return {std::nullopt, std::move(driver.diagnostics)};
  }
  detail::Parser parser(driver, scanner.handle());
  if (parser.parse() == 0 && driver.diagnostics.empty())
    return {std::move(driver.document), {}};
  return {std::nullopt, std::move(driver.diagnostics)};
}

ParseResult parse_file(const std::string& path, ParseOptions options)
{
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    return {std::nullopt, {{path, {}, "file_io", "cannot open input file"}}};
  std::string input;
  if (!process_detail::readText(stream, input))
    return {std::nullopt, {{path, {}, "file_io", "cannot read input file"}}};
  return parse(input, path, options);
}

}  // namespace ircx::itf::syntax

namespace ircx::itf::syntax::detail {

void Driver::advance(std::string_view token)
{
  token_range.begin = cursor;
  for (const char character : token) {
    ++cursor.offset;
    if (character == '\n') {
      ++cursor.line;
      cursor.column = 1;
    } else {
      ++cursor.column;
    }
  }
  token_range.end = cursor;
}

bool Driver::fail(SourceRange range, std::string message, std::string code)
{
  if (diagnostics.empty())
    diagnostics.push_back({document.source_name, range, std::move(code), std::move(message)});
  return false;
}

bool Driver::open_delimiter()
{
  if (nesting_ >= options.max_nesting)
    return fail(token_range, "nesting limit exceeded");
  ++nesting_;
  return true;
}

void Driver::close_delimiter()
{
  if (nesting_ > 0)
    --nesting_;
}

std::optional<Node> Driver::number(std::string text, SourceRange range)
{
  std::string_view literal(text);
  if (literal.starts_with('+'))
    literal.remove_prefix(1);
  double value = 0;
  const auto converted = std::from_chars(literal.data(), literal.data() + literal.size(), value, std::chars_format::general);
  if (converted.ec != std::errc{} || converted.ptr != literal.data() + literal.size() || !std::isfinite(value)) {
    fail(range, "expected a finite ITF number: " + text, "invalid_number");
    return std::nullopt;
  }
  auto node = make_node(NodeKind::kNumber, std::move(text), range);
  node.number = value;
  return node;
}

Node make_node(NodeKind kind, std::string text, SourceRange range, std::vector<Node> children)
{
  Node node;
  node.kind = kind;
  node.text = std::move(text);
  node.range = range;
  node.children = std::move(children);
  return node;
}

std::vector<Node> append(std::vector<Node> prefix, std::vector<Node> suffix)
{
  prefix.insert(prefix.end(), std::make_move_iterator(suffix.begin()), std::make_move_iterator(suffix.end()));
  return prefix;
}

}  // namespace ircx::itf::syntax::detail

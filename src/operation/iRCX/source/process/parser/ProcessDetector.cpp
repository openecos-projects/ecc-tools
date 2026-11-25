#include <cctype>

#include "ProcessSyntax.hpp"

namespace ircx::process_detail {
std::string upper(std::string_view text)
{
  std::string result(text);
  for (char& c : result) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  return result;
}

namespace {
// These tokens are only format signatures, never an input to either parser.
struct Token
{
  std::string text;
  ProcessPosition position;
  bool quoted = false;
};

Result<std::vector<Token>> tokenize(std::string_view text, bool dollar_comments)
{
  std::vector<Token> tokens;
  ProcessPosition position;
  std::size_t i = 0;
  if (text.starts_with("\xef\xbb\xbf")) { i = 3; position.offset = 3; }
  const auto advance = [&] {
    if (text[i++] == '\n') { ++position.line; position.column = 1; }
    else ++position.column;
    ++position.offset;
  };
  while (i < text.size()) {
    if (std::isspace(static_cast<unsigned char>(text[i]))) { advance(); continue; }
    if (text[i] == '#' || (dollar_comments && text[i] == '$') || (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/')) {
      while (i < text.size() && text[i] != '\n') advance();
      continue;
    }
    Token token{{}, position, false};
    if (text[i] == '"') {
      token.quoted = true;
      advance();
      while (i < text.size() && text[i] != '"') { token.text += text[i]; advance(); }
      if (i == text.size())
        return Failure("syntax_error", token.position, {}, "unterminated quoted name");
      advance();
    } else if (std::string_view("{}()=,").find(text[i]) != std::string_view::npos) {
      token.text += text[i]; advance();
    } else {
      while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))
             && std::string_view("{}()=,#\"").find(text[i]) == std::string_view::npos) {
        if (dollar_comments && text[i] == '$')
          break;
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') break;
        token.text += text[i]; advance();
      }
    }
    if (token.text.empty() && !token.quoted)
      return Failure("syntax_error", position, {}, "invalid token");
    tokens.push_back(std::move(token));
  }
  return tokens;
}

Result<SyntaxKind> signatures(std::string_view text, bool dollar_comments)
{
  auto parsed = tokenize(text, dollar_comments);
  if (!parsed)
    return parsed.error;
  const auto& tokens = *parsed;
  bool itf = false, tech = false;
  int depth = 0;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const auto& token = tokens[i];
    if (token.quoted) continue;
    // Assignment payloads may be identifiers that happen to spell a keyword.
    if (i > 0 && tokens[i - 1].text == "=") continue;
    const auto key = upper(token.text);
    if (depth == 0 && key == "TECHNOLOGY") {
      // A technology label is an identifier, including when it spells a keyword.
      if (i + 1 < tokens.size() && tokens[i + 1].text == "=") ++i;
      if (i + 1 < tokens.size()) ++i;
      continue;
    }
    if (depth == 0 && i + 2 < tokens.size() && tokens[i + 2].text == "{") {
      itf |= key == "CONDUCTOR";
      tech |= key == "METAL";
      // Skip the name: it is allowed to be a keyword.
      if (key == "CONDUCTOR" || key == "METAL" || key == "VIA" || key == "DIELECTRIC") { ++i; continue; }
    }
    if (depth == 0 && key == "UNITS" && i + 1 < tokens.size() && tokens[i + 1].text == "{") tech = true;
    if (i + 1 < tokens.size()) {
      const bool assignment = tokens[i + 1].text == "=";
      if (depth == 1 && (key == "FROM" || key == "TO") && assignment) {
        itf = true;
        if (i + 2 < tokens.size()) i += 2; // Skip the referenced layer name.
        continue;
      }
      if (key == "THICKNESS" || key == "AREA" || key == "HALF_NODE_SCALE_FACTOR") {
        if (assignment) itf = true;
        // An axis block is shared syntax, not a scalar declaration.
        else if (tokens[i + 1].text != "{") tech = true;
      }
      if ((key == "FROM" || key == "TO" || key == "RPSQ" || key == "RPV") && assignment) itf = true;
      if (depth == 1 && (key == "UPPERLAYER" || key == "LOWERLAYER") && !assignment) tech = true;
    }
    if (token.text == "{") ++depth;
    if (token.text == "}") --depth;
  }
  if (itf && tech) return SyntaxKind::kMixed;
  return itf ? SyntaxKind::kItf : tech ? SyntaxKind::kTech : SyntaxKind::kUnknown;
}
}  // namespace

Result<SyntaxKind> detectSyntax(std::string_view text)
{
  const auto plain = signatures(text, false);
  if (text.find('$') == std::string_view::npos)
    return plain;
  // Dollar comments are specific to ITF. A second lexical view prevents
  // comment contents from claiming Tech syntax, while preserving Tech names
  // such as "$wire" in the ordinary view. The selected frontend still parses
  // the entire original input and reports errors without semantic fallback.
  const auto commented = signatures(text, true);
  if (commented && *commented == SyntaxKind::kItf && (!plain || *plain != SyntaxKind::kTech))
    return commented;
  // A valid ITF can lack strong signatures (for example a via-only table).
  // If the views disagree, let the strict full-file syntax probes decide;
  // neither comment contents nor a Tech identifier containing '$' wins here.
  if (commented && (!plain || *commented != *plain))
    return SyntaxKind::kUnknown;
  return plain;
}
}  // namespace ircx::process_detail

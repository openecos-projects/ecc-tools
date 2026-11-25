%option reentrant noyywrap nounput noinput nodefault never-interactive
%option prefix="ircx_itf_"
%option extra-type="ircx::itf::syntax::detail::Driver*"

%{
#include "ItfDriver.hpp"
#include "itf_parser.hpp"

#include <string>
#include <string_view>

#define YY_DECL ircx::itf::syntax::detail::Parser::symbol_type ircx_itf_lex(yyscan_t yyscanner)
#define YY_USER_ACTION yyextra->advance(std::string_view(yytext, static_cast<std::size_t>(yyleng)));

namespace {
using Parser = ircx::itf::syntax::detail::Parser;
using Driver = ircx::itf::syntax::detail::Driver;

Parser::symbol_type word(Driver& driver, std::string text)
{
  // Recognize syntax roles only. Model support and physical validation are
  // handled after parsing; unknown fields must remain visible in the AST.
  std::string key = text;
  for (char& character : key) {
    if (character >= 'a' && character <= 'z')
      character = static_cast<char>(character - 'a' + 'A');
  }
  if (key == "CONDUCTOR" || key == "VIA" || key == "DIELECTRIC")
    return Parser::make_NAMED(std::move(text), driver.token_range);
  if (key == "TECHNOLOGY")
    return Parser::make_TECHNOLOGY(std::move(text), driver.token_range);
  if (key == "ETCH_VS_WIDTH_AND_SPACING" || key == "THICKNESS_VS_WIDTH_AND_SPACING" || key == "ETCH_VS_WIDTH_AND_LENGTH")
    return Parser::make_TABLE(std::move(text), driver.token_range);
  if (key == "RESISTIVE_ONLY" || key == "CAPACITIVE_ONLY" || key == "ETCH_FROM_TOP")
    return Parser::make_MODIFIER(std::move(text), driver.token_range);
  // Keep malformed numbers whole so they cannot become a valid numeric prefix.
  // Conversion is deferred to the grammar: numeric-looking names remain names.
  if (std::string_view("+-.0123456789").find(text.front()) != std::string_view::npos)
    return Parser::make_NUMBER(std::move(text), driver.token_range);
  return Parser::make_WORD(std::move(text), driver.token_range);
}
}  // namespace
%}

SPACE       [ \t\r\n\f\v]+
ATOM        [^\x00-\x20\x7f{}()=,#\"$/]+
WORD        {ATOM}("/"{ATOM})*
QUOTED      \"[^\"\x00]*\"

%%

{SPACE}                 { /* Location accounting is done by YY_USER_ACTION. */ }
[$#][^\r\n\x00]*        { /* ITF dollar comments; retain the accepted hash extension. */ }
"//"[^\r\n\x00]*        { /* Retain the accepted slash-comment extension. */ }

{QUOTED}                {
                          const auto length = static_cast<std::size_t>(yyleng - 2);
                          return Parser::make_STRING(std::string(yytext + 1, length), yyextra->token_range);
                        }
\"[^\"\x00]*           {
                          yyextra->fail(yyextra->token_range, "unterminated quoted name");
                          return Parser::make_YYerror(yyextra->token_range);
                        }

"{"                     {
                          if (!yyextra->open_delimiter())
                            return Parser::make_YYerror(yyextra->token_range);
                          return Parser::make_LBRACE(yyextra->token_range);
                        }
"("                     {
                          if (!yyextra->open_delimiter())
                            return Parser::make_YYerror(yyextra->token_range);
                          return Parser::make_LPAREN(yyextra->token_range);
                        }
"}"                     {
                          yyextra->close_delimiter();
                          return Parser::make_RBRACE(yyextra->token_range);
                        }
")"                     {
                          yyextra->close_delimiter();
                          return Parser::make_RPAREN(yyextra->token_range);
                        }
"="                     { return Parser::make_EQUAL(yyextra->token_range); }
","                     { return Parser::make_COMMA(yyextra->token_range); }
{WORD}                  {
                          return word(*yyextra, std::string(yytext, static_cast<std::size_t>(yyleng)));
                        }

.                       {
                          yyextra->fail(yyextra->token_range, "unexpected character in ITF input");
                          return Parser::make_YYerror(yyextra->token_range);
                        }
<<EOF>>                 { return Parser::make_END(yyextra->eof_range()); }

%%

%option reentrant noyywrap nounput noinput nodefault never-interactive
%option prefix="ircx_tech_"
%option extra-type="ircx::tech::syntax::detail::Driver*"
%x EXPR

%{
#include "tech_parser.hpp"
#include "TechDriver.hpp"

#define YY_DECL ircx::tech::syntax::detail::Parser::symbol_type ircx_tech_lex(yyscan_t yyscanner)
#define YY_USER_ACTION yyextra->advance(std::string_view(yytext, static_cast<std::size_t>(yyleng)));
using Parser = ircx::tech::syntax::detail::Parser;

static Parser::symbol_type word(ircx::tech::syntax::detail::Driver& driver, std::string_view text) {
  const auto* info = ircx::tech::syntax::lookup_keyword(text);
  if (!info) return Parser::make_IDENT(driver.identifier(text), driver.token_range);
  auto node = driver.keyword(*info, text);
  const auto range = driver.token_range;
  switch (info->syntax) {
    case ircx::tech::syntax::Syntax::block: return Parser::make_BLOCK(std::move(node), range);
    case ircx::tech::syntax::Syntax::named: return Parser::make_NAMED(std::move(node), range);
    case ircx::tech::syntax::Syntax::model: return Parser::make_MODEL(std::move(node), range);
    case ircx::tech::syntax::Syntax::scalar: return Parser::make_SCALAR(std::move(node), range);
    case ircx::tech::syntax::Syntax::flexible: return Parser::make_FLEXIBLE(std::move(node), range);
    case ircx::tech::syntax::Syntax::reference: return Parser::make_REFERENCE(std::move(node), range);
    case ircx::tech::syntax::Syntax::lower: return Parser::make_LOWER(std::move(node), range);
    case ircx::tech::syntax::Syntax::density: return Parser::make_DENSITY(std::move(node), range);
    case ircx::tech::syntax::Syntax::table: return Parser::make_TABLE(std::move(node), range);
    case ircx::tech::syntax::Syntax::polynomial: return Parser::make_POLYNOMIAL(std::move(node), range);
    case ircx::tech::syntax::Syntax::flag: return Parser::make_FLAG(std::move(node), range);
    case ircx::tech::syntax::Syntax::imax: return Parser::make_IMAX(std::move(node), range);
  }
  driver.fail(range, "invalid keyword category");
  return Parser::make_YYerror(range);
}
%}

NUM ([0-9]|\.[0-9])([A-Za-z0-9_.]|[eE][+-])*
WORD [A-Za-z_$][A-Za-z0-9_.$:/-]*
EXPRWORD [A-Za-z_$][A-Za-z0-9_$]*

%%
<*>[ \t\r\n]+    {}
<*>#[^\r\n]*     {}
<*>{NUM}          {
  auto number = yyextra->number(yytext);
  if (!number) return Parser::make_YYerror(yyextra->token_range);
  return Parser::make_NUMBER(std::move(*number), yyextra->token_range);
}
<INITIAL>{WORD}   { return word(*yyextra, yytext); }
<EXPR>{EXPRWORD}  {
  const auto* info = ircx::tech::syntax::lookup_keyword(yytext);
  if (info && info->keyword == ircx::tech::syntax::Keyword::imax)
    return Parser::make_IMAX(yyextra->keyword(*info, yytext), yyextra->token_range);
  return Parser::make_IDENT(yyextra->identifier(yytext), yyextra->token_range);
}
<*>"{" {
  if (!yyextra->open_delimiter()) return Parser::make_YYerror(yyextra->token_range);
  if (yyextra->pending_polynomial) {
    yyextra->pending_polynomial = false;
    yyextra->polynomial_nesting = yyextra->nesting;
    BEGIN(EXPR);
  }
  return Parser::make_LBRACE(yyextra->token_range);
}
<*>"}" {
  if (yyextra->close_delimiter()) BEGIN(INITIAL);
  return Parser::make_RBRACE(yyextra->token_range);
}
<*>"(" { if (!yyextra->open_delimiter()) return Parser::make_YYerror(yyextra->token_range); return Parser::make_LPAREN(yyextra->token_range); }
<*>")" { yyextra->close_delimiter(); return Parser::make_RPAREN(yyextra->token_range); }
<*>"," { return Parser::make_COMMA(yyextra->token_range); }
<*>"+" { return Parser::make_PLUS(yyextra->token_range); }
<*>"-" { return Parser::make_MINUS(yyextra->token_range); }
<*>"*" { return Parser::make_STAR(yyextra->token_range); }
<*>"/" { return Parser::make_SLASH(yyextra->token_range); }
<*>"^" { return Parser::make_CARET(yyextra->token_range); }
<*>. { yyextra->fail(yyextra->token_range, "unexpected character in technology file"); return Parser::make_YYerror(yyextra->token_range); }
<*><<EOF>> { return Parser::make_END(yyextra->eof_range()); }
%%

%top{
// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include "VerilogParser.hh"
#include "VerilogLexer.hh"
#include <cctype>
#include <stdexcept>
#define YY_FATAL_ERROR(message) throw std::runtime_error(message)
#define YY_BUF_SIZE 65536
#define YY_READ_BUF_SIZE 65536
}
%option reentrant noyywrap nounput noinput nodefault never-interactive
%option prefix="IdbVerilog"
%option extra-type="idb::verilog::detail::ParseContext*"
%x COMMENT LINE_COMMENT NUMBER_BASE NUMBER_DIGITS STRING_BODY
%{
namespace idb::verilog::detail { grammar::Parser::symbol_type scanToken(void* scanner); }
#define YY_DECL idb::verilog::grammar::Parser::symbol_type idb::verilog::detail::scanToken(void* yyscanner)
#define YY_INPUT(buffer, result, capacity) result = static_cast<int>(yyextra->read(buffer, capacity));
#define YY_USER_ACTION yyextra->match(std::string_view(yytext, yyleng));
using GeneratedParser = idb::verilog::grammar::Parser;
using idb::verilog::detail::Error;
namespace {
GeneratedParser::symbol_type word(std::string text, idb::verilog::detail::ParseContext& context)
{
  const auto location = context.token(context.location());
  if (idb::verilog::isKeyword(text, context.language()))
    return GeneratedParser::make_RESERVED(std::move(text), location);
  return GeneratedParser::make_IDENTIFIER(std::move(text), location);
}
}  // namespace
%}
WS      [ \t\r\n\f]
ID      [A-Za-z_][A-Za-z0-9_$]*
BASE    \'[sS]?[bBoOdDhH]
%%
 /* Bound trivia matches: growing a Flex token across refills rescans its prefix. */
<INITIAL,NUMBER_BASE,NUMBER_DIGITS>{WS}{1,256} {}
<INITIAL,NUMBER_BASE,NUMBER_DIGITS>"//" {
  yyextra->comment_state = YY_START;
  BEGIN(LINE_COMMENT);
}
<LINE_COMMENT>[^\n]{1,256} {}
<LINE_COMMENT>\n { BEGIN(yyextra->comment_state); }
<LINE_COMMENT><<EOF>> { BEGIN(yyextra->comment_state); }
<INITIAL,NUMBER_BASE,NUMBER_DIGITS>"/*" {
  yyextra->comment_state = YY_START;
  yyextra->comment_location = yyextra->location();
  BEGIN(COMMENT);
}
<COMMENT>"*/" { BEGIN(yyextra->comment_state); }
<COMMENT>[^*]{1,256} {}
<COMMENT>"*" {}
<COMMENT><<EOF>> { throw Error(yyextra->comment_location, "unterminated block comment"); }

<INITIAL>\\[^ \t\r\n\f]+/{WS} {
  for (int i = 1; i < yyleng; ++i)
    if (static_cast<unsigned char>(yytext[i]) < 33 || static_cast<unsigned char>(yytext[i]) > 126)
      throw Error(yyextra->location(), "invalid character in escaped identifier");
  return GeneratedParser::make_IDENTIFIER(std::string(yytext + 1, yyleng - 1), yyextra->token(yyextra->location()));
}
<INITIAL>\\[^ \t\r\n\f]* { throw Error(yyextra->location(), "escaped identifier requires a name and terminating whitespace"); }
<INITIAL>"module" { return GeneratedParser::make_MODULE(yyextra->token(yyextra->location())); }
<INITIAL>"endmodule" { return GeneratedParser::make_ENDMODULE(yyextra->token(yyextra->location())); }
<INITIAL>"input" { return GeneratedParser::make_INPUT(yyextra->token(yyextra->location())); }
<INITIAL>"output" { return GeneratedParser::make_OUTPUT(yyextra->token(yyextra->location())); }
<INITIAL>"inout" { return GeneratedParser::make_INOUT(yyextra->token(yyextra->location())); }
<INITIAL>"wire" { return GeneratedParser::make_WIRE(yyextra->token(yyextra->location())); }
<INITIAL>"tri" { return GeneratedParser::make_TRI(yyextra->token(yyextra->location())); }
<INITIAL>"wand" { return GeneratedParser::make_WAND(yyextra->token(yyextra->location())); }
<INITIAL>"wor" { return GeneratedParser::make_WOR(yyextra->token(yyextra->location())); }
<INITIAL>"supply0" { return GeneratedParser::make_SUPPLY0(yyextra->token(yyextra->location())); }
<INITIAL>"supply1" { return GeneratedParser::make_SUPPLY1(yyextra->token(yyextra->location())); }
<INITIAL>"signed" { return GeneratedParser::make_SIGNED(yyextra->token(yyextra->location())); }
<INITIAL>"unsigned" { if (yyextra->sv()) return GeneratedParser::make_UNSIGNED(yyextra->token(yyextra->location())); return word(std::string(yytext, yyleng), *yyextra); }
<INITIAL>"logic" { if (yyextra->sv()) return GeneratedParser::make_LOGIC(yyextra->token(yyextra->location())); return word(std::string(yytext, yyleng), *yyextra); }
<INITIAL>"var" { if (yyextra->sv()) return GeneratedParser::make_VAR(yyextra->token(yyextra->location())); return word(std::string(yytext, yyleng), *yyextra); }
<INITIAL>"integer" { return GeneratedParser::make_INTEGER(yyextra->token(yyextra->location())); }
<INITIAL>"int" { if (yyextra->sv()) return GeneratedParser::make_INT(yyextra->token(yyextra->location())); return word(std::string(yytext, yyleng), *yyextra); }
<INITIAL>"parameter" { return GeneratedParser::make_PARAMETER(yyextra->token(yyextra->location())); }
<INITIAL>"localparam" { return GeneratedParser::make_LOCALPARAM(yyextra->token(yyextra->location())); }
<INITIAL>"assign" { return GeneratedParser::make_ASSIGN(yyextra->token(yyextra->location())); }
<INITIAL>"defparam" { return GeneratedParser::make_DEFPARAM(yyextra->token(yyextra->location())); }
<INITIAL>"genvar" { return GeneratedParser::make_GENVAR(yyextra->token(yyextra->location())); }
<INITIAL>"generate" { return GeneratedParser::make_GENERATE(yyextra->token(yyextra->location())); }
<INITIAL>"endgenerate" { return GeneratedParser::make_ENDGENERATE(yyextra->token(yyextra->location())); }
<INITIAL>"begin" { return GeneratedParser::make_BEGIN_BLOCK(yyextra->token(yyextra->location())); }
<INITIAL>"end" { return GeneratedParser::make_END_BLOCK(yyextra->token(yyextra->location())); }
<INITIAL>"if" { return GeneratedParser::make_IF(yyextra->token(yyextra->location())); }
<INITIAL>"else" { return GeneratedParser::make_ELSE(yyextra->token(yyextra->location())); }
<INITIAL>"for" { return GeneratedParser::make_FOR(yyextra->token(yyextra->location())); }
<INITIAL>"case" { return GeneratedParser::make_CASE(yyextra->token(yyextra->location())); }
<INITIAL>"endcase" { return GeneratedParser::make_ENDCASE(yyextra->token(yyextra->location())); }
<INITIAL>"default" { return GeneratedParser::make_DEFAULT(yyextra->token(yyextra->location())); }
<INITIAL>{ID} { return word(std::string(yytext, yyleng), *yyextra); }
<INITIAL>\$[A-Za-z0-9_$]* { return GeneratedParser::make_SYSTEM_FUNCTION(std::string(yytext, yyleng), yyextra->token(yyextra->location())); }
<INITIAL>"`default_nettype" { return GeneratedParser::make_DEFAULT_NETTYPE(yyextra->token(yyextra->location())); }
<INITIAL>"`resetall" { return GeneratedParser::make_RESETALL(yyextra->token(yyextra->location())); }
<INITIAL>`[A-Za-z0-9_$]* { throw Error(yyextra->location(), "unsupported compiler directive or macro: " + std::string(yytext, yyleng)); }

<INITIAL>[0-9][0-9_]* { yyextra->startLiteral(std::string_view(yytext, yyleng)); BEGIN(NUMBER_BASE); }
<INITIAL>{BASE} { yyextra->startLiteral(std::string_view(yytext, yyleng)); BEGIN(NUMBER_DIGITS); }
<INITIAL>\'[01xXzZ][A-Za-z0-9_$]+ { throw Error(yyextra->location(), "invalid unbased unsized literal"); }
<INITIAL>\'[01xXzZ] {
  yyextra->requireSv(yyextra->location());
  return GeneratedParser::make_FILL(std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(yytext[1])))), yyextra->token(yyextra->location()));
}
<INITIAL>\' { throw Error(yyextra->location(), "expected Verilog number base after apostrophe"); }
<NUMBER_BASE>{BASE} { yyextra->appendLiteral(std::string_view(yytext, yyleng)); BEGIN(NUMBER_DIGITS); }
<NUMBER_BASE>\' { throw Error(yyextra->literalLocation(), "expected Verilog number base after apostrophe"); }
<NUMBER_BASE>[A-Za-z_$0-9.] {
  if (yyextra->adjacentLiteral()) throw Error(yyextra->literalLocation(), "invalid integer or unsupported real literal");
  yyextra->rewindMatch(); yyless(0); BEGIN(INITIAL);
  return GeneratedParser::make_NUMBER(yyextra->finishNumber(), yyextra->token(yyextra->literalLocation()));
}
<NUMBER_BASE>. {
  yyextra->rewindMatch(); yyless(0); BEGIN(INITIAL);
  return GeneratedParser::make_NUMBER(yyextra->finishNumber(), yyextra->token(yyextra->literalLocation()));
}
<NUMBER_BASE><<EOF>> {
  BEGIN(INITIAL);
  return GeneratedParser::make_NUMBER(yyextra->finishNumber(), yyextra->token(yyextra->literalLocation()));
}
<NUMBER_DIGITS>[A-Za-z0-9_$?]+ {
  yyextra->appendLiteral(" "); yyextra->appendLiteral(std::string_view(yytext, yyleng)); BEGIN(INITIAL);
  return GeneratedParser::make_NUMBER(yyextra->finishNumber(), yyextra->token(yyextra->literalLocation()));
}
<NUMBER_DIGITS>(.|\n) { throw Error(yyextra->literalLocation(), "missing digits in based number"); }
<NUMBER_DIGITS><<EOF>> { throw Error(yyextra->literalLocation(), "missing digits in based number"); }

<INITIAL>\" { yyextra->startLiteral(std::string_view(yytext, yyleng)); BEGIN(STRING_BODY); }
<STRING_BODY>[^"\\\n\r]{1,256} { yyextra->appendLiteral(std::string_view(yytext, yyleng)); }
<STRING_BODY>\\(.|\n) { yyextra->appendLiteral(std::string_view(yytext, yyleng)); }
<STRING_BODY>\" {
  yyextra->appendLiteral("\""); BEGIN(INITIAL);
  return GeneratedParser::make_STRING(yyextra->finishString(), yyextra->token(yyextra->literalLocation()));
}
<STRING_BODY>[\n\r] { throw Error(yyextra->literalLocation(), "newline in string literal"); }
<STRING_BODY>\\ { throw Error(yyextra->literalLocation(), "unterminated string literal"); }
<STRING_BODY><<EOF>> { throw Error(yyextra->literalLocation(), "unterminated string literal"); }

<INITIAL>"(*" { return GeneratedParser::make_ATTR_OPEN(yyextra->token(yyextra->location())); }
<INITIAL>"*)" { return GeneratedParser::make_ATTR_CLOSE(yyextra->token(yyextra->location())); }
<INITIAL>".*" { return GeneratedParser::make_WILDCARD(yyextra->token(yyextra->location())); }
<INITIAL>"+:" { return GeneratedParser::make_PLUS_COLON(yyextra->token(yyextra->location())); }
<INITIAL>"-:" { return GeneratedParser::make_MINUS_COLON(yyextra->token(yyextra->location())); }
<INITIAL>"**" { return GeneratedParser::make_POWER(yyextra->token(yyextra->location())); }
<INITIAL>"<<" { return GeneratedParser::make_LSHIFT(yyextra->token(yyextra->location())); }
<INITIAL>">>" { return GeneratedParser::make_RSHIFT(yyextra->token(yyextra->location())); }
<INITIAL>"<<<" { return GeneratedParser::make_ALSHIFT(yyextra->token(yyextra->location())); }
<INITIAL>">>>" { return GeneratedParser::make_ARSHIFT(yyextra->token(yyextra->location())); }
<INITIAL>"<=" { return GeneratedParser::make_LE(yyextra->token(yyextra->location())); }
<INITIAL>">=" { return GeneratedParser::make_GE(yyextra->token(yyextra->location())); }
<INITIAL>"==" { return GeneratedParser::make_EQ(yyextra->token(yyextra->location())); }
<INITIAL>"!=" { return GeneratedParser::make_NE(yyextra->token(yyextra->location())); }
<INITIAL>"===" { return GeneratedParser::make_CASE_EQ(yyextra->token(yyextra->location())); }
<INITIAL>"!==" { return GeneratedParser::make_CASE_NE(yyextra->token(yyextra->location())); }
<INITIAL>"&&" { return GeneratedParser::make_LAND(yyextra->token(yyextra->location())); }
<INITIAL>"||" { return GeneratedParser::make_LOR(yyextra->token(yyextra->location())); }
<INITIAL>"~&" { return GeneratedParser::make_NAND(yyextra->token(yyextra->location())); }
<INITIAL>"~|" { return GeneratedParser::make_NOR(yyextra->token(yyextra->location())); }
<INITIAL>"~^" { return GeneratedParser::make_XNOR(yyextra->token(yyextra->location())); }
<INITIAL>"^~" { return GeneratedParser::make_XNOR(yyextra->token(yyextra->location())); }
<INITIAL>[()\[\]{};:,.#=+*/%&|^~!<>?-] { return GeneratedParser::symbol_type(static_cast<unsigned char>(yytext[0]), yyextra->token(yyextra->location())); }
<INITIAL>. { throw Error(yyextra->location(), "invalid Verilog character: " + std::string(yytext, yyleng)); }
<INITIAL><<EOF>> { return GeneratedParser::make_END(yyextra->token(yyextra->endLocation())); }
%%
namespace idb::verilog::detail {
void* createScanner(ParseContext& context)
{
  yyscan_t scanner = nullptr;
  if (IdbVeriloglex_init_extra(&context, &scanner) != 0) throw std::bad_alloc();
  return scanner;
}
void destroyScanner(void* scanner) { IdbVeriloglex_destroy(scanner); }
}  // namespace idb::verilog::detail

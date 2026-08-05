%{
// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include <cstdlib>
#include <string>

#include "VerilogScanner.hh"

#undef YY_DECL
#define YY_DECL int idb::verilog::Scanner::lex(idb::verilog::Parser::semantic_type* yylval, idb::verilog::Parser::location_type* yylloc)

#define YY_USER_ACTION yylloc->step(); yylloc->columns(yyleng);

using token = idb::verilog::Parser::token;
%}

%option c++
%option yyclass="idb::verilog::Scanner"
%option prefix="IdbVerilog"
%option noyywrap
%option never-interactive
%option yylineno

%x BLOCK_COMMENT
%x ATTRIBUTE

SIGN            [+-]
UNSIGNED        [0-9][0-9_]*
BLANK           [ \t\r]
EOL             \n
ESC_ID          \\[^ \t\r\n,;()]+
SIMPLE_ID       [A-Za-z_][A-Za-z0-9_$./'-]*

%%

{BLANK}+        {}

{EOL}           {
                  yylloc->lines(1);
                  yylloc->step();
                }

"//"[^\n]*      {}

"/*"            { BEGIN(BLOCK_COMMENT); }
<BLOCK_COMMENT>{
  "*/"          { BEGIN(INITIAL); }
  {EOL}         {
                  yylloc->lines(1);
                  yylloc->step();
                }
  .             {}
  <<EOF>>       {
                  BEGIN(INITIAL);
                  return 0;
                }
}

"(*"            { BEGIN(ATTRIBUTE); }
<ATTRIBUTE>{
  "*)"          { BEGIN(INITIAL); }
  {EOL}         {
                  yylloc->lines(1);
                  yylloc->step();
                }
  .             {}
  <<EOF>>       {
                  BEGIN(INITIAL);
                  return 0;
                }
}

^[ \t]*`[^\n]*  {}

{SIGN}?{UNSIGNED}?"'"[sS]?[bB][01xXzZ?_]+ {
                  yylval->emplace<std::string>(yytext);
                  return token::CONSTANT;
                }

{SIGN}?{UNSIGNED}?"'"[sS]?[oO][0-7xXzZ?_]+ {
                  yylval->emplace<std::string>(yytext);
                  return token::CONSTANT;
                }

{SIGN}?{UNSIGNED}?"'"[sS]?[dD][0-9xXzZ?_]+ {
                  yylval->emplace<std::string>(yytext);
                  return token::CONSTANT;
                }

{SIGN}?{UNSIGNED}?"'"[sS]?[hH][0-9a-fA-FxXzZ?_]+ {
                  yylval->emplace<std::string>(yytext);
                  return token::CONSTANT;
                }

"module"        { return token::MODULE; }
"endmodule"     { return token::ENDMODULE; }
"input"         { return token::INPUT; }
"output"        { return token::OUTPUT; }
"inout"         { return token::INOUT; }
"wire"          { return token::WIRE; }
"supply0"       { return token::SUPPLY0; }
"supply1"       { return token::SUPPLY1; }
"tri"           { return token::TRI; }
"wand"          { return token::WAND; }
"wor"           { return token::WOR; }
"reg"           { return token::REG; }
"assign"        { return token::ASSIGN; }
"signed"        { return token::SIGNED; }

{SIGN}?{UNSIGNED} {
                  yylval->emplace<int>(std::atoi(yytext));
                  return token::INT;
                }

{ESC_ID}        {
                  yylval->emplace<std::string>(yytext);
                  return token::IDENT;
                }

{SIMPLE_ID}     {
                  yylval->emplace<std::string>(yytext);
                  return token::IDENT;
                }

"."|","|":"|";"|"="|"+"|"-"|"*"|"/"|"#"|"("|")"|"["|"]"|"{"|"}" {
                  return static_cast<int>(yytext[0]);
                }

.               {
                  return static_cast<int>(yytext[0]);
                }

%%

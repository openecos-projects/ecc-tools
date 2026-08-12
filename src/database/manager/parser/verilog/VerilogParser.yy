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

%require "3.2"
%skeleton "lalr1.cc"
%define api.namespace {idb::verilog}
%define api.parser.class {Parser}
%define api.value.type variant
%define parse.assert
%locations
%define api.location.file "VerilogLocation.hh"
%parse-param { Scanner* scanner }
%parse-param { ParserContext* context }

%code requires {
#include <string>
#include <vector>

#include "VerilogParserContext.hh"

namespace idb::verilog {
class Scanner;
}
}

%code {
#include "VerilogScanner.hh"

#undef yylex
#define yylex scanner->lex

namespace {
int locLine(const idb::verilog::Parser::location_type& loc)
{
  return loc.begin.line;
}
}
}

%token MODULE ENDMODULE INPUT OUTPUT INOUT WIRE SUPPLY0 SUPPLY1 TRI WAND WOR REG ASSIGN SIGNED
%token <std::string> IDENT CONSTANT
%token <int> INT

%type <std::vector<idb::verilog::CppVerilogID*>> module_ports port_refs
%type <std::vector<std::string>> declaration_names
%type <idb::verilog::ParserRange> range_opt range
%type <DclType> net_dcl_type port_dcl_type
%type <idb::verilog::CppVerilogID*> id_ref
%type <idb::verilog::CppVerilogNetExpr*> net_expr net_atom net_concat assign_rhs assign_lhs port_expr constant_expr
%type <std::vector<idb::verilog::CppVerilogNetExpr*>> net_exprs
%type <idb::verilog::PortConnectionSpec> named_port_connection
%type <std::vector<idb::verilog::PortConnectionSpec>> named_port_connections named_port_connections_opt

%start file

%%

file:
    modules
  ;

modules:
    %empty
  | modules module
  ;

module:
    MODULE IDENT '(' module_ports ')' ';'
    {
      context->beginModule(locLine(@1), $2, std::move($4));
    }
    module_items ENDMODULE
    {
      context->endModule();
    }
  | MODULE IDENT '(' ')' ';'
    {
      context->beginModule(locLine(@1), $2, {});
    }
    module_items ENDMODULE
    {
      context->endModule();
    }
  | MODULE IDENT ';'
    {
      context->beginModule(locLine(@1), $2, {});
    }
    module_items ENDMODULE
    {
      context->endModule();
    }
  ;

module_ports:
    port_refs
    {
      $$ = std::move($1);
    }
  ;

port_refs:
    id_ref
    {
      $$.push_back($1);
    }
  | port_refs ',' id_ref
    {
      $$ = std::move($1);
      $$.push_back($3);
    }
  ;

module_items:
    %empty
  | module_items module_item
  ;

module_item:
    declaration
  | continuous_assign
  | instantiation
  | ';'
  ;

declaration:
    port_dcl_type declaration_modifiers range_opt declaration_names ';'
    {
      context->addDeclaration(locLine(@1), $1, $3, std::move($4));
    }
  | net_dcl_type declaration_modifiers range_opt declaration_names ';'
    {
      context->addDeclaration(locLine(@1), $1, $3, std::move($4));
    }
  ;

port_dcl_type:
    INPUT  { $$ = DclType::KInput; }
  | OUTPUT { $$ = DclType::KOutput; }
  | INOUT  { $$ = DclType::KInout; }
  ;

net_dcl_type:
    WIRE    { $$ = DclType::KWire; }
  | SUPPLY0 { $$ = DclType::KSupply0; }
  | SUPPLY1 { $$ = DclType::KSupply1; }
  | TRI     { $$ = DclType::KTri; }
  | WAND    { $$ = DclType::KWand; }
  | WOR     { $$ = DclType::KWor; }
  ;

declaration_modifiers:
    %empty
  | declaration_modifiers declaration_modifier
  ;

declaration_modifier:
    WIRE
  | REG
  | SIGNED
  ;

range_opt:
    %empty
    {
      $$ = idb::verilog::ParserRange{};
    }
  | range
    {
      $$ = $1;
    }
  ;

range:
    '[' INT ':' INT ']'
    {
      $$ = idb::verilog::ParserRange{true, $2, $4};
    }
  ;

declaration_names:
    id_ref declaration_initializer_opt
    {
      $$.push_back(context->idName($1));
    }
  | declaration_names ',' id_ref declaration_initializer_opt
    {
      $$ = std::move($1);
      $$.push_back(context->idName($3));
    }
  ;

declaration_initializer_opt:
    %empty
  | '=' net_expr
  ;

continuous_assign:
    ASSIGN assign_lhs '=' assign_rhs ';'
    {
      context->addAssign(locLine(@1), $2, $4);
    }
  ;

assign_lhs:
    net_expr
    {
      $$ = $1;
    }
  ;

assign_rhs:
    net_expr
    {
      $$ = context->makeAssignCompatibleExpr(locLine(@1), $1);
    }
  ;

instantiation:
    IDENT parameter_override_opt IDENT '(' named_port_connections_opt ')' ';'
    {
      context->addInstance(locLine(@1), $1, $3, std::move($5));
    }
  ;

parameter_override_opt:
    %empty
  | '#' balanced_parens
  ;

balanced_parens:
    '(' balanced_items ')'
  ;

balanced_braces:
    '{' balanced_items '}'
  ;

balanced_brackets:
    '[' balanced_items ']'
  ;

balanced_items:
    %empty
  | balanced_items balanced_item
  ;

balanced_item:
    IDENT
  | CONSTANT
  | INT
  | MODULE
  | ENDMODULE
  | INPUT
  | OUTPUT
  | INOUT
  | WIRE
  | SUPPLY0
  | SUPPLY1
  | TRI
  | WAND
  | WOR
  | REG
  | ASSIGN
  | SIGNED
  | '.'
  | ','
  | ':'
  | ';'
  | '='
  | '+'
  | '-'
  | '*'
  | '/'
  | '#'
  | balanced_parens
  | balanced_braces
  | balanced_brackets
  ;

named_port_connections_opt:
    %empty
    {
      $$ = {};
    }
  | named_port_connections
    {
      $$ = std::move($1);
    }
  ;

named_port_connections:
    named_port_connection
    {
      $$.push_back($1);
    }
  | named_port_connections ',' named_port_connection
    {
      $$ = std::move($1);
      $$.push_back($3);
    }
  ;

named_port_connection:
    '.' id_ref '(' port_expr ')'
    {
      $$ = idb::verilog::PortConnectionSpec{$2, $4};
    }
  | '.' id_ref '(' ')'
    {
      $$ = idb::verilog::PortConnectionSpec{$2, nullptr};
    }
  ;

port_expr:
    net_expr
    {
      $$ = $1;
    }
  ;

net_expr:
    net_atom
    {
      $$ = $1;
    }
  | net_concat
    {
      $$ = $1;
    }
  ;

net_concat:
    '{' net_exprs '}'
    {
      $$ = context->makeConcatExpr(locLine(@1), std::move($2));
    }
  ;

net_exprs:
    net_expr
    {
      $$.push_back($1);
    }
  | net_exprs ',' net_expr
    {
      $$ = std::move($1);
      $$.push_back($3);
    }
  ;

net_atom:
    id_ref
    {
      $$ = context->makeIdExpr(locLine(@1), $1);
    }
  | constant_expr
    {
      $$ = $1;
    }
  ;

constant_expr:
    CONSTANT
    {
      $$ = context->makeConstantExpr(locLine(@1), $1);
    }
  | INT
    {
      $$ = context->makeConstantExpr(locLine(@1), std::to_string($1));
    }
  ;

id_ref:
    IDENT
    {
      $$ = context->makeIdFromName($1);
    }
  | IDENT '[' INT ']'
    {
      $$ = context->makeIndexId($1, $3);
    }
  | IDENT '[' INT ':' INT ']'
    {
      $$ = context->makeSliceId($1, $3, $5);
    }
  ;

%%

void idb::verilog::Parser::error(const location_type& loc, const std::string& message)
{
  context->setError(loc.begin.line, message);
}

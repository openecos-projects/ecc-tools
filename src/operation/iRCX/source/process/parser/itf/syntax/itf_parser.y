%skeleton "lalr1.cc"
%require "3.7"
%define api.namespace {ircx::itf::syntax::detail}
%define api.parser.class {Parser}
%define api.value.type variant
%define api.token.constructor
%define api.location.type {ircx::itf::syntax::SourceRange}
%define parse.error detailed
%define parse.lac full
%locations
%expect 0

%code requires {
  #include "ItfDriver.hpp"
  typedef void* yyscan_t;
}

%code {
  ircx::itf::syntax::detail::Parser::symbol_type ircx_itf_lex(yyscan_t scanner);
  #define yylex ircx_itf_lex
}

%parse-param {Driver& driver} {yyscan_t scanner}
%lex-param {yyscan_t scanner}
%initial-action { @$.begin = driver.cursor; @$.end = driver.cursor; }

%token END 0 "end of file"
%token <std::string> WORD "field or identifier"
%token <std::string> STRING "quoted name"
%token <std::string> NUMBER "number"
%token <std::string> NAMED "named block"
%token <std::string> TECHNOLOGY "TECHNOLOGY"
%token <std::string> TABLE "effect table"
%token <std::string> MODIFIER "effect modifier"
%token LBRACE "{" RBRACE "}" LPAREN "(" RPAREN ")" EQUAL "=" COMMA ","

%type <std::vector<Node>> statements content table_content block table_block modifiers numeric_items
%type <Node> statement item scalar number numeric_group parenthesized
%type <std::string> name reference

%start document

%%

document:
  statements
    { driver.document.statements = std::move($1); }
;

statements:
  %empty
    { $$ = {}; }
| statements statement
    { $$ = std::move($1); $$.push_back(std::move($2)); }
;

statement:
  NAMED name block
    {
      $$ = make_node(NodeKind::kField, std::move($1), @$, std::move($3));
      $$.name = std::move($2);
    }
| TECHNOLOGY optional_equal name
    {
      $$ = make_node(NodeKind::kField, std::move($1), @$);
      $$.children.push_back(make_node(NodeKind::kReference, std::move($3), @3));
    }
| WORD EQUAL scalar
    {
      $$ = make_node(NodeKind::kField, std::move($1), @$);
      $$.children.push_back(std::move($3));
    }
| WORD optional_equal block
    { $$ = make_node(NodeKind::kField, std::move($1), @$, std::move($3)); }
| WORD optional_equal parenthesized
    {
      $$ = make_node(NodeKind::kField, std::move($1), @$);
      $$.children.push_back(std::move($3));
    }
| WORD modifiers block
    { $$ = make_node(NodeKind::kField, std::move($1), @$, append(std::move($2), std::move($3))); }
| TABLE optional_equal table_block
    { $$ = make_node(NodeKind::kField, std::move($1), @$, std::move($3)); }
| TABLE modifiers table_block
    { $$ = make_node(NodeKind::kField, std::move($1), @$, append(std::move($2), std::move($3))); }
;

optional_equal:
  %empty
| EQUAL
;

block:
  LBRACE content RBRACE
    { $$ = std::move($2); }
;

// Conductor tables accept in-block modifiers. The
// prefix form remains accepted for existing inputs and via etch tables.
table_block:
  LBRACE table_content RBRACE
    { $$ = std::move($2); }
;

modifiers:
  MODIFIER
    { $$.push_back(make_node(NodeKind::kModifier, std::move($1), @1)); }
| modifiers MODIFIER
    { $$ = std::move($1); $$.push_back(make_node(NodeKind::kModifier, std::move($2), @2)); }
;

content:
  %empty
    { $$ = {}; }
| content item
    { $$ = std::move($1); $$.push_back(std::move($2)); }
| content COMMA
    { $$ = std::move($1); }
;

table_content:
  %empty
    { $$ = {}; }
| table_content item
    { $$ = std::move($1); $$.push_back(std::move($2)); }
| table_content MODIFIER
    { $$ = std::move($1); $$.push_back(make_node(NodeKind::kModifier, std::move($2), @2)); }
| table_content COMMA
    { $$ = std::move($1); }
;

item:
  statement     { $$ = std::move($1); }
| number        { $$ = std::move($1); }
| numeric_group { $$ = std::move($1); }
;

// Numeric groups cannot contain property declarations. Preserve grouping in
// the syntax tree; the adapter flattens values in their original order.
numeric_group:
  parenthesized
    { $$ = std::move($1); }
| LBRACE numeric_items RBRACE
    { $$ = make_node(NodeKind::kList, {}, @$, std::move($2)); }
;

parenthesized:
  LPAREN numeric_items RPAREN
    { $$ = make_node(NodeKind::kList, {}, @$, std::move($2)); }
;

numeric_items:
  %empty
    { $$ = {}; }
| numeric_items number
    { $$ = std::move($1); $$.push_back(std::move($2)); }
| numeric_items numeric_group
    { $$ = std::move($1); $$.push_back(std::move($2)); }
| numeric_items COMMA
    { $$ = std::move($1); }
;

scalar:
  number
    { $$ = std::move($1); }
| reference
    { $$ = make_node(NodeKind::kReference, std::move($1), @$); }
;

number:
  NUMBER
    {
      auto parsed = driver.number(std::move($1), @1);
      if (!parsed)
        YYABORT;
      $$ = std::move(*parsed);
    }
;

// Reserved words are valid as names/references; keep their original spelling.
name:
  reference
    { $$ = std::move($1); }
| NUMBER
    { $$ = std::move($1); }
;

reference:
  WORD       { $$ = std::move($1); }
| STRING     { $$ = std::move($1); }
| NAMED      { $$ = std::move($1); }
| TECHNOLOGY { $$ = std::move($1); }
| TABLE      { $$ = std::move($1); }
| MODIFIER   { $$ = std::move($1); }
;

%%

void ircx::itf::syntax::detail::Parser::error(const location_type& range, const std::string& message)
{
  driver.fail(range, message);
}

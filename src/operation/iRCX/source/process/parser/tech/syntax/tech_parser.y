%skeleton "lalr1.cc"
%require "3.7"
%define api.namespace {ircx::tech::syntax::detail}
%define api.parser.class {Parser}
%define api.value.type variant
%define api.token.constructor
%define api.location.type {ircx::tech::syntax::SourceRange}
%define parse.error detailed
%define parse.lac full
%locations
%expect 0

%code requires {
  #include "TechDriver.hpp"
  typedef void* yyscan_t;
}
%code {
  ircx::tech::syntax::detail::Parser::symbol_type ircx_tech_lex(yyscan_t scanner);
  #define yylex ircx_tech_lex
}
%parse-param {Driver& driver} {yyscan_t scanner}
%lex-param {yyscan_t scanner}

%token END 0 "end of file"
%token LBRACE "{" RBRACE "}" LPAREN "(" RPAREN ")" COMMA ","
%token PLUS "+" MINUS "-" STAR "*" SLASH "/" CARET "^"
%token <ircx::tech::syntax::Node> NUMBER "number" IDENT "identifier"
%token <ircx::tech::syntax::Node> BLOCK "block keyword" NAMED "layer keyword" MODEL "VIAMODEL"
%token <ircx::tech::syntax::Node> SCALAR "scalar keyword" FLEXIBLE "scalar/table keyword"
%token <ircx::tech::syntax::Node> REFERENCE "reference keyword" LOWER "LOWERLAYER"
%token <ircx::tech::syntax::Node> DENSITY "CAP_DENSITY" TABLE "table keyword" FLAG "option"
%token <ircx::tech::syntax::Node> POLYNOMIAL "polynomial keyword" IMAX "IMAX"

%type <ircx::tech::syntax::Node> property number name keyword group rectangle polynomial_row polynomial_start
%type <std::vector<ircx::tech::syntax::Node>> properties content modifiers extra_names rectangles
%type <std::vector<ircx::tech::syntax::Node>> polynomial_body polynomial_rows
%type <ircx::tech::syntax::detail::Expression> expression
%type <std::vector<ircx::tech::syntax::detail::Expression>> arguments

%left PLUS MINUS
%left STAR SLASH
%precedence UNARY
%right CARET

%%
document:
  properties { driver.document.statements = std::move($1); }
;

properties:
  %empty { $$ = {}; }
| properties property { $$ = std::move($1); $$.push_back(std::move($2)); }
;

property:
  NAMED name LBRACE content RBRACE {
    $4.insert($4.begin(), std::move($2));
    $$ = finish(std::move($1), std::move($4), @$, true);
  }
| BLOCK LBRACE content RBRACE { $$ = finish(std::move($1), std::move($3), @$, true); }
| MODEL name LBRACE rectangles RBRACE {
    $4.insert($4.begin(), std::move($2));
    $$ = finish(std::move($1), std::move($4), @$, true);
  }
| SCALAR number { $$ = finish(std::move($1), {std::move($2)}, @$); }
| FLEXIBLE number { $$ = finish(std::move($1), {std::move($2)}, @$); }
| FLEXIBLE LBRACE content RBRACE { $$ = finish(std::move($1), std::move($3), @$, true); }
| REFERENCE name { $$ = finish(std::move($1), {std::move($2)}, @$); }
| LOWER name extra_names {
    $3.insert($3.begin(), std::move($2));
    $$ = finish(std::move($1), std::move($3), @$);
  }
| DENSITY number name { $$ = finish(std::move($1), {std::move($2), std::move($3)}, @$); }
| TABLE modifiers LBRACE content RBRACE {
    for (auto& node : $4) $2.push_back(std::move(node));
    $$ = finish(std::move($1), std::move($2), @$, true);
  }
| polynomial_start LBRACE polynomial_body RBRACE {
    $$ = finish(std::move($1), std::move($3), @$, true);
  }
| FLAG { $$ = std::move($1); }
;

polynomial_start: POLYNOMIAL {
  driver.pending_polynomial = true;
  driver.expression_nodes = 0;
  $$ = std::move($1);
};

content:
  %empty { $$ = {}; }
| content property { $$ = std::move($1); $$.push_back(std::move($2)); }
| content number { $$ = std::move($1); $$.push_back(std::move($2)); }
| content group { $$ = std::move($1); $$.push_back(std::move($2)); }
| content COMMA {
    $$ = std::move($1);
    $$.push_back(container(ircx::tech::syntax::NodeKind::separator, {}, @2, ","));
  }
;

group: LBRACE content RBRACE { $$ = container(ircx::tech::syntax::NodeKind::list, std::move($2), @$); };
modifiers:
  %empty { $$ = {}; }
| modifiers FLAG { $$ = std::move($1); $2.qualifier = true; $$.push_back(std::move($2)); }
;
extra_names:
  %empty { $$ = {}; }
| extra_names IDENT { $$ = std::move($1); $$.push_back(std::move($2)); }
;

number:
  NUMBER { $$ = std::move($1); }
| PLUS NUMBER { $$ = signed_number(std::move($2), '+', @$); }
| MINUS NUMBER { $$ = signed_number(std::move($2), '-', @$); }
;

// Keywords can also be layer/model names in syntactically named positions.
name:
  IDENT { $$ = std::move($1); }
| keyword { $$ = std::move($1); $$.kind = ircx::tech::syntax::NodeKind::identifier; $$.keyword = ircx::tech::syntax::Keyword::unknown; }
;
keyword:
  BLOCK | NAMED | MODEL | SCALAR | FLEXIBLE | REFERENCE | LOWER | DENSITY
| TABLE | FLAG | POLYNOMIAL | IMAX
;

rectangles:
  %empty { $$ = {}; }
| rectangles rectangle { $$ = std::move($1); $$.push_back(std::move($2)); }
;
rectangle: name number number number number {
  $$ = container(ircx::tech::syntax::NodeKind::rectangle,
                 {std::move($1), std::move($2), std::move($3), std::move($4), std::move($5)}, @$);
};

polynomial_body:
  polynomial_row { $$.push_back(std::move($1)); }
| polynomial_rows { $$ = std::move($1); }
;
polynomial_rows:
  LBRACE polynomial_row RBRACE { $2.range = @$; $$.push_back(std::move($2)); }
| polynomial_rows LBRACE polynomial_row RBRACE {
    $$ = std::move($1); $3.range = {@2.begin, @4.end}; $$.push_back(std::move($3));
  }
;
polynomial_row:
  expression { $$ = container(ircx::tech::syntax::NodeKind::polynomial, {std::move($1.node)}, @$); }
| expression IMAX expression {
    auto maximum = finish(std::move($2), {std::move($3.node)}, {@2.begin, @3.end});
    $$ = container(ircx::tech::syntax::NodeKind::polynomial, {std::move($1.node), std::move(maximum)}, @$);
  }
;

expression:
  NUMBER { $$ = driver.expression(std::move($1)); if (!driver.diagnostics.empty()) YYABORT; }
| IDENT { $$ = driver.expression(std::move($1)); if (!driver.diagnostics.empty()) YYABORT; }
| IDENT LPAREN arguments RPAREN {
    $$ = driver.expression(container(ircx::tech::syntax::NodeKind::call_expression, {}, @$, std::move($1.text)), std::move($3)); if (!driver.diagnostics.empty()) YYABORT;
  }
| LPAREN expression RPAREN { $$ = std::move($2); $$.node.range = @$; }
| PLUS expression %prec UNARY {
    $$ = driver.expression(container(ircx::tech::syntax::NodeKind::unary_expression, {}, @$, "+"), {std::move($2)}); if (!driver.diagnostics.empty()) YYABORT;
  }
| MINUS expression %prec UNARY {
    $$ = driver.expression(container(ircx::tech::syntax::NodeKind::unary_expression, {}, @$, "-"), {std::move($2)}); if (!driver.diagnostics.empty()) YYABORT;
  }
| expression PLUS expression {
    $$ = driver.expression(container(ircx::tech::syntax::NodeKind::binary_expression, {}, @$, "+"), {std::move($1), std::move($3)}); if (!driver.diagnostics.empty()) YYABORT;
  }
| expression MINUS expression {
    $$ = driver.expression(container(ircx::tech::syntax::NodeKind::binary_expression, {}, @$, "-"), {std::move($1), std::move($3)}); if (!driver.diagnostics.empty()) YYABORT;
  }
| expression STAR expression {
    $$ = driver.expression(container(ircx::tech::syntax::NodeKind::binary_expression, {}, @$, "*"), {std::move($1), std::move($3)}); if (!driver.diagnostics.empty()) YYABORT;
  }
| expression SLASH expression {
    $$ = driver.expression(container(ircx::tech::syntax::NodeKind::binary_expression, {}, @$, "/"), {std::move($1), std::move($3)}); if (!driver.diagnostics.empty()) YYABORT;
  }
| expression CARET expression {
    $$ = driver.expression(container(ircx::tech::syntax::NodeKind::binary_expression, {}, @$, "^"), {std::move($1), std::move($3)}); if (!driver.diagnostics.empty()) YYABORT;
  }
;
arguments:
  expression { $$.push_back(std::move($1)); }
| arguments COMMA expression { $$ = std::move($1); $$.push_back(std::move($3)); }
;
%%

void ircx::tech::syntax::detail::Parser::error(const location_type& range, const std::string& message) {
  driver.fail(range, message);
}

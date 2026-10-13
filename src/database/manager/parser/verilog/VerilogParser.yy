// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
%require "3.2"
%skeleton "lalr1.cc"
%define api.namespace {idb::verilog::grammar}
%define api.parser.class {Parser}
%define api.value.type variant
%define api.token.constructor
%define parse.error verbose
%define api.location.type {idb::verilog::SourceLocation}
%locations
%expect 0
%parse-param {idb::verilog::detail::ParseContext& driver}
%lex-param {idb::verilog::detail::ParseContext& driver}
%code requires {
#include "VerilogParseContext.hh"
#define YYLLOC_DEFAULT(Current, Rhs, N) do { (Current) = YYRHSLOC(Rhs, (N) ? 1 : 0); } while (false)
}
%code {
namespace idb::verilog::detail { grammar::Parser::symbol_type scanToken(void* scanner); }
static idb::verilog::grammar::Parser::symbol_type yylex(idb::verilog::detail::ParseContext& driver)
{ return idb::verilog::detail::scanToken(driver.scanner()); }
}
%token END 0 "end of input"
%token <std::string> IDENTIFIER "identifier" NUMBER "number" FILL "fill literal" STRING "string" SYSTEM_FUNCTION "system function" RESERVED "reserved keyword"
%token MODULE "module" ENDMODULE "endmodule" INPUT "input" OUTPUT "output" INOUT "inout"
%token WIRE "wire" TRI "tri" WAND "wand" WOR "wor" SUPPLY0 "supply0" SUPPLY1 "supply1"
%token SIGNED "signed" UNSIGNED "unsigned" LOGIC "logic" VAR "var" INTEGER "integer" INT "int"
%token PARAMETER "parameter" LOCALPARAM "localparam" ASSIGN "assign" DEFPARAM "defparam" GENVAR "genvar"
%token GENERATE "generate" ENDGENERATE "endgenerate" BEGIN_BLOCK "begin" END_BLOCK "end"
%token IF "if" ELSE "else" FOR "for" CASE "case" ENDCASE "endcase" DEFAULT "default"
%token DEFAULT_NETTYPE "`default_nettype" RESETALL "`resetall"
%token ATTR_OPEN "(*" ATTR_CLOSE "*)" WILDCARD ".*" PLUS_COLON "+:" MINUS_COLON "-:"
%token POWER "**" LSHIFT "<<" RSHIFT ">>" ALSHIFT "<<<" ARSHIFT ">>>"
%token LE "<=" GE ">=" EQ "==" NE "!=" CASE_EQ "===" CASE_NE "!=="
%token LAND "&&" LOR "||" NAND "~&" NOR "~|" XNOR "~^"

%precedence IF_WITHOUT_ELSE
%precedence ELSE
%right '?'
%left LOR
%left LAND
%left '|'
%left '^' XNOR
%left '&'
%left EQ NE CASE_EQ CASE_NE
%left '<' LE '>' GE
%left LSHIFT RSHIFT ALSHIFT ARSHIFT
%left '+' '-'
%left '*' '/' '%'
// IEEE 1364-2005 Table 5-4: power, like other binary operators, is left associative.
%left POWER
%precedence UNARY

%type <std::vector<idb::verilog::Attribute>> attributes nonempty_attributes attribute_group attribute_list
%type <idb::verilog::Attribute> attribute
%type <idb::verilog::Declaration> declaration_spec port_spec data_spec typed_data_spec declaration_type
%type <idb::verilog::Direction> direction
%type <idb::verilog::NetType> net_type
%type <std::optional<bool>> signing optional_signing
%type <bool> optional_logic parameter_kind nonblock_item
%type <idb::verilog::DeclaredDataType> parameter_type
%type <idb::verilog::Parameter> parameter_spec
%type <idb::verilog::Range> range optional_range
%type <idb::verilog::ExprId> expression primary reference optional_expression initializer group_open
%type <std::vector<idb::verilog::ExprId>> expression_list
%type <std::string> unary_operator unary_prefix optional_label
%type <std::vector<idb::verilog::Connection>> connections named_connections positional_connections parameter_actuals
%type <idb::verilog::Connection> named_connection
%type <std::vector<idb::verilog::PathComponent>> parameter_path
%type <idb::verilog::PathComponent> path_component
%type <std::unique_ptr<idb::verilog::Generate>> if_start for_start case_start block_start if_generate for_generate case_generate block_generate
%type <std::unique_ptr<idb::verilog::GenerateBlock>> named_block generate_body
%type <std::vector<idb::verilog::GenerateBranch>> case_branches
%type <idb::verilog::GenerateBranch> case_branch

%%
compilation_unit:
    %empty
  | compilation_unit module_declaration
  | compilation_unit directive
  ;
module_start:
    attributes MODULE IDENTIFIER { driver.beginModule(std::move($3), std::move($1), @2); }
  ;
module_declaration:
    module_start parameter_header module_ports ';' module_items ENDMODULE { driver.endModule(); }
  ;
parameter_header:
    %empty
  | '#' '(' { driver.beginParameterHeader(); } parameter_declaration ')' { driver.endParameterHeader(); }
  ;
module_ports:
    %empty
  | '(' ')'
  | '(' nonansi_ports ')'
  | '(' ansi_ports ')'
  ;
nonansi_ports:
    IDENTIFIER { driver.port(std::move($1), @1, false); }
  | nonansi_ports ',' IDENTIFIER { driver.port(std::move($3), @3, false); }
  ;
ansi_ports:
    ansi_port
  | ansi_ports ',' ansi_port
  | ansi_ports ',' IDENTIFIER { driver.port(std::move($3), @3, true); }
  ;
ansi_port:
    port_spec { driver.portSpec(std::move($1), @1); } IDENTIFIER { driver.port(std::move($3), @3, true); }
  ;
port_spec:
    direction data_spec { $$ = std::move($2); $$.direction = $1; }
  | typed_data_spec { $$ = std::move($1); }
  ;
module_items:
    %empty
  | module_items module_item
  ;
module_item:
    attributes nonblock_item
  | attributes block_generate { driver.emitGenerate(std::move($2)); }
  ;
nonblock_item:
    ';' { $$ = false; }
  | declaration_spec { driver.declarationSpec(std::move($1)); } declaration_names ';' { $$ = false; }
  | parameter_declaration ';' { $$ = false; }
  | ASSIGN assignments ';' { $$ = false; }
  | instance_header instances ';' { $$ = false; }
  | GENVAR genvar_names ';' { $$ = false; }
  | DEFPARAM parameter_overrides ';' { $$ = false; }
  | GENERATE { driver.beginRegion(@1); } module_items ENDGENERATE { driver.endRegion(); $$ = false; }
  | if_generate { driver.emitGenerate(std::move($1)); $$ = true; }
  | case_generate { driver.emitGenerate(std::move($1)); $$ = true; }
  | for_generate { driver.emitGenerate(std::move($1)); $$ = false; }
  ;
directive:
    DEFAULT_NETTYPE WIRE { driver.setImplicit(true); }
  | DEFAULT_NETTYPE IDENTIFIER {
      if ($2 != "none") throw idb::verilog::detail::Error(@2, "only default_nettype wire/none are supported");
      driver.setImplicit(false);
    }
  | RESETALL { driver.setImplicit(true); }
  ;

direction: INPUT { $$ = idb::verilog::Direction::input; }
  | OUTPUT { $$ = idb::verilog::Direction::output; }
  | INOUT { $$ = idb::verilog::Direction::inout; }
  ;
net_type: WIRE { $$ = idb::verilog::NetType::wire; }
  | TRI { $$ = idb::verilog::NetType::tri; }
  | WAND { $$ = idb::verilog::NetType::wand; }
  | WOR { $$ = idb::verilog::NetType::wor; }
  | SUPPLY0 { $$ = idb::verilog::NetType::supply0; }
  | SUPPLY1 { $$ = idb::verilog::NetType::supply1; }
  ;
signing: SIGNED { $$ = true; } | UNSIGNED { $$ = false; };
optional_signing: %empty { $$ = {}; } | signing { $$ = $1; };
optional_logic: %empty { $$ = false; } | LOGIC { $$ = true; };
declaration_type:
    net_type optional_logic {
      $$ = {}; $$.type = $1; $$.object_kind = idb::verilog::DeclaredObjectKind::net;
      if ($2) $$.data_type = idb::verilog::DeclaredDataType::logic;
    }
  | VAR optional_logic {
      $$ = {}; $$.object_kind = idb::verilog::DeclaredObjectKind::variable;
      if ($2) $$.data_type = idb::verilog::DeclaredDataType::logic;
    }
  | LOGIC { $$ = {}; $$.data_type = idb::verilog::DeclaredDataType::logic; }
  ;
typed_data_spec:
    declaration_type optional_signing optional_range { $$ = std::move($1); $$.is_signed = $2.value_or(false); $$.range = $3; }
  | signing optional_range { $$ = {}; $$.is_signed = *$1; $$.range = $2; }
  | range { $$ = {}; $$.range = $1; }
  ;
data_spec: %empty { $$ = {}; } | typed_data_spec { $$ = std::move($1); };
declaration_spec:
    direction data_spec { $$ = std::move($2); $$.direction = $1; }
  | declaration_type optional_signing optional_range { $$ = std::move($1); $$.is_signed = $2.value_or(false); $$.range = $3; }
  ;
declaration_names:
    IDENTIFIER initializer { driver.declaration(std::move($1), $2, @1); }
  | declaration_names ',' IDENTIFIER initializer { driver.declaration(std::move($3), $4, @3); }
  ;
initializer: %empty { $$ = idb::verilog::noExpr; } | '=' expression { $$ = $2; };
range: '[' expression ':' expression ']' { $$ = {$2, $4}; };
optional_range: %empty { $$ = {}; } | range { $$ = $1; };

parameter_kind: PARAMETER { $$ = false; } | LOCALPARAM { $$ = true; };
parameter_type:
    %empty { $$ = idb::verilog::DeclaredDataType::implicit; }
  | INTEGER { $$ = idb::verilog::DeclaredDataType::integer; }
  | INT { $$ = idb::verilog::DeclaredDataType::intType; }
  | LOGIC { $$ = idb::verilog::DeclaredDataType::logic; }
  ;
parameter_spec:
    parameter_kind parameter_type optional_signing optional_range {
      if ($4.present() && ($2 == idb::verilog::DeclaredDataType::integer || $2 == idb::verilog::DeclaredDataType::intType))
        throw idb::verilog::detail::Error(@4, "expected identifier after integer parameter type");
      $$ = {}; $$.location = @1; $$.local = $1; $$.data_type = $2; $$.signing = $3; $$.range = $4;
    }
  ;
parameter_declaration:
    parameter_spec { driver.parameterSpec(std::move($1)); } parameter_assignment
  | parameter_declaration ',' parameter_assignment
  | parameter_declaration ',' parameter_spec { driver.parameterSpec(std::move($3)); } parameter_assignment
  ;
parameter_assignment: IDENTIFIER '=' expression { driver.parameter(std::move($1), $3, @1); };
assignments:
    expression '=' expression { driver.scope().statements.emplace_back(idb::verilog::Assignment{@1, $1, $3}); }
  | assignments ',' expression '=' expression { driver.scope().statements.emplace_back(idb::verilog::Assignment{@3, $3, $5}); }
  ;
instance_header:
    IDENTIFIER parameter_actuals { driver.instanceType(std::move($1), std::move($2)); }
  ;
parameter_actuals: %empty { $$ = {}; } | '#' '(' connections ')' { $$ = std::move($3); };
instances:
    IDENTIFIER optional_range '(' connections ')' { driver.instance(std::move($1), $2, std::move($4), @1); }
  | instances ',' IDENTIFIER optional_range '(' connections ')' { driver.instance(std::move($3), $4, std::move($6), @3); }
  ;
connections:
    %empty { $$ = {}; }
  | named_connections { $$ = std::move($1); }
  | positional_connections { $$ = std::move($1); }
  ;
named_connections:
    named_connection { $$.push_back(std::move($1)); }
  | named_connections ',' named_connection { $$ = std::move($1); $$.push_back(std::move($3)); }
  ;
named_connection:
    '.' IDENTIFIER '(' optional_expression ')' { $$ = {@1, std::move($2), $4}; }
  | '.' IDENTIFIER { $$ = {@1, std::move($2), idb::verilog::noExpr, idb::verilog::ConnectionKind::implicit}; }
  | WILDCARD { $$ = {@1, {}, idb::verilog::noExpr, idb::verilog::ConnectionKind::wildcard}; }
  ;
positional_connections:
    expression { $$.push_back({@1, {}, $1}); }
  | ',' optional_expression { $$.push_back({@1, {}, idb::verilog::noExpr}); $$.push_back({@2, {}, $2}); }
  | positional_connections ',' optional_expression { $$ = std::move($1); $$.push_back({@3, {}, $3}); }
  ;
optional_expression: %empty { $$ = idb::verilog::noExpr; } | expression { $$ = $1; };
genvar_names:
    IDENTIFIER { driver.scope().genvars.push_back(std::move($1)); }
  | genvar_names ',' IDENTIFIER { driver.scope().genvars.push_back(std::move($3)); }
  ;
parameter_overrides:
    parameter_path '=' expression { driver.scope().overrides.push_back({@1, std::move($1), $3}); }
  | parameter_overrides ',' parameter_path '=' expression { driver.scope().overrides.push_back({@3, std::move($3), $5}); }
  ;
parameter_path:
    path_component { $$.push_back(std::move($1)); }
  | parameter_path '.' path_component { $$ = std::move($1); $$.push_back(std::move($3)); }
  ;
path_component:
    IDENTIFIER { $$ = {std::move($1)}; }
  | IDENTIFIER '[' expression ']' { $$ = {std::move($1), $3}; }
  ;

if_start: IF { $$ = driver.beginGenerate(idb::verilog::GenerateKind::conditional, @1); };
if_generate:
    if_start '(' expression ')' generate_body %prec IF_WITHOUT_ELSE {
      $$ = std::move($1); $$.get()->condition = $3; $$.get()->branches.push_back({{}, std::move($5)});
    }
  | if_start '(' expression ')' generate_body ELSE generate_body {
      $$ = std::move($1); $$.get()->condition = $3;
      $$.get()->branches.push_back({{}, std::move($5)}); $$.get()->branches.push_back({{}, std::move($7)});
    }
  ;
for_start: FOR { $$ = driver.beginGenerate(idb::verilog::GenerateKind::loop, @1); };
for_generate:
    for_start '(' IDENTIFIER '=' expression ';' expression ';' IDENTIFIER '=' expression ')' generate_body {
      if ($3 != $9) throw idb::verilog::detail::Error(@9, "generate step must assign the same genvar");
      $$ = std::move($1); $$.get()->variable = std::move($3); $$.get()->initial = $5; $$.get()->condition = $7; $$.get()->step = $11;
      $13->direct_conditional = false; $$.get()->branches.push_back({{}, std::move($13)});
    }
  ;
case_start: CASE { $$ = driver.beginGenerate(idb::verilog::GenerateKind::selection, @1); };
case_generate:
    case_start '(' expression ')' case_branches ENDCASE {
      bool default_seen = false;
      for (const auto& branch : $5) if (branch.matches.empty()) {
        if (default_seen) throw idb::verilog::detail::Error(@1, "duplicate generate case default");
        default_seen = true;
      }
      $$ = std::move($1); $$.get()->condition = $3; $$.get()->branches = std::move($5);
    }
  ;
case_branches:
    %empty { $$.clear(); }
  | case_branches case_branch { $$ = std::move($1); $$.push_back(std::move($2)); }
  ;
case_branch:
    expression_list ':' generate_body { $$ = {std::move($1), std::move($3)}; }
  | DEFAULT ':' generate_body { $$ = {{}, std::move($3)}; }
  | DEFAULT generate_body { $$ = {{}, std::move($2)}; }
  ;
block_start: %empty { $$ = driver.beginGenerate(idb::verilog::GenerateKind::block, driver.lastTokenLocation()); };
block_generate:
    block_start named_block { $$ = std::move($1); $$.get()->branches.push_back({{}, std::move($2)}); }
  ;
block_open: BEGIN_BLOCK optional_label { driver.beginBlock(std::move($2), @1); };
optional_label: %empty { $$ = {}; } | ':' IDENTIFIER { $$ = std::move($2); };
named_block: block_open module_items END_BLOCK { $$ = driver.endBlock(); };
anonymous_scope: %empty { driver.beginBlock({}, driver.lastTokenLocation()); };
generate_body:
    named_block { $$ = std::move($1); }
  | anonymous_scope nonblock_item { $$ = driver.endBlock($2); }
  | anonymous_scope nonempty_attributes nonblock_item { $$ = driver.endBlock(); }
  | anonymous_scope nonempty_attributes block_generate { driver.emitGenerate(std::move($3)); $$ = driver.endBlock(); }
  ;

attributes: %empty { $$ = {}; } | nonempty_attributes { $$ = std::move($1); };
nonempty_attributes:
    attribute_group { $$ = std::move($1); }
  | nonempty_attributes attribute_group { $$ = std::move($1); for (auto& attribute : $2) $$.push_back(std::move(attribute)); }
  ;
attribute_group: ATTR_OPEN attribute_list ATTR_CLOSE { $$ = std::move($2); };
attribute_list:
    attribute { $$.push_back(std::move($1)); }
  | attribute_list ',' attribute { $$ = std::move($1); $$.push_back(std::move($3)); }
  ;
attribute:
    IDENTIFIER { $$ = {@1, std::move($1)}; }
  | IDENTIFIER '=' expression { $$ = {@1, std::move($1), $3}; }
  | IDENTIFIER '=' STRING { $$ = {@1, std::move($1), idb::verilog::noExpr, std::move($3)}; }
  ;

reference: IDENTIFIER { $$ = driver.add(idb::verilog::ExpressionKind::reference, @1, std::move($1)); };
// Bound deferred shifts as well as completed AST depth. An iterative parser
// otherwise stacks an entire unary/conditional/concatenation chain before reducing it.
group_open: '(' { driver.openExpression(@1); $$ = idb::verilog::noExpr; };
concat_open: '{' { driver.openExpression(@1); };
select_open: '[' { driver.openExpression(@1); };
conditional_open: '?' { driver.openExpression(@1); };
primary:
    reference { $$ = $1; }
  | NUMBER { $$ = driver.add(idb::verilog::ExpressionKind::number, @1, std::move($1)); }
  | FILL { $$ = driver.add(idb::verilog::ExpressionKind::fill, @1, std::move($1)); }
  | reference select_open expression ']' { driver.closeExpression(); $$ = driver.add(idb::verilog::ExpressionKind::select, @1, {}, {$1, $3}); }
  | reference select_open expression ':' expression ']' { driver.closeExpression(); $$ = driver.add(idb::verilog::ExpressionKind::slice, @1, {}, {$1, $3, $5}); }
  | reference select_open expression PLUS_COLON expression ']' { driver.closeExpression(); $$ = driver.add(idb::verilog::ExpressionKind::indexedUp, @1, {}, {$1, $3, $5}); }
  | reference select_open expression MINUS_COLON expression ']' { driver.closeExpression(); $$ = driver.add(idb::verilog::ExpressionKind::indexedDown, @1, {}, {$1, $3, $5}); }
  | SYSTEM_FUNCTION group_open expression ')' {
      driver.closeExpression();
      if ($1 != "$clog2" && $1 != "$signed" && $1 != "$unsigned")
        throw idb::verilog::detail::Error(@1, "unsupported constant system function: " + $1);
      $$ = driver.add(idb::verilog::ExpressionKind::call, @1, std::move($1), {$3});
    }
  | group_open expression ')' { driver.closeExpression(); $$ = $2; }
  | concat_open expression_list '}' { driver.closeExpression(); $$ = driver.add(idb::verilog::ExpressionKind::concatenate, @1, {}, std::move($2)); }
  | concat_open expression '{' expression_list '}' '}' {
      driver.closeExpression();
      const auto concatenation = driver.add(idb::verilog::ExpressionKind::concatenate, @1, {}, std::move($4));
      $$ = driver.add(idb::verilog::ExpressionKind::repeat, @1, {}, {$2, concatenation});
    }
  ;
expression_list:
    expression { $$.push_back($1); }
  | expression_list ',' expression { $$ = std::move($1); $$.push_back($3); }
  ;
unary_operator:
    '+' { $$ = "+"; } | '-' { $$ = "-"; } | '!' { $$ = "!"; } | '~' { $$ = "~"; }
  | '&' { $$ = "&"; } | '|' { $$ = "|"; } | '^' { $$ = "^"; }
  | NAND { $$ = "~&"; } | NOR { $$ = "~|"; } | XNOR { $$ = "~^"; }
  ;
unary_prefix: unary_operator { driver.openExpression(@1); $$ = std::move($1); };
expression:
    primary { $$ = $1; }
  | unary_prefix expression %prec UNARY { driver.closeExpression(); $$ = driver.add(idb::verilog::ExpressionKind::unary, @1, std::move($1), {$2}); }
  | expression LOR expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "||", {$1, $3}); }
  | expression LAND expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "&&", {$1, $3}); }
  | expression '|' expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "|", {$1, $3}); }
  | expression '^' expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "^", {$1, $3}); }
  | expression XNOR expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "~^", {$1, $3}); }
  | expression '&' expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "&", {$1, $3}); }
  | expression EQ expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "==", {$1, $3}); }
  | expression NE expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "!=", {$1, $3}); }
  | expression CASE_EQ expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "===", {$1, $3}); }
  | expression CASE_NE expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "!==", {$1, $3}); }
  | expression '<' expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "<", {$1, $3}); }
  | expression LE expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "<=", {$1, $3}); }
  | expression '>' expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, ">", {$1, $3}); }
  | expression GE expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, ">=", {$1, $3}); }
  | expression LSHIFT expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "<<", {$1, $3}); }
  | expression RSHIFT expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, ">>", {$1, $3}); }
  | expression ALSHIFT expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "<<<", {$1, $3}); }
  | expression ARSHIFT expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, ">>>", {$1, $3}); }
  | expression '+' expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "+", {$1, $3}); }
  | expression '-' expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "-", {$1, $3}); }
  | expression '*' expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "*", {$1, $3}); }
  | expression '/' expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "/", {$1, $3}); }
  | expression '%' expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "%", {$1, $3}); }
  | expression POWER expression { $$ = driver.add(idb::verilog::ExpressionKind::binary, @1, "**", {$1, $3}); }
  | expression conditional_open expression ':' expression %prec '?' {
      driver.closeExpression(); $$ = driver.add(idb::verilog::ExpressionKind::conditional, @1, {}, {$1, $3, $5});
    }
  ;
%%
void idb::verilog::grammar::Parser::error(const location_type& location, const std::string& message)
{ throw idb::verilog::detail::Error(location, "unsupported structural Verilog construct or " + message); }

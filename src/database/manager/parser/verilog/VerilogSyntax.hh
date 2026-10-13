// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "VerilogSource.hh"

namespace idb::verilog {
using ExprId = uint32_t;
inline constexpr ExprId noExpr = UINT32_MAX;

enum class ExpressionKind
{
  reference,
  number,
  fill,
  call,
  concatenate,
  repeat,
  unary,
  binary,
  conditional,
  select,
  slice,
  indexedUp,
  indexedDown
};
struct Expression
{
  ExpressionKind kind;
  SourceLocation location;
  std::string text;
  std::vector<ExprId> operands;
};
struct Range
{
  ExprId left = noExpr;
  ExprId right = noExpr;
  bool present() const { return left != noExpr; }
};
// Written spelling is retained here. Port defaults and object kinds are semantic decisions.
enum class DeclaredObjectKind
{
  implicit,
  net,
  variable
};
enum class DeclaredDataType
{
  implicit,
  logic,
  integer,
  intType
};
struct Declaration
{
  SourceLocation location;
  std::string name;
  Direction direction = Direction::none;
  NetType type = NetType::wire;
  bool is_signed = false;
  Range range;
  DeclaredObjectKind object_kind = DeclaredObjectKind::implicit;
  DeclaredDataType data_type = DeclaredDataType::implicit;
  ExprId initializer = noExpr;
  bool complete() const { return object_kind != DeclaredObjectKind::implicit || data_type != DeclaredDataType::implicit; }
};
struct Parameter
{
  SourceLocation location;
  std::string name;
  ExprId value;
  Range range;
  DeclaredDataType data_type = DeclaredDataType::implicit;
  std::optional<bool> signing;
  bool local = false;
  bool header = false;
};
enum class ConnectionKind
{
  expression,
  implicit,
  wildcard
};
struct Connection
{
  SourceLocation location;
  std::string name;  // Empty for positional connections. A missing expression is an open port.
  ExprId expression = noExpr;
  ConnectionKind kind = ConnectionKind::expression;
};
struct Instance
{
  SourceLocation location;
  std::string type;
  std::string name;
  std::vector<Connection> parameters;
  std::vector<Connection> ports;
  Range range;
};
struct Assignment
{
  SourceLocation location;
  ExprId left;
  ExprId right;
};
struct Generate;
using Statement = std::variant<Instance, Assignment, std::unique_ptr<Generate>>;
struct PathComponent
{
  std::string name;
  ExprId index = noExpr;
};
struct ParameterOverride
{
  SourceLocation location;
  std::vector<PathComponent> path;
  ExprId value;
};
// A lexical body is shared by module and generate syntax, without pretending a
// generate block is a module or giving it a module's ports and attributes.
struct ScopeSyntax
{
  SourceLocation location;
  std::vector<Parameter> parameters;
  std::vector<Declaration> declarations;
  std::vector<Statement> statements;
  bool implicit_nets = true;
  unsigned generate_count = 0;
  std::vector<std::string> genvars;
  std::vector<ParameterOverride> overrides;
};
struct Attribute
{
  SourceLocation location;
  std::string name;
  ExprId value = noExpr;
  std::optional<std::string> string_value;
};
struct Module
{
  SourceLocation location;
  std::string name;
  std::vector<std::string> ports;
  bool ansi_ports = false;
  bool parameter_ports = false;
  std::vector<Attribute> attributes;
  ScopeSyntax scope;
};
struct GenerateBlock
{
  std::string name;
  bool direct_conditional = false;
  ScopeSyntax scope;
};
enum class GenerateKind
{
  block,
  conditional,
  loop,
  selection
};
struct GenerateBranch
{
  std::vector<ExprId> matches;  // Empty is the default case arm.
  std::unique_ptr<GenerateBlock> body;
};
struct Generate
{
  SourceLocation location;
  GenerateKind kind;
  unsigned number = 0;
  std::string variable;
  ExprId condition = noExpr;
  ExprId initial = noExpr;
  ExprId step = noExpr;
  std::vector<GenerateBranch> branches;
};
struct SyntaxTree
{
  std::string source;
  std::vector<std::string> source_files;
  std::vector<Expression> expressions;
  std::vector<Module> modules;
};

struct ParseResult
{
  std::unique_ptr<SyntaxTree> design;
  std::vector<Diagnostic> diagnostics;
  explicit operator bool() const { return design != nullptr; }
};

// No logging, process termination, mutable global state or borrowed AST handles.
// Verilog-2005 by default; common structural SystemVerilog is explicitly selectable.
ParseResult parse(std::string_view text, std::string source = "<memory>", const SourceOptions& options = {});
ParseResult readFile(const std::string& path, const SourceOptions& options = {});  // Plain text or gzip, without temporary files.
// Ordered files form one compilation unit: macros and compiler directives carry across files.
ParseResult readFiles(const std::vector<std::string>& paths, const SourceOptions& options = {});

}  // namespace idb::verilog

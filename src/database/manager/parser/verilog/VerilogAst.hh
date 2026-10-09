// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once

#include <string>
#include <variant>
#include <vector>

#include "VerilogTypes.hh"

namespace idb::verilog {
using ExprId = uint32_t;
inline constexpr ExprId noExpr = UINT32_MAX;

enum class ExpressionKind
{
  reference,
  number,
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
struct Declaration
{
  SourceLocation location;
  std::string name;
  Direction direction = Direction::none;
  NetType type = NetType::wire;
  bool is_signed = false;
  Range range;
  bool explicit_type = false;
};
struct Parameter
{
  SourceLocation location;
  std::string name;
  ExprId value;
  Range range;
  bool is_signed = false;
  bool local = false;
  bool integer = false;
};
struct Connection
{
  SourceLocation location;
  std::string name;  // Empty for positional connections. A missing expression is an open port.
  ExprId expression = noExpr;
};
struct Instance
{
  SourceLocation location;
  std::string type;
  std::string name;
  std::vector<Connection> parameters;
  std::vector<Connection> ports;
};
struct Assignment
{
  SourceLocation location;
  ExprId left;
  ExprId right;
};
using Statement = std::variant<Instance, Assignment>;
struct Module
{
  SourceLocation location;
  std::string name;
  std::vector<std::string> ports;
  std::vector<Parameter> parameters;
  std::vector<Declaration> declarations;
  std::vector<Statement> statements;
  bool implicit_nets = true;
  bool ansi_ports = false;
  bool parameter_ports = false;
};
struct Design
{
  std::string source;
  std::vector<Expression> expressions;
  std::vector<Module> modules;
};

}  // namespace idb::verilog

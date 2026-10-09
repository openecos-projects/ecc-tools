// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace idb::verilog {
enum class Direction
{
  none,
  input,
  output,
  inout
};
enum class NetType
{
  wire,
  tri,
  wand,
  wor,
  supply0,
  supply1
};

struct SourceLocation
{
  uint32_t line = 1;
  uint32_t column = 1;
};
struct Diagnostic
{
  std::string file;
  SourceLocation location;
  std::string message;
  std::string text() const { return file + ":" + std::to_string(location.line) + ":" + std::to_string(location.column) + ": " + message; }
};
namespace detail {
struct Error : std::runtime_error
{
  SourceLocation location;
  Error(SourceLocation position, std::string message) : std::runtime_error(std::move(message)), location(position) {}
};
}  // namespace detail

}  // namespace idb::verilog

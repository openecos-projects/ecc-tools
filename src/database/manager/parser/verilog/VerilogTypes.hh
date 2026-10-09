// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace idb::verilog {
enum class LanguageMode
{
  verilog2005,
  systemVerilog
};
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

// A single cell lookup supplies both leaf identity and port types. Port order
// requires a Verilog module declaration; physical pin maps cannot invent it.
struct PortShape
{
  Direction direction;
  uint32_t width;
  bool is_signed = false;
  bool two_state = false;
  NetType net_type = NetType::wire;
  bool is_variable = false;
  std::optional<std::pair<int32_t, int32_t>> range;
};
struct LibraryCell
{
  std::unordered_map<std::string, PortShape> ports;
};
using LibraryLookup = std::function<const LibraryCell*(std::string_view)>;

struct SourceLocation
{
  uint32_t line = 1;
  uint32_t column = 1;
  uint32_t file = 0;
  uint32_t order = 0;  // Token order in the compilation unit, including expanded includes.
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

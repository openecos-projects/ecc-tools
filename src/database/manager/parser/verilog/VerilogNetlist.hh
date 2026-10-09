// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "VerilogTypes.hh"
#include "VerilogValue.hh"

namespace idb::verilog {
using NetId = uint32_t;
inline constexpr NetId zeroBit = UINT32_MAX;
inline constexpr NetId oneBit = UINT32_MAX - 1;
inline constexpr NetId xBit = UINT32_MAX - 2;
inline constexpr NetId zBit = UINT32_MAX - 3;
inline constexpr bool isConstant(NetId bit)
{
  return bit >= zBit;
}
struct Signal
{
  std::vector<NetId> bits;  // Most significant first, independent of the declaration's index direction.
  bool is_signed = false;
  bool unsized = false;
};
struct FlatNet
{
  std::string name;
  NetType type;
};
struct FlatPort
{
  std::string name;
  Direction direction;
  std::optional<std::pair<int32_t, int32_t>> range;
  Signal signal;
};
struct FlatConnection
{
  SourceLocation location;
  std::string name;
  Signal signal;
};
struct FlatInstance
{
  SourceLocation location;
  std::string type;
  std::string name;
  std::vector<FlatConnection> ports;
};
struct FlatBus
{
  std::string name;
  int32_t left;
  int32_t right;
  std::vector<NetId> bits;
};
// Directed assignments remain distinct from electrical inout bindings.
struct NetConnection
{
  NetId target;
  NetId source;
  SourceLocation location;
};
struct FlatDesign
{
  std::string source;
  std::string top;
  std::vector<FlatNet> nets;
  std::vector<FlatPort> ports;
  std::vector<FlatInstance> instances;
  std::vector<FlatBus> buses;
  std::vector<NetConnection> assignments;
  std::vector<NetConnection> inouts;
};
inline Signal resized(Signal signal, std::size_t width)
{
  if (width > BitVector::kMaxWidth || width == 0)
    throw std::invalid_argument("signal width exceeds frontend limit");
  if (signal.bits.size() > width)
    signal.bits.erase(signal.bits.begin(), signal.bits.end() - width);
  else if (signal.bits.size() < width)
    signal.bits.insert(
        signal.bits.begin(), width - signal.bits.size(),
        !signal.bits.empty() && (signal.is_signed || (signal.unsized && (signal.bits.front() == xBit || signal.bits.front() == zBit)))
            ? signal.bits.front()
            : zeroBit);
  return signal;
}

}  // namespace idb::verilog

#pragma once

#include <cstdint>
#include <functional>

namespace ecc::symbol {

// A dense, domain-typed name id. Domain is a phantom tag (e.g. `struct PinTag;`)
// so an Id<PinTag> cannot be passed where an Id<NetTag> is expected, even
// though both are a plain uint32_t underneath. Value 0 is reserved as "no id".
template <typename Domain>
struct Id
{
  uint32_t value = 0;

  constexpr bool valid() const { return value != 0; }
  friend constexpr bool operator==(Id lhs, Id rhs) { return lhs.value == rhs.value; }
  friend constexpr bool operator!=(Id lhs, Id rhs) { return lhs.value != rhs.value; }
};

}  // namespace ecc::symbol

template <typename Domain>
struct std::hash<ecc::symbol::Id<Domain>>
{
  std::size_t operator()(ecc::symbol::Id<Domain> id) const { return std::hash<uint32_t>{}(id.value); }
};

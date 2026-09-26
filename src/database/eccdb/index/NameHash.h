#pragma once

#include <cstddef>
#include <functional>
#include <string_view>

namespace eccdb::index {

struct TransparentStringHash
{
  using is_transparent = void;

  [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept { return std::hash<std::string_view>{}(value); }
};

}  // namespace eccdb::index

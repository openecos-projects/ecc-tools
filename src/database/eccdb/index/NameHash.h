// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

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

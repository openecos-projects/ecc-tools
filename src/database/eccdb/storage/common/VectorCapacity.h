// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <stdexcept>
#include <vector>

namespace eccdb::detail {

// Reserve enough capacity for one append without forcing an allocation for
// every element. Call this before mutating the vector's owning object so an
// allocation failure leaves the logical relation unchanged.
template <typename T>
void reserveAppendSlot(std::vector<T>& values)
{
  if (values.size() != values.capacity()) return;

  const auto size = values.size();
  const auto maximum = values.max_size();
  if (size == maximum) throw std::length_error("vector has no append capacity");

  // A vector close to max_size cannot double. Reserving the single required
  // slot avoids overflow and avoids requesting an unnecessarily huge buffer.
  const auto next_capacity = size == 0u ? 1u : (size > maximum / 2u ? size + 1u : size * 2u);
  values.reserve(next_capacity);
}

}  // namespace eccdb::detail

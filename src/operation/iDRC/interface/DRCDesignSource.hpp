// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory
#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "../../../database/interaction/RT_DRC/ids.hpp"

namespace idrc {

// Input projection and ordinal lookup for a caller-owned design. Implementations
// own their run snapshot; the checker only consumes its existing Shape format.
class DRCDesignSource
{
 public:
  virtual ~DRCDesignSource() = default;
  virtual void beginRun() = 0;
  // May be called after partial initialization or detachment; must be idempotent.
  virtual void endRun() noexcept = 0;
  virtual void wrapDatabase() = 0;
  [[nodiscard]] virtual int32_t regularNetCount() const = 0;
  [[nodiscard]] virtual std::string netName(int32_t net_idx, bool initialized) const = 0;
  [[nodiscard]] virtual std::vector<ids::Shape> environmentShapes(std::set<size_t>* obs_shape_idx_set) const = 0;
  [[nodiscard]] virtual std::vector<ids::Shape> resultShapes() const = 0;
};

}  // namespace idrc

// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file FastSTAIncremental.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief Incremental update coordinator for CTS fast STA contexts.
 */

#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "FastSTA.hh"
#include "clock_sizing/FastSTAClockSizingEdit.hh"

namespace icts {

struct FastStaContext;

class FastStaIncremental
{
 public:
  FastStaIncremental() = delete;

  static auto changeBufferMaster(FastStaContext& context, FastStaNodeId node_id, std::string_view cell_master) -> bool;
  static auto validateBufferMasterChanges(const FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes) -> bool;
  static auto describeBufferMasterRegion(const FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes)
      -> std::optional<FastStaDirtyRegion>;
  // Clock-sizing trials only update the physical clock domain.  The complete
  // timing graph remains resident for publication, but trial discovery must
  // not walk data-path nets that happen to leave a clock sink.
  static auto describeClockBufferMasterRegion(const FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes)
      -> std::optional<FastStaDirtyRegion>;
  static auto changeBufferMasters(FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes) -> bool;
  static auto changeBufferMastersIncremental(FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes)
      -> std::optional<FastStaDirtyRegion>;
  static auto changeBufferMastersClockIncremental(FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes)
      -> std::optional<FastStaDirtyRegion>;
  static auto changeBufferMasterIncremental(FastStaContext& context, FastStaNodeId node_id, std::string_view cell_master) -> std::optional<FastStaDirtyRegion>;

 private:
  friend class FastSTA;

  // Used synchronously after describeClockBufferMasterRegion on this unchanged
  // context. Keeping this private prevents callers from bypassing owner/region
  // validation or retaining a prepared region across model mutations.
  static auto applyPreparedClockBufferMasters(FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes) -> bool;
};

}  // namespace icts

// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
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

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <boost/json.hpp>

namespace odb {
class dbAccessPoint;
class dbBPin;
class dbBlock;
class dbInst;
class dbITerm;
class dbMPin;
class dbNet;
}  // namespace odb

namespace eccdb::tritonroute::odb_trace_projection {

// A projection is either a complete, backend-comparable JSON value or an
// explicitly unrecordable observation. Callers must not emit expected when
// complete() is false. `ignored` documents data that the corresponding DRT
// reader deliberately filters (for example, a backside routing shape).
struct Projection
{
  std::optional<boost::json::value> expected;
  std::vector<std::string> unrecordable;
  std::vector<std::string> ignored;

  [[nodiscard]] bool complete() const noexcept
  {
    return expected.has_value() && unrecordable.empty();
  }
};

// Named object collections are canonicalized when order has no meaning.
// Guide, wire-shape, and access-point sequences retain ODB iteration order.
//
// blockages:
//   [[layer, x_min, y_min, x_max, y_max], ...]
// Mirrors Parser::setObstructions. Backside-layer blockages are reported in
// ignored because DRT's filtered technology has no corresponding layer.
[[nodiscard]] Projection blockages(odb::dbBlock* block);

// bterms:
//   [[name, signal_type, io_type, net_or_null,
//     [[ [layer, x_min, y_min, x_max, y_max], ...], ...]], ...]
// The innermost group is one dbBPin, so BPin multiplicity is preserved. This
// covers the geometry and terminal metadata consumed by Parser::setBTerms;
// pin-access data is intentionally projected separately.
[[nodiscard]] Projection bterms(odb::dbBlock* block);

// specialWire:
//   [[wire_type,
//     [["path", layer, shape_type, begin_x, begin_y, end_x, end_y, width]
//       | ["via", "tech"|"design", via_name, x, y], ...]], ...]
// Mirrors Parser::updateNetRouting for a SPECIAL net after its SBox geometry
// is converted to the path coordinates DRT consumes. Backside shapes are
// recorded in ignored, not silently included in expected.
[[nodiscard]] Projection specialWire(odb::dbNet* net);

// guides:
//   [[layer, via_layer_or_null, x_min, y_min, x_max, y_max,
//     congested, jumper, connected_to_term], ...]
// This is the per-net ODB guide model read by GuideProcessor::readGuides and
// written by GuideProcessor::saveGuidesUpdates.
[[nodiscard]] Projection guides(odb::dbNet* net);

// pinAccess:
//   [x, y, layer, [N|E|S|W|U|D, ...], low_type, high_type,
//    [[cut_count, [["tech"|"design", via_name], ...]], ...],
//    [[x_min, y_min, x_max, y_max, begin_truncated, end_truncated], ...]]
// Mirrors the fields copied by Parser::updatefrAccessPoint. A JSON null value
// is a complete result for an access point on a backside layer: DRT drops it.
[[nodiscard]] Projection pinAccess(odb::dbAccessPoint* access_point);

// btermPinAccess:
//   [pinAccess, ...]
// This preserves the BPin access-point collection consumed by
// Parser::setBTerms. Backside access points are omitted from the array and
// reported in ignored because updatefrAccessPoint drops them.
[[nodiscard]] Projection btermPinAccess(odb::dbBPin* pin);

// masterPinAccess:
//   [[group_index, [pinAccess, ...]], ...]
// Group order is preserved because it is the ODB pin-access index used by DRT.
[[nodiscard]] Projection masterPinAccess(odb::dbMPin* pin);

// preferredPinAccess:
//   [[master_port_ordinal, pinAccess], ...]
// The ordinal is the dbMPin order within the owning dbMTerm. It lets an
// adapter reproduce Parser::setAccessPoints without using ODB object ids.
// For the dbInst overload the outer record is [mterm_name, pinAccess_list].
[[nodiscard]] Projection preferredPinAccess(odb::dbInst* instance);

// Same preferred-access projection for one ITerm. This is useful at the exact
// Parser::setAccessPoints call site and does not require scanning the block.
[[nodiscard]] Projection preferredPinAccess(odb::dbITerm* term);

}  // namespace eccdb::tritonroute::odb_trace_projection

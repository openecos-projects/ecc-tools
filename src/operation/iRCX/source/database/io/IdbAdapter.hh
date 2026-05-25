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
#pragma once

#include <cassert>
#include <optional>

#include "Types.hh"
#include "builder.h"
namespace ircx {

class Net;
class Segment;
class Patch;
class Via;
class Pin;

class LayoutData;
class LayerTable;
class SpefContext;

class IdbAdapter
{
 public:
  explicit IdbAdapter(::idb::IdbBuilder* idb) : idb_(idb) { assert(idb_); }
  IdbAdapter() = delete;
  ~IdbAdapter() = default;

  bool adapt(LayoutData&, LayerTable&, SpefContext&);

 private:
  void adaptLayerTable(::idb::IdbLayers* idb_layers);
  void adaptRoutingLayer(::idb::IdbLayers* idb_layers);

  void adaptSpefContext(::idb::IdbDesign* idb_design);

  void adaptNet(::idb::IdbNetList* idb_netlist);
  Pin adaptPin(::idb::IdbPin* idb_pin, bool is_driving);
  std::optional<Segment> adaptSegments(::idb::IdbRegularWireSegment* idb_seg);
  std::optional<Patch> adaptPatch(::idb::IdbRegularWireSegment* idb_seg);
  std::optional<Via> adaptVia(::idb::IdbVia* idb_via);

  void adaptSpecialNet(::idb::IdbSpecialNetList* idb_special_netlist);

  //idb
  ::idb::IdbBuilder* idb_{nullptr};
  // from outside
  LayoutData* layout_data_{nullptr};
  LayerTable* layer_table_{nullptr};
  SpefContext* spef_context_{nullptr};

  GtlRectI IdbRectToGtlRect(::idb::IdbRect*) const;
  GtlPointI IdbPointToGtlPoint(::idb::IdbCoordinate<int32_t>*) const;
};

} // namespace ircx

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

#include <string>
#include <vector>

#include "ARInternalTypes.hpp"
#include "GeometryTypes.h"
#include "IdbDesign.h"
#include "IdbNet.h"
#include "IdbRegularWire.h"
#include "IdbSpecialNet.h"
#include "LocalStubRouter.hpp"
#include "RowSiteGrid.hpp"

namespace imj {

class RepairTransaction
{
 public:
  bool commit(idb::IdbDesign* design, idb::IdbNet* net, const DiodeCandidateMaster& dm, const SiteCandidate& cand,
              const CandidateDiodeGeometry& geom, const RoutePlan& plan, const PGSpecialNets& pg_nets,
              RowSiteGrid& row_grid, LocalStubRouter& router);

  void rollback();

  const std::string& instanceName() const { return _inst_name; }
  int wireSegmentsAdded() const { return _wire_segments_added; }
  int viasAdded() const { return _vias_added; }

 private:
  bool _committed = false;
  idb::IdbDesign* _design = nullptr;
  idb::IdbNet* _net = nullptr;
  idb::IdbRegularWire* _target_wire = nullptr;
  bool _created_wire = false;
  std::string _inst_name;
  SiteCandidate _site;
  RowSiteGrid* _row_grid = nullptr;
  LocalStubRouter* _router = nullptr;
  std::vector<idb::IdbRegularWireSegment*> _added_segments;
  std::vector<ecc::geometry::ShapeId> _added_shape_ids;
  int _wire_segments_added = 0;
  int _vias_added = 0;
};

}  // namespace imj

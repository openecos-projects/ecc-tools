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
#include "RepairTransaction.hpp"

#include <algorithm>
#include <map>
#include <utility>

#include "IdbInstance.h"
#include "IdbPins.h"

namespace imj {
namespace {

void connectDiodePGPins(idb::IdbDesign* design, idb::IdbInstance* inst, idb::IdbTerm* sig_term,
                        const PGSpecialNets& pg_nets)
{
  if (!design || !inst || !inst->get_pin_list() || !design->get_special_net_list()) {
    return;
  }
  for (idb::IdbPin* pin : inst->get_pin_list()->get_pin_list()) {
    if (!pin || !pin->get_term() || pin->get_term() == sig_term) {
      continue;
    }
    if (idb::IdbSpecialNet* sn = resolvePGSpecialNet(design, pin->get_term(), pg_nets)) {
      design->connectPinToSpecialNet(pin, sn);
    }
  }
}

idb::IdbRegularWire* findExistingRoutedWire(idb::IdbNet* net)
{
  if (!net || !net->get_wire_list()) {
    return nullptr;
  }
  for (idb::IdbRegularWire* wire : net->get_wire_list()->get_wire_list()) {
    if (wire && wire->get_wire_statement() == idb::IdbWiringStatement::kRouted) {
      return wire;
    }
  }
  return nullptr;
}

}  // namespace

idb::IdbSpecialNet* resolvePGSpecialNet(idb::IdbDesign* design, idb::IdbTerm* term, const PGSpecialNets& pg_nets)
{
  if (!design || !term || !design->get_special_net_list()) {
    return nullptr;
  }
  auto* snets = design->get_special_net_list();
  idb::IdbSpecialNet* sn = snets->find_net(term->get_name());
  if (!sn && term->is_power()) {
    sn = pg_nets.default_pwr;
  } else if (!sn && term->is_ground()) {
    sn = pg_nets.default_gnd;
  }
  return sn;
}

PGSpecialNets findDefaultPGSpecialNets(idb::IdbDesign* design)
{
  PGSpecialNets pg;
  if (!design || !design->get_special_net_list()) {
    return pg;
  }
  for (idb::IdbSpecialNet* sn : design->get_special_net_list()->get_net_list()) {
    if (!sn) {
      continue;
    }
    if (!pg.default_pwr && sn->get_connect_type() == idb::IdbConnectType::kPower) {
      pg.default_pwr = sn;
    }
    if (!pg.default_gnd && sn->get_connect_type() == idb::IdbConnectType::kGround) {
      pg.default_gnd = sn;
    }
  }
  return pg;
}

bool RepairTransaction::commit(idb::IdbDesign* design, idb::IdbNet* net, const DiodeCandidateMaster& dm,
                               const SiteCandidate& cand, const CandidateDiodeGeometry& geom, const RoutePlan& plan,
                               const PGSpecialNets& pg_nets, RowSiteGrid& row_grid, LocalStubRouter& router)
{
  if (!design || !net || !net->get_wire_list() || !dm.master || !dm.signal_term || !plan.found) {
    return false;
  }

  // Pre-validate via templates before mutating IDB state.
  for (const StubMove& mv : plan.moves) {
    if (mv.is_via) {
      const ViaOption* vo = router.viaBetween(mv.layer_idx, mv.next_layer_idx);
      if (!vo || !vo->tmpl) {
        return false;
      }
    } else if (mv.layer_idx < 0 || mv.layer_idx >= static_cast<int>(router.layers().size())) {
      return false;
    }
  }

  _design = design;
  _net = net;
  _site = cand;
  _row_grid = &row_grid;
  _router = &router;
  _created_wire = false;
  _added_segments.clear();
  _added_shape_ids.clear();
  _wire_segments_added = 0;
  _vias_added = 0;

  _target_wire = findExistingRoutedWire(net);
  if (!_target_wire) {
    _target_wire = net->get_wire_list()->add_wire();
    if (!_target_wire) {
      return false;
    }
    _target_wire->set_wire_state(idb::IdbWiringStatement::kRouted);
    _created_wire = true;
  }

  _inst_name = design->makeUniqueInstanceName("ant_diode");
  idb::IdbInstance* inst = design->createInstance(
      _inst_name, dm.master->get_name(), idb::IdbInstanceType::kDist, idb::IdbPlacementStatus::kPlaced,
      normalizeOrient(cand.orient), cand.inst_x, cand.inst_y, idb::IdbCreatePolicy::kErrorIfExists);
  if (!inst) {
    rollback();
    return false;
  }

  idb::IdbPin* sig_pin = inst->get_pin(dm.signal_term->get_name());
  if (!sig_pin || !design->connectPinToNet(sig_pin, net)) {
    rollback();
    return false;
  }
  connectDiodePGPins(design, inst, dm.signal_term, pg_nets);

  std::vector<LayerShapeBox> metal_shapes = geom.all_signal_metal_shapes;
  std::vector<std::pair<ecc::geometry::LayerId, idb::IdbRect>> cut_shapes;
  for (const CutShapeBox& cs : geom.all_signal_cut_shapes) {
    const ecc::geometry::LayerId cut_lid = router.geomCutLayerIdForOrder(cs.cut_order);
    if (cut_lid != 0) {
      cut_shapes.emplace_back(cut_lid, cs.rect);
    }
  }

  bool first_seg = (_target_wire->get_num() == 0);
  for (const StubMove& mv : plan.moves) {
    idb::IdbRegularWireSegment* seg = _target_wire->add_segment();
    if (!seg) {
      rollback();
      return false;
    }
    _added_segments.push_back(seg);
    if (!first_seg) {
      seg->set_layer_as_new();
    }
    first_seg = false;

    if (mv.is_via) {
      const ViaOption* vo = router.viaBetween(mv.layer_idx, mv.next_layer_idx);
      seg->set_layer(vo->bottom_layer);
      seg->set_layer_name(vo->bottom_layer->get_name());
      seg->set_is_via(true);
      seg->add_point(mv.x1, mv.y1);
      idb::IdbVia* v = seg->copy_via(vo->tmpl);
      if (!v) {
        rollback();
        return false;
      }
      v->set_coordinate(mv.x1, mv.y1);
      for (const idb::IdbRect& br : vo->bottom_rects) {
        metal_shapes.push_back({mv.layer_idx, shiftedRect(br, mv.x1, mv.y1)});
      }
      for (const idb::IdbRect& tr : vo->top_rects) {
        metal_shapes.push_back({mv.next_layer_idx, shiftedRect(tr, mv.x1, mv.y1)});
      }
      for (const idb::IdbRect& cr : vo->cut_rects) {
        if (vo->cut_geom_layer_id != 0) {
          cut_shapes.emplace_back(vo->cut_geom_layer_id, shiftedRect(cr, mv.x1, mv.y1));
        }
      }
      ++_vias_added;
    } else {
      const auto& linfo = router.layers()[mv.layer_idx];
      seg->set_layer(linfo.layer);
      seg->set_layer_name(linfo.layer->get_name());
      if (linfo.has_custom_wire_ext) {
        seg->add_flush_point(mv.x1, mv.y1, linfo.end_ext);
        seg->add_flush_point(mv.x2, mv.y2, linfo.end_ext);
      } else {
        seg->add_point(mv.x1, mv.y1);
        seg->add_point(mv.x2, mv.y2);
      }
      metal_shapes.push_back({mv.layer_idx, router.wireBox(mv.layer_idx, mv.x1, mv.y1, mv.x2, mv.y2)});
      ++_wire_segments_added;
    }
  }

  auto appendShapeIds = [&](const std::vector<ecc::geometry::ShapeId>& sids) {
    _added_shape_ids.insert(_added_shape_ids.end(), sids.begin(), sids.end());
  };

  appendShapeIds(router.addCommittedNetShapes(net, metal_shapes, cut_shapes));

  std::map<idb::IdbSpecialNet*, std::pair<std::vector<LayerShapeBox>, std::vector<CutShapeBox>>> pg_by_snet;
  std::vector<LayerShapeBox> obs_metals = geom.obs_metal_shapes;
  std::vector<CutShapeBox> obs_cuts = geom.obs_cut_shapes;
  for (const PGShapeBox& pg : geom.pg_metal_shapes) {
    if (pg.snet != nullptr) {
      pg_by_snet[pg.snet].first.push_back({pg.layer_idx, pg.rect, 0});
    } else {
      obs_metals.push_back({pg.layer_idx, pg.rect, 0});
    }
  }
  for (const PGCutShapeBox& pgc : geom.pg_cut_shapes) {
    if (pgc.snet != nullptr) {
      pg_by_snet[pgc.snet].second.push_back({pgc.cut_order, pgc.rect});
    } else {
      obs_cuts.push_back({pgc.cut_order, pgc.rect});
    }
  }
  for (const auto& [snet, shapes] : pg_by_snet) {
    appendShapeIds(router.addCommittedSpecialNetShapes(snet, shapes.first, shapes.second));
  }
  appendShapeIds(router.addCommittedObstacles(obs_metals));
  appendShapeIds(router.addCommittedCutObstacles(obs_cuts));

  row_grid.markOccupied(cand.row_idx, cand.site_idx, cand.sites_needed);
  _committed = true;
  return true;
}

void RepairTransaction::rollback()
{
  if (_router != nullptr && !_added_shape_ids.empty()) {
    _router->removeCommittedShapes(_added_shape_ids);
  }
  _added_shape_ids.clear();

  if (_committed && _row_grid != nullptr) {
    _row_grid->unmarkOccupied(_site.row_idx, _site.site_idx, _site.sites_needed);
  }
  _committed = false;

  if (_target_wire != nullptr) {
    for (idb::IdbRegularWireSegment* seg : _added_segments) {
      if (seg != nullptr) {
        _target_wire->delete_seg(seg);
      }
    }
  }
  _added_segments.clear();

  if (_created_wire && _net != nullptr && _net->get_wire_list() != nullptr && _target_wire != nullptr) {
    auto& wlist = _net->get_wire_list()->get_wire_list();
    auto it = std::find(wlist.begin(), wlist.end(), _target_wire);
    if (it != wlist.end()) {
      wlist.erase(it);
      delete _target_wire;
    }
  }
  _target_wire = nullptr;
  _created_wire = false;

  if (_design != nullptr && !_inst_name.empty()) {
    _design->removeInstanceSafe(_inst_name);
  }
  _inst_name.clear();
  _wire_segments_added = 0;
  _vias_added = 0;
}

}  // namespace imj

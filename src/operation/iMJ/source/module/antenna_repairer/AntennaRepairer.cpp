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
#include "AntennaRepairer.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "ARConfig.hpp"
#include "ARInternalTypes.hpp"
#include "ARReporter.hpp"
#include "AntennaChecker.hpp"
#include "DiodeLibrary.hpp"
#include "IdbDesign.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbRegularWire.h"
#include "LocalStubRouter.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "RepairTransaction.hpp"
#include "RowSiteGrid.hpp"
#include "idm.h"

namespace imj {
namespace {

std::vector<idb::IdbPin*> collectTargetPinsForLayer(idb::IdbNet* net, const std::vector<const ACViolation*>& net_viols,
                                                    int target_order, idb::IdbLayers* layers)
{
  std::vector<idb::IdbPin*> result;
  std::unordered_set<idb::IdbPin*> seen;
  if (!net || !net->get_instance_pin_list()) {
    return result;
  }

  auto addMatchingViols = [&](bool filter_layer) {
    for (const ACViolation* v : net_viols) {
      if (filter_layer && layers && layers->get_layer_order(v->layer_name) != target_order) {
        continue;
      }
      for (idb::IdbPin* pin : net->get_instance_pin_list()->get_pin_list()) {
        if (!pin || seen.count(pin) || pin->get_port_box_list().empty()) {
          continue;
        }
        const std::string inst_name = pin->get_instance() ? pin->get_instance()->get_name() : "";
        if (pin->get_pin_name() == v->pin_name && inst_name == v->instance_name) {
          seen.insert(pin);
          result.push_back(pin);
        }
      }
    }
  };

  addMatchingViols(true);
  if (result.empty()) {
    addMatchingViols(false);
  }
  if (result.empty()) {
    for (idb::IdbPin* pin : net->get_instance_pin_list()->get_pin_list()) {
      if (!pin || !pin->get_term() || seen.count(pin) || pin->get_port_box_list().empty()) {
        continue;
      }
      if (pin->get_term()->has_antenna_gate_area() || pin->get_term()->has_antenna_gate_area_by_layer()) {
        seen.insert(pin);
        result.push_back(pin);
      }
    }
  }
  return result;
}

std::vector<LayerShapeBox> collectGoalShapesForNet(idb::IdbNet* net, const std::vector<idb::IdbPin*>& target_pins,
                                                   int max_route_order, const LocalStubRouter& router)
{
  std::vector<LayerShapeBox> goals;
  auto pushLayerShape = [&](idb::IdbLayerShape* sh) {
    if (!sh || !sh->get_layer()) {
      return;
    }
    const int order = static_cast<int>(sh->get_layer()->get_order());
    if (order > max_route_order) {
      return;
    }
    const int li = router.layerIdxByOrder(order);
    if (li < 0) {
      return;
    }
    for (idb::IdbRect* r : sh->get_rect_list()) {
      if (r) {
        goals.push_back({li, *r});
      }
    }
  };

  for (idb::IdbPin* gpin : target_pins) {
    for (idb::IdbLayerShape* sh : gpin->get_port_box_list()) {
      pushLayerShape(sh);
    }
    for (idb::IdbVia* via : gpin->get_via_list()) {
      if (!via) {
        continue;
      }
      idb::IdbLayerShape bsh = via->get_bottom_layer_shape();
      idb::IdbLayerShape tsh = via->get_top_layer_shape();
      pushLayerShape(&bsh);
      pushLayerShape(&tsh);
    }
  }
  if (net && net->get_wire_list()) {
    for (idb::IdbRegularWire* wire : net->get_wire_list()->get_wire_list()) {
      if (!wire) {
        continue;
      }
      for (idb::IdbRegularWireSegment* seg : wire->get_segment_list()) {
        if (!seg) {
          continue;
        }
        if ((seg->is_wire() || seg->is_rect()) && seg->get_layer()) {
          const int order = static_cast<int>(seg->get_layer()->get_order());
          const int li = (order <= max_route_order) ? router.layerIdxByOrder(order) : -1;
          if (li >= 0) {
            goals.push_back({li, seg->get_segment_rect()});
          }
        }
        if (seg->is_via()) {
          for (idb::IdbVia* via : seg->get_via_list()) {
            if (!via) {
              continue;
            }
            idb::IdbLayerShape bsh = via->get_bottom_layer_shape();
            idb::IdbLayerShape tsh = via->get_top_layer_shape();
            pushLayerShape(&bsh);
            pushLayerShape(&tsh);
          }
        }
      }
    }
  }
  return goals;
}

std::vector<int> sortedViolLayerOrders(const std::vector<const ACViolation*>& net_viols, idb::IdbLayers* layers,
                                       int fallback_max_order)
{
  std::set<int> unique_orders;
  for (const ACViolation* v : net_viols) {
    const int order = layers ? layers->get_layer_order(v->layer_name) : -1;
    if (order >= 0) {
      unique_orders.insert(order);
    }
  }
  if (unique_orders.empty() && fallback_max_order >= 0) {
    unique_orders.insert(fallback_max_order);
  }
  return std::vector<int>(unique_orders.begin(), unique_orders.end());
}

ACViolationType canonicalViolationType(ACViolationType type)
{
  switch (type) {
    case ACViolationType::kAntennaDiffPar:
      return ACViolationType::kAntennaPar;
    case ACViolationType::kAntennaDiffCar:
      return ACViolationType::kAntennaCar;
    case ACViolationType::kAntennaDiffPsr:
      return ACViolationType::kAntennaPsr;
    case ACViolationType::kAntennaDiffCsr:
      return ACViolationType::kAntennaCsr;
    case ACViolationType::kAntennaDiffCutPar:
      return ACViolationType::kAntennaCutPar;
    case ACViolationType::kAntennaDiffCutCar:
      return ACViolationType::kAntennaCutCar;
    default:
      return type;
  }
}

double normalizedExcess(const ACViolation& v)
{
  const double raw = std::max(0.0, v.ratio - v.threshold);
  return (v.threshold > kAntennaRepairEps) ? (raw / v.threshold) : raw;
}

bool isNetRepairProgress(const std::vector<const ACViolation*>& before_viols,
                         const std::vector<ACViolation>& after_viols)
{
  if (before_viols.empty()) {
    return false;
  }
  if (after_viols.empty()) {
    return true;
  }
  using ViolKey = std::tuple<std::string, std::string, std::string, int>;
  std::map<ViolKey, std::vector<double>> before_groups;
  std::map<ViolKey, std::vector<double>> after_groups;
  double before_total_excess = 0.0;
  double after_total_excess = 0.0;

  for (const ACViolation* v : before_viols) {
    if (v == nullptr) {
      continue;
    }
    const double ex = normalizedExcess(*v);
    before_total_excess += ex;
    before_groups[{v->instance_name, v->pin_name, v->layer_name, static_cast<int>(canonicalViolationType(v->type))}]
        .push_back(ex);
  }
  for (const ACViolation& v : after_viols) {
    const double ex = normalizedExcess(v);
    after_total_excess += ex;
    after_groups[{v.instance_name, v.pin_name, v.layer_name, static_cast<int>(canonicalViolationType(v.type))}]
        .push_back(ex);
  }
  for (auto& [k, vec] : before_groups) {
    (void) k;
    std::sort(vec.begin(), vec.end(), std::greater<double>());
  }
  for (auto& [k, avec] : after_groups) {
    auto bit = before_groups.find(k);
    if (bit == before_groups.end() || avec.size() > bit->second.size()) {
      return false;
    }
    std::sort(avec.begin(), avec.end(), std::greater<double>());
    const auto& bvec = bit->second;
    for (size_t i = 0; i < avec.size(); ++i) {
      if (avec[i] > bvec[i] + kAntennaRepairEps) {
        return false;
      }
    }
  }
  if (after_viols.size() < before_viols.size()) {
    return true;
  }
  const double min_step = std::max(1e-4, 1e-3 * before_total_excess);
  return after_total_excess + min_step < before_total_excess;
}

}  // namespace

void AntennaRepairer::initInst()
{
  if (_ar_instance == nullptr) {
    _ar_instance = new AntennaRepairer();
  }
}

AntennaRepairer& AntennaRepairer::getInst()
{
  if (_ar_instance == nullptr) {
    MJLOG.error(Loc::current(), "The instance is not initialized!");
  }
  return *_ar_instance;
}

void AntennaRepairer::destroyInst()
{
  if (_ar_instance != nullptr) {
    delete _ar_instance;
    _ar_instance = nullptr;
  }
}

ARSummary AntennaRepairer::repair(std::map<std::string, std::any> config_map)
{
  Monitor monitor;
  MJLOG.info(Loc::current(), "Starting AntennaRepairer...");

  ARSummary summary;
  const ARConfig cfg = parseARConfig(config_map);
  if (!cfg.valid) {
    summary.ok = false;
    return summary;
  }

  idb::IdbDesign* design = (dmInst != nullptr) ? dmInst->get_idb_design() : nullptr;
  if (!design || !design->get_layout()) {
    MJLOG.warn(Loc::current(), "No design loaded; skipping antenna repair.");
    summary.ok = false;
    writeRepairOutputs(summary, cfg.report_path, cfg.step_json_path);
    return summary;
  }

  idb::IdbLayout* layout = design->get_layout();
  const int32_t dbu =
      (design->get_units() && design->get_units()->get_micron_dbu() > 0)
          ? design->get_units()->get_micron_dbu()
          : (layout->get_units() && layout->get_units()->get_micron_dbu() > 0 ? layout->get_units()->get_micron_dbu()
                                                                              : 1000);
  const int32_t radius_dbu = static_cast<int32_t>(std::llround(cfg.search_radius_um * dbu));

  ACModel ac_model = AntennaChecker::buildModelForDesign(design);
  std::vector<ACViolation> violations = AntennaChecker::collectDesignViolations(design, ac_model);
  summary.violations_before = static_cast<int>(violations.size());
  summary.violations_after = summary.violations_before;

  if (violations.empty()) {
    MJLOG.info(Loc::current(), "No antenna violations found; nothing to repair.");
    summary.ok = writeRepairOutputs(summary, cfg.report_path, cfg.step_json_path);
    MJLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
    return summary;
  }

  const std::vector<DiodeCandidateMaster> diodes = discoverDiodes(layout, cfg.explicit_diode);
  if (diodes.empty()) {
    MJLOG.warn(Loc::current(), "No usable antenna diode master found in LEF library.");
    std::set<std::string> uniq_nets;
    for (const ACViolation& v : violations) {
      uniq_nets.insert(v.net_name);
    }
    summary.nets_unrepaired = static_cast<int>(uniq_nets.size());
    summary.ok = writeRepairOutputs(summary, cfg.report_path, cfg.step_json_path);
    return summary;
  }

  RowSiteGrid row_grid;
  row_grid.init(design);
  const PGSpecialNets pg_nets = findDefaultPGSpecialNets(design);

  LocalStubRouter router;
  if (!router.init(design)) {
    MJLOG.warn(Loc::current(), "Failed to initialize routing context for antenna repair.");
    summary.ok = false;
    writeRepairOutputs(summary, cfg.report_path, cfg.step_json_path);
    return summary;
  }

  const int max_router_order = router.layers().back().order;
  auto orderToIdx = [&router](int order) { return router.layerIdxByOrder(order); };

  std::set<std::string> used_masters;
  std::set<std::string> repaired_nets;
  CandidateDiodeGeometry diode_geom;

  for (int iter = 0; iter < cfg.max_iter && !violations.empty(); ++iter) {
    summary.iterations = iter + 1;
    std::map<std::string, std::vector<const ACViolation*>> by_net;
    for (const ACViolation& v : violations) {
      by_net[v.net_name].push_back(&v);
    }

    int inserted_this_iter = 0;
    for (const auto& [net_name, net_viols] : by_net) {
      idb::IdbNet* net = design->get_net_list() ? design->get_net_list()->find_net(net_name) : nullptr;
      if (!net) {
        continue;
      }
      const std::vector<int> viol_orders =
          sortedViolLayerOrders(net_viols, layout->get_layers(), max_router_order);

      bool placed_for_net = false;
      for (int viol_order : viol_orders) {
        if (placed_for_net) {
          break;
        }
        const std::vector<idb::IdbPin*> target_pins =
            collectTargetPinsForLayer(net, net_viols, viol_order, layout->get_layers());
        if (target_pins.empty()) {
          continue;
        }
        const std::vector<LayerShapeBox> goal_shapes = collectGoalShapesForNet(net, target_pins, viol_order, router);
        if (goal_shapes.empty()) {
          continue;
        }
        idb::IdbPin* primary_pin = target_pins[0];
        idb::IdbCoordinate<int32_t>* avg = primary_pin->get_average_coordinate();
        const int32_t anchor_x =
            avg ? avg->get_x() : (goal_shapes[0].rect.get_low_x() + goal_shapes[0].rect.get_high_x()) / 2;
        const int32_t anchor_y =
            avg ? avg->get_y() : (goal_shapes[0].rect.get_low_y() + goal_shapes[0].rect.get_high_y()) / 2;

        for (const DiodeCandidateMaster& dm : diodes) {
          if (placed_for_net) {
            break;
          }
          if (!dm.supportsLayerOrder(viol_order)) {
            continue;
          }
          const std::vector<SiteCandidate> sites = row_grid.findCandidates(dm, anchor_x, anchor_y, radius_dbu);
          for (const SiteCandidate& cand : sites) {
            extractDiodeGeometry(dm, design, pg_nets, cand.inst_x, cand.inst_y, cand.orient, viol_order, orderToIdx,
                                 diode_geom);
            if (diode_geom.start_shapes.empty() || !router.validateCandidateCell(net, diode_geom)) {
              continue;
            }
            const RoutePlan plan = router.routeStub(net, viol_order, diode_geom, goal_shapes, radius_dbu);
            if (!plan.found) {
              continue;
            }

            RepairTransaction tx;
            if (!tx.commit(design, net, dm, cand, diode_geom, plan, pg_nets, row_grid, router)) {
              continue;
            }
            const std::vector<ACViolation> after_viols =
                AntennaChecker::collectNetViolations(design, ac_model, net);
            if (!isNetRepairProgress(net_viols, after_viols)) {
              tx.rollback();
              continue;
            }

            ARPlacedDiode pd;
            pd.instance_name = tx.instanceName();
            pd.master_name = dm.master->get_name();
            pd.net_name = net_name;
            pd.target_pin = (primary_pin->get_instance() ? primary_pin->get_instance()->get_name() + "/" : "") +
                            primary_pin->get_pin_name();
            pd.x_dbu = cand.inst_x;
            pd.y_dbu = cand.inst_y;
            pd.wire_segments_added = tx.wireSegmentsAdded();
            pd.vias_added = tx.viasAdded();
            summary.placed_diodes.push_back(std::move(pd));

            used_masters.insert(dm.master->get_name());
            repaired_nets.insert(net_name);
            ++summary.diodes_inserted;
            ++inserted_this_iter;
            placed_for_net = true;
            break;
          }
        }
      }
    }

    if (inserted_this_iter == 0) {
      break;
    }
    violations = AntennaChecker::collectDesignViolations(design, ac_model);
    summary.violations_after = static_cast<int>(violations.size());
  }

  std::set<std::string> remaining_nets;
  for (const ACViolation& v : violations) {
    remaining_nets.insert(v.net_name);
  }
  summary.nets_unrepaired = static_cast<int>(remaining_nets.size());
  for (const std::string& n : repaired_nets) {
    if (!remaining_nets.count(n)) {
      ++summary.nets_repaired;
    }
  }
  summary.diode_masters_used.assign(used_masters.begin(), used_masters.end());

  if (!writeRepairOutputs(summary, cfg.report_path, cfg.step_json_path)) {
    summary.ok = false;
  }
  MJLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
  return summary;
}

AntennaRepairer* AntennaRepairer::_ar_instance = nullptr;

}  // namespace imj

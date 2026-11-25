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
#include "DiodeLibrary.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <tuple>

#include "IdbMacroProperty.h"
#include "IdbObs.h"
#include "IdbPins.h"
#include "Logger.hpp"

namespace imj {
namespace {

std::string toUpperAscii(std::string s)
{
  for (char& ch : s) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return s;
}

std::vector<std::string> splitTokens(const std::string& input)
{
  std::vector<std::string> tokens;
  std::string cur;
  for (char ch : input) {
    if (ch == ',' || std::isspace(static_cast<unsigned char>(ch))) {
      if (!cur.empty()) {
        tokens.push_back(cur);
        cur.clear();
      }
    } else {
      cur.push_back(ch);
    }
  }
  if (!cur.empty()) {
    tokens.push_back(cur);
  }
  return tokens;
}

bool isPowerOrGroundTerm(idb::IdbTerm* term)
{
  if (!term) {
    return true;
  }
  return term->is_special_net() || term->is_power() || term->is_ground() ||
         term->get_type() == idb::IdbConnectType::kPower || term->get_type() == idb::IdbConnectType::kGround;
}

bool hasAnyGateArea(idb::IdbTerm* term)
{
  if (term->has_antenna_gate_area() && term->get_antenna_gate_area() > kAntennaRepairEps) {
    return true;
  }
  for (const auto& [lname, area] : term->get_antenna_gate_area_by_layer()) {
    (void) lname;
    if (area > kAntennaRepairEps) {
      return true;
    }
  }
  return false;
}

std::optional<DiodeCandidateMaster> inspectMaster(idb::IdbLayout* layout, idb::IdbCellMaster* master,
                                                  bool require_core_nonfiller)
{
  if (!layout || !master || master->get_width() == 0 || master->get_height() == 0) {
    return std::nullopt;
  }
  if (master->get_origin_x() != 0 || master->get_origin_y() != 0) {
    return std::nullopt;
  }
  const idb::CellMasterType mtype = master->get_type();
  if (mtype == idb::CellMasterType::kCoreTieHigh || mtype == idb::CellMasterType::kCoreTieLow ||
      master->is_core_filler() || master->is_endcap()) {
    return std::nullopt;
  }
  if (require_core_nonfiller &&
      mtype != idb::CellMasterType::kCore && mtype != idb::CellMasterType::kCoreAntenaCell) {
    return std::nullopt;
  }

  idb::IdbTerm* sig_term = nullptr;
  for (idb::IdbTerm* term : master->get_term_list()) {
    if (!term || isPowerOrGroundTerm(term)) {
      continue;
    }
    if (sig_term != nullptr) {
      return std::nullopt;  // Require a single signal terminal on a diode cell.
    }
    sig_term = term;
  }
  if (!sig_term || sig_term->get_port_list().empty()) {
    return std::nullopt;
  }
  const idb::IdbConnectDirection dir = sig_term->get_direction();
  if (dir != idb::IdbConnectDirection::kInput && dir != idb::IdbConnectDirection::kInOut) {
    return std::nullopt;
  }
  if (hasAnyGateArea(sig_term)) {
    return std::nullopt;
  }

  DiodeCandidateMaster cand;
  cand.master = master;
  cand.signal_term = sig_term;
  cand.unqualified_diff_area =
      sig_term->has_antenna_diff_area() ? std::max(0.0, sig_term->get_antenna_diff_area()) : 0.0;
  if (cand.unqualified_diff_area > kAntennaRepairEps) {
    cand.min_diff_decl_order = 0;
  }

  if (idb::IdbLayers* layers = layout->get_layers()) {
    for (const auto& [lname, area] : sig_term->get_antenna_diff_area_by_layer()) {
      if (area <= kAntennaRepairEps) {
        continue;
      }
      int order = layers->get_layer_order(lname);
      if (order >= 0) {
        cand.diff_area_by_layer_order[order] = std::max(cand.diff_area_by_layer_order[order], area);
        cand.min_diff_decl_order = std::min(cand.min_diff_decl_order, order);
      }
    }
  }

  for (idb::IdbPort* port : sig_term->get_port_list()) {
    if (!port) {
      continue;
    }
    for (idb::IdbLayerShape* sh : port->get_layer_shape()) {
      if (!sh || !sh->get_layer() || !sh->get_layer()->is_routing() || sh->get_rect_list().empty()) {
        continue;
      }
      const int order = static_cast<int>(sh->get_layer()->get_order());
      if (!cand.layerCarriesDiff(order)) {
        continue;
      }
      cand.min_effective_diff_order = std::min(cand.min_effective_diff_order, order);
      cand.max_diff_area = std::max(cand.max_diff_area, cand.effectiveDiffArea(order));
    }
  }

  if (cand.min_effective_diff_order == std::numeric_limits<int>::max()) {
    return std::nullopt;
  }

  const std::string upper = toUpperAscii(master->get_name());
  cand.is_antenna_class = (mtype == idb::CellMasterType::kCoreAntenaCell);
  cand.name_hint = (upper.find("ANT") != std::string::npos) || (upper.find("DIODE") != std::string::npos);
  cand.width = master->get_width();
  return cand;
}

void collectShapeList(idb::IdbLayerShape* sh, int32_t inst_x, int32_t inst_y, idb::IdbOrient orient, uint32_t master_w,
                      uint32_t master_h, const std::function<int(int)>& order_to_idx,
                      const std::function<void(int order, int layer_idx, const idb::IdbRect& rect)>& on_routing,
                      const std::function<void(int cut_order, const idb::IdbRect& rect)>& on_cut)
{
  if (!sh || !sh->get_layer()) {
    return;
  }
  const int order = static_cast<int>(sh->get_layer()->get_order());
  const bool is_cut = sh->get_layer()->is_cut();
  const int li = is_cut ? -1 : order_to_idx(order);
  if (!is_cut && li < 0) {
    return;
  }
  for (idb::IdbRect* r : sh->get_rect_list()) {
    if (!r) {
      continue;
    }
    const idb::IdbRect placed = transformMasterRect(*r, inst_x, inst_y, orient, master_w, master_h);
    if (is_cut) {
      if (on_cut) {
        on_cut(order, placed);
      }
    } else if (on_routing) {
      on_routing(order, li, placed);
    }
  }
}

void collectPortGeometry(const std::vector<idb::IdbPort*>& ports, int32_t inst_x, int32_t inst_y, idb::IdbOrient orient,
                         uint32_t master_w, uint32_t master_h, const std::function<int(int)>& order_to_idx,
                         const std::function<void(int order, int layer_idx, const idb::IdbRect& rect)>& on_routing,
                         const std::function<void(int cut_order, const idb::IdbRect& rect)>& on_cut)
{
  for (idb::IdbPort* port : ports) {
    if (!port) {
      continue;
    }
    for (idb::IdbLayerShape* sh : port->get_layer_shape()) {
      collectShapeList(sh, inst_x, inst_y, orient, master_w, master_h, order_to_idx, on_routing, on_cut);
    }
    for (idb::IdbVia* via : port->get_via_list()) {
      if (!via) {
        continue;
      }
      idb::IdbLayerShape bsh = via->get_bottom_layer_shape();
      idb::IdbLayerShape tsh = via->get_top_layer_shape();
      idb::IdbLayerShape csh = via->get_cut_layer_shape();
      collectShapeList(&bsh, inst_x, inst_y, orient, master_w, master_h, order_to_idx, on_routing, on_cut);
      collectShapeList(&tsh, inst_x, inst_y, orient, master_w, master_h, order_to_idx, on_routing, on_cut);
      collectShapeList(&csh, inst_x, inst_y, orient, master_w, master_h, order_to_idx, on_routing, on_cut);
    }
  }
}

}  // namespace

std::vector<DiodeCandidateMaster> discoverDiodes(idb::IdbLayout* layout, const std::string& explicit_spec)
{
  std::vector<DiodeCandidateMaster> result;
  if (!layout || !layout->get_cell_master_list()) {
    return result;
  }
  auto* master_list = layout->get_cell_master_list();

  if (!explicit_spec.empty()) {
    for (const std::string& name : splitTokens(explicit_spec)) {
      if (auto cand = inspectMaster(layout, master_list->find_cell_master(name), false)) {
        result.push_back(std::move(*cand));
      } else {
        MJLOG.warn(Loc::current(), "Requested diode master is missing or invalid: ", name);
      }
    }
    return result;
  }

  for (idb::IdbCellMaster* master : master_list->get_cell_master()) {
    if (auto cand = inspectMaster(layout, master, true)) {
      result.push_back(std::move(*cand));
    }
  }

  std::sort(result.begin(), result.end(), [](const DiodeCandidateMaster& a, const DiodeCandidateMaster& b) {
    if (a.is_antenna_class != b.is_antenna_class) {
      return a.is_antenna_class > b.is_antenna_class;
    }
    if (a.name_hint != b.name_hint) {
      return a.name_hint > b.name_hint;
    }
    if (a.width != b.width) {
      return a.width < b.width;
    }
    if (std::abs(a.max_diff_area - b.max_diff_area) > kAntennaRepairEps) {
      return a.max_diff_area > b.max_diff_area;
    }
    return a.master->get_name() < b.master->get_name();
  });
  return result;
}

void extractDiodeGeometry(const DiodeCandidateMaster& dm, idb::IdbDesign* design, const PGSpecialNets& pg_nets,
                          int32_t inst_x, int32_t inst_y, idb::IdbOrient orient, int max_route_order,
                          const std::function<int(int)>& order_to_idx, CandidateDiodeGeometry& out)
{
  out.clear();
  if (!dm.master || !dm.signal_term) {
    return;
  }
  out.inst_x = inst_x;
  out.inst_y = inst_y;
  const uint32_t mw = dm.master->get_width();
  const uint32_t mh = dm.master->get_height();
  out.cell_bbox = transformMasterRect(idb::IdbRect(0, 0, static_cast<int32_t>(mw), static_cast<int32_t>(mh)), inst_x,
                                      inst_y, orient, mw, mh);

  collectPortGeometry(
      dm.signal_term->get_port_list(), inst_x, inst_y, orient, mw, mh, order_to_idx,
      [&](int order, int li, const idb::IdbRect& placed) {
        out.all_signal_metal_shapes.push_back({li, placed});
        if (order <= max_route_order && dm.layerCarriesDiff(order)) {
          out.start_shapes.push_back({li, placed});
        }
      },
      [&](int cut_order, const idb::IdbRect& placed) {
        out.all_signal_cut_shapes.push_back({cut_order, placed});
      });

  for (idb::IdbTerm* term : dm.master->get_term_list()) {
    if (!term || term == dm.signal_term) {
      continue;
    }
    idb::IdbSpecialNet* snet = resolvePGSpecialNet(design, term, pg_nets);
    const std::string& term_name = term->get_name();
    const bool is_power = term->is_power();
    const bool is_ground = term->is_ground();
    collectPortGeometry(
        term->get_port_list(), inst_x, inst_y, orient, mw, mh, order_to_idx,
        [&](int /*order*/, int li, const idb::IdbRect& placed) {
          out.pg_metal_shapes.push_back({li, placed, snet, term_name, is_power, is_ground});
          out.internal_obs.push_back({li, placed});
        },
        [&](int cut_order, const idb::IdbRect& placed) {
          out.pg_cut_shapes.push_back({cut_order, placed, snet, term_name, is_power, is_ground});
        });
  }

  idb::macro::MacroProperty* mprop = dm.master->get_macro_property();
  for (idb::IdbObs* obs : dm.master->get_obs_list()) {
    if (!obs) {
      continue;
    }
    for (idb::IdbObsLayer* ol : obs->get_obs_layer_list()) {
      if (!ol) {
        continue;
      }
      int32_t obs_sp = 0;
      if (mprop != nullptr && ol->get_shape() != nullptr && ol->get_shape()->get_layer() != nullptr) {
        if (auto* sp = mprop->find_obs_spacing(ol->get_shape()->get_layer())) {
          obs_sp = std::max<int32_t>(0, sp->get_min_spacing());
        }
      }
      collectShapeList(
          ol->get_shape(), inst_x, inst_y, orient, mw, mh, order_to_idx,
          [&](int /*order*/, int li, const idb::IdbRect& placed) {
            out.obs_metal_shapes.push_back({li, placed, obs_sp});
            out.internal_obs.push_back({li, placed, obs_sp});
          },
          [&](int cut_order, const idb::IdbRect& placed) {
            out.obs_cut_shapes.push_back({cut_order, placed});
          });
    }
  }
}

}  // namespace imj

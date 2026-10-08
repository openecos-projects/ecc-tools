#include "PyPlaceDB.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "IdbLayer.h"
#include "IdbLayout.h"
#include "IdbCellMaster.h"
#include "IdbDesign.h"
#include "IdbInstance.h"
#include "TimingExportAdapter.hpp"

namespace python_interface {
namespace {

using ista::TimingGraphArcSnapshot;
using ista::TimingLibArcSnapshot;
using ista::TimingLibCellSnapshot;
using ista::TimingLibPortSnapshot;
using ista::TimingLutSnapshot;
using ista::TimingNetArcSnapshot;
using ista::TimingPinSnapshot;
using ista::TimingSnapshot;

template <typename T>
void appendValues(pybind11::list& target, const std::vector<T>& values)
{
  for (const T& value : values) target.append(value);
}

void appendLut(pybind11::list& values, pybind11::list& axis1, pybind11::list& axis2, pybind11::list& dims, const TimingLutSnapshot& lut)
{
  pybind11::list value_row;
  appendValues(value_row, lut.values);
  values.append(value_row);
  pybind11::list first_axis;
  pybind11::list second_axis;
  if (!lut.axes.empty()) appendValues(first_axis, lut.axes[0]);
  if (lut.axes.size() > 1) appendValues(second_axis, lut.axes[1]);
  axis1.append(first_axis);
  axis2.append(second_axis);
  dims.append(pybind11::make_tuple(lut.axes.empty() ? 0 : lut.axes[0].size(), lut.axes.size() > 1 ? lut.axes[1].size() : 0));
}

int32_t mapId(const std::unordered_map<std::string, int32_t>& ids, const std::string& name)
{
  const auto it = ids.find(name);
  return it == ids.end() ? -1 : it->second;
}

}  // namespace

void PyPlaceDB::init_timing(const TimingSnapshot& snapshot, idm::DataManager* db)
{
  timing_schema_version = snapshot.schema_version;
  timing_source_time_unit_ps = snapshot.time_unit_ps;
  timing_source_capacitance_unit_pf = snapshot.capacitance_unit_pf;
  timing_source_resistance_unit_ohm = snapshot.resistance_unit_ohm;
  timing_unit_provenance = snapshot.timing_unit_provenance;
  timing_sdc_path = snapshot.sdc_path;
  timing_spef_path = snapshot.spef_path;
  timing_parasitics_initialization = snapshot.parasitics_initialization;
  appendValues(timing_diagnostics, snapshot.diagnostics);
  appendValues(timing_clock_names, snapshot.clock_names);
  for (const auto& clock : snapshot.clock_metadata) {
    pybind11::list waveform;
    appendValues(waveform, clock.waveform_ps);
    timing_clock_info.append(pybind11::make_tuple(clock.name, clock.period_ps, clock.rise_edge_ps, clock.fall_edge_ps,
                                                  clock.setup_uncertainty_ps, clock.hold_uncertainty_ps, waveform));
  }

  // DreamPlace's RC operator consumes physical per-micron units.  Preserve
  // Liberty/SPEF units above and derive those runtime values from LEF.
  c_unit = 0.16e-3;
  r_unit = 2.535;
  std::string rc_provenance = "fallback constants (LEF routing RC unavailable)";
  if (db != nullptr && db->get_idb_layout() != nullptr) {
    idb::IdbLayout* layout = db->get_idb_layout();
    const auto& routing_layers = layout->get_layers()->get_routing_layers();
    const double micron_dbu = layout->get_units()->get_micron_dbu();
    const std::string& signal_rc_layer = db->get_config().get_routing_layer_1st();
    for (idb::IdbLayer* layer : routing_layers) {
      if (!signal_rc_layer.empty() && layer->get_name() != signal_rc_layer)
        continue;
      auto* routing_layer = dynamic_cast<idb::IdbLayerRouting*>(layer);
      if (routing_layer == nullptr || micron_dbu <= 0.0 || routing_layer->get_width() <= 0) continue;
      const double width_um = static_cast<double>(routing_layer->get_width()) / micron_dbu;
      const double capacitance = routing_layer->get_capacitance();
      const double edge_capacitance = routing_layer->get_edge_capacitance();
      const double resistance = routing_layer->get_resistance();
      if (width_um <= 0.0 || capacitance < 0.0 || resistance < 0.0) continue;
      const double candidate_c_unit = capacitance * width_um + std::max(0.0, edge_capacitance) * 2.0;
      const double candidate_r_unit = resistance / width_um;
      if (!std::isfinite(candidate_c_unit) || !std::isfinite(candidate_r_unit) || candidate_c_unit <= 0.0 || candidate_r_unit <= 0.0) continue;
      c_unit = candidate_c_unit;
      r_unit = candidate_r_unit;
      rc_provenance = "LEF routing layer " + routing_layer->get_name() + " (pF/um, ohm/um)";
      break;
    }
  }
  timing_diagnostics.append("DreamPlace RC units: " + rc_provenance);

  std::unordered_map<std::string, int32_t> pin_ids;
  for (std::size_t index = 0; index < pin_names.size(); ++index) {
    pin_ids.emplace(pin_names[index].cast<std::string>(), static_cast<int32_t>(index));
  }
  std::unordered_map<std::string, int32_t> node_ids;
  for (std::size_t index = 0; index < node_names.size(); ++index) {
    node_ids.emplace(node_names[index].cast<std::string>(), static_cast<int32_t>(index));
  }
  std::unordered_map<std::string, int32_t> net_ids;
  for (std::size_t index = 0; index < net_names.size(); ++index) {
    net_ids.emplace(net_names[index].cast<std::string>(), static_cast<int32_t>(index));
  }

  std::unordered_map<std::string, int32_t> clock_pin_indices;

  std::unordered_map<std::string, TimingPinSnapshot> timing_pins;
  for (const TimingPinSnapshot& pin : snapshot.pins) timing_pins.emplace(pin.name, pin);
  for (const std::string& name : snapshot.clock_pins) {
    const int32_t id = mapId(pin_ids, name);
    if (id >= 0) {
      clock_pin_indices.emplace(name, static_cast<int32_t>(clock_pins.size()));
      clock_pins.append(id);
      clk_pin_names.append(name);
      const auto pin_it = timing_pins.find(name);
      const TimingPinSnapshot& pin = pin_it == timing_pins.end() ? TimingPinSnapshot{} : pin_it->second;
      clk_pin_r_aat.append(pin.max_rise_aat_ps);
      clk_pin_f_aat.append(pin.max_fall_aat_ps);
      clk_pin_rtran.append(pin.max_rise_slew_ps);
      clk_pin_ftran.append(pin.max_fall_slew_ps);
    }
  }
  for (const std::string& name : snapshot.ff_output_pins) {
    const int32_t id = mapId(pin_ids, name);
    if (id >= 0) FF_ids.append(id);
  }

  for (const std::string& name : snapshot.start_points) {
    const int32_t id = mapId(pin_ids, name);
    if (id < 0) {
      timing_diagnostics.append("Excluded timing start point absent from placement pins: " + name);
      continue;
    }
    start_points.append(id);
    const auto pin_it = timing_pins.find(name);
    const TimingPinSnapshot pin = pin_it == timing_pins.end() ? TimingPinSnapshot{} : pin_it->second;
    inrdelays.append(pin.max_rise_aat_ps);
    infdelays.append(pin.max_fall_aat_ps);
    inrtrans.append(pin.max_rise_slew_ps);
    inftrans.append(pin.max_fall_slew_ps);
  }
  for (const std::string& name : snapshot.end_points) {
    const int32_t id = mapId(pin_ids, name);
    if (id < 0) {
      timing_diagnostics.append("Excluded timing endpoint absent from placement pins: " + name);
      continue;
    }
    end_points.append(id);
    const auto pin_it = timing_pins.find(name);
    const TimingPinSnapshot pin = pin_it == timing_pins.end() ? TimingPinSnapshot{} : pin_it->second;
    outcaps.append(0.0);
    // Python subtracts setup at the current data slew. Restore the capture
    // deadline here; retain the native checked RAT in backend_endpoint_* below.
    endpoints_rRAT.append(pin.is_unconstrained_output ? 9.0e7 : pin.max_rise_rat_ps + pin.setup_check_time_ps);
    endpoints_fRAT.append(pin.is_unconstrained_output ? 9.0e7 : pin.max_fall_rat_ps + pin.setup_check_time_ps);
    backend_endpoint_rAAT.append(pin.max_rise_aat_ps);
    backend_endpoint_fAAT.append(pin.max_fall_aat_ps);
    backend_endpoint_rRAT.append(pin.max_rise_rat_ps);
    backend_endpoint_fRAT.append(pin.max_fall_rat_ps);
    backend_endpoint_rSlew.append(pin.max_rise_slew_ps);
    backend_endpoint_fSlew.append(pin.max_fall_slew_ps);
    backend_endpoint_min_rAAT.append(pin.min_rise_aat_ps);
    backend_endpoint_min_fAAT.append(pin.min_fall_aat_ps);
    backend_endpoint_min_rRAT.append(pin.min_rise_rat_ps);
    backend_endpoint_min_fRAT.append(pin.min_fall_rat_ps);
  }
  for (std::size_t pin_id = 0; pin_id < pin_names.size(); ++pin_id) {
    pin_2_libpin_offset.append(-1);
  }
  for (std::size_t node_id = 0; node_id < node_names.size(); ++node_id) {
    inst_main_id.append(-1);
    inst_libcell_offset.append(0);
  }

  std::unordered_map<std::string, int32_t> cell_ids;
  auto* physical_masters = db == nullptr || db->get_idb_layout() == nullptr
                               ? nullptr : db->get_idb_layout()->get_cell_master_list();
  for (const TimingLibCellSnapshot& cell : snapshot.lib_cells) {
    const int32_t id = static_cast<int32_t>(cell_ids.size());
    cell_ids.emplace(cell.name, id);
    flat_libcell_names.append(cell.name);
    flat_libcell_info.append(pybind11::make_tuple(cell.name, cell.main_id, cell.size_index, cell.vt_index));
    idb::IdbCellMaster* master = physical_masters == nullptr ? nullptr : physical_masters->find_cell_master(cell.name);
    flat_libcell_width.append(master == nullptr ? 0.0 : static_cast<double>(master->get_width()));
    flat_libcell_height.append(master == nullptr ? 0.0 : static_cast<double>(master->get_height()));
    flat_libcell_leakage.append(cell.leakage);
  }
  main_id_2_cell_id_start.append(0);
  for (std::size_t index = 1; index < snapshot.lib_cells.size(); ++index) {
    if (snapshot.lib_cells[index].main_id != snapshot.lib_cells[index - 1].main_id) {
      main_id_2_cell_id_start.append(index);
    }
  }
  if (!snapshot.lib_cells.empty()) main_id_2_cell_id_start.append(snapshot.lib_cells.size());
  int32_t sizeable_families = 0;
  for (std::size_t main_id = 0; main_id + 1 < main_id_2_cell_id_start.size(); ++main_id) {
    const auto begin = main_id_2_cell_id_start[main_id].cast<std::size_t>();
    const auto end = main_id_2_cell_id_start[main_id + 1].cast<std::size_t>();
    int32_t size_limit = 1;
    int32_t vt_limit = 1;
    uint32_t row_height = 0;
    bool physical_family = true;
    bool buffer_family = true;
    for (std::size_t cell_id = begin; cell_id < end; ++cell_id) {
      const auto& cell = snapshot.lib_cells[cell_id];
      buffer_family = buffer_family && cell.is_buffer;
      auto* master = physical_masters == nullptr ? nullptr : physical_masters->find_cell_master(cell.name);
      if (master == nullptr || master->get_width() <= 0 || master->get_height() <= 0
          || (row_height != 0 && master->get_height() != row_height)) {
        physical_family = false;
      }
      if (master != nullptr) row_height = master->get_height();
      size_limit = std::max(size_limit, cell.size_index);
      vt_limit = std::max(vt_limit, cell.vt_index + 1);
    }
    const bool sizeable = physical_family && end - begin > 1 && (size_limit > 1 || vt_limit > 1);
    main_id_is_sizeable.append(sizeable);
    if (physical_family && buffer_family) buffer_main_type_candidate_indices.append(main_id);
    sizeable_families += sizeable;
    flat_libcell_main_id2size_vt_limit.append(
        pybind11::make_tuple(snapshot.lib_cells[begin].name, size_limit, vt_limit));
  }
  if (buffer_main_type_candidate_indices.size() == 1) {
    buffer_main_type_index = buffer_main_type_candidate_indices[0].cast<int32_t>();
    buffer_main_type_status = "ok";
  } else {
    buffer_main_type_status = buffer_main_type_candidate_indices.empty()
                                  ? "unsupported_no_buffer_family"
                                  : "unsupported_multiple_buffer_families";
  }
  timing_diagnostics.append("Liberty families: " + std::to_string(main_id_is_sizeable.size())
                            + ", physically sizeable: " + std::to_string(sizeable_families));

  std::unordered_map<std::string, int32_t> instance_cells;
  if (db != nullptr && db->get_idb_design() != nullptr) {
    for (idb::IdbInstance* instance : db->get_idb_design()->get_instance_list()->get_instance_list()) {
      if (instance == nullptr || instance->get_cell_master() == nullptr) continue;
      const int32_t node_id = mapId(node_ids, instance->get_name());
      const int32_t cell_id = mapId(cell_ids, instance->get_cell_master()->get_name());
      if (node_id < 0 || cell_id < 0) continue;
      const int32_t main_id = snapshot.lib_cells[cell_id].main_id;
      inst_main_id[static_cast<std::size_t>(node_id)] = main_id;
      inst_libcell_offset[static_cast<std::size_t>(node_id)]
          = cell_id - main_id_2_cell_id_start[main_id].cast<int32_t>();
      instance_cells.emplace(instance->get_name(), cell_id);
    }
  }
  for (std::size_t pin_id = 0; pin_id < pin_names.size(); ++pin_id) {
    const std::string name = pin_names[pin_id].cast<std::string>();
    const auto timing_pin = timing_pins.find(name);
    if (timing_pin == timing_pins.end()) continue;
    const auto instance = instance_cells.find(timing_pin->second.instance_name);
    if (instance == instance_cells.end()) continue;
    const auto& ports = snapshot.lib_cells[instance->second].ports;
    const std::string port_name = name.substr(name.rfind(':') + 1);
    for (std::size_t offset = 0; offset < ports.size(); ++offset) {
      if (ports[offset].name == port_name) {
        pin_2_libpin_offset[pin_id] = static_cast<int32_t>(offset);
        break;
      }
    }
  }

  cell_id_2_arc_id_start.append(0);
  cell_id_2_libpin_id_start.append(0);
  int32_t flat_arc_id = 0;
  int32_t flat_pin_id = 0;
  int32_t missing_physical_pin_shapes = 0;
  for (const TimingLibCellSnapshot& cell : snapshot.lib_cells) {
    auto* master = physical_masters == nullptr ? nullptr : physical_masters->find_cell_master(cell.name);
    for (const TimingLibPortSnapshot& port : cell.ports) {
      auto* term = master == nullptr ? nullptr : master->findTerm(port.name);
      auto* box = term == nullptr || term->get_port_number() == 0 ? nullptr : term->get_bounding_box();
      flat_lib_pin_offset_x.append(box == nullptr ? 0 : (box->get_low_x() + box->get_high_x()) / 2);
      flat_lib_pin_offset_y.append(box == nullptr ? 0 : (box->get_low_y() + box->get_high_y()) / 2);
      missing_physical_pin_shapes += box == nullptr;
      flat_lib_pin_cap.append(port.capacitance_pf);
      flat_lib_pin_rcap.append(port.rise_capacitance_pf);
      flat_lib_pin_fcap.append(port.fall_capacitance_pf);
      flat_lib_pin_cap_limit.append(port.max_capacitance_pf);
      flat_lib_pin_slew_limit.append(port.max_slew_ps);
      ++flat_pin_id;
    }
    int32_t local_arc_id = 0;
    for (const TimingLibArcSnapshot& arc : cell.arcs) {
      flat_libarc_info.append(pybind11::make_tuple(arc.source_port, arc.sink_port, cell_ids[cell.name], local_arc_id, arc.sense, arc.trigger));
      appendLut(f_delay_flat_luts_values, f_delay_flat_luts_trans_table, f_delay_flat_luts_cap_table, f_delay_flat_luts_dim, arc.fall_delay);
      appendLut(r_delay_flat_luts_values, r_delay_flat_luts_trans_table, r_delay_flat_luts_cap_table, r_delay_flat_luts_dim, arc.rise_delay);
      appendLut(f_trans_flat_luts_values, f_trans_flat_luts_trans_table, f_trans_flat_luts_cap_table, f_trans_flat_luts_dim, arc.fall_slew);
      appendLut(r_trans_flat_luts_values, r_trans_flat_luts_trans_table, r_trans_flat_luts_cap_table, r_trans_flat_luts_dim, arc.rise_slew);
      ++flat_arc_id;
      ++local_arc_id;
    }
    cell_id_2_arc_id_start.append(flat_arc_id);
    cell_id_2_libpin_id_start.append(flat_pin_id);
  }
  timing_diagnostics.append("Liberty pin geometry: LEF term bounding-box centers in DBU; "
                            + std::to_string(missing_physical_pin_shapes) + " ports without physical shapes use (0,0)");
  auto lutId = [&](int32_t cell_id, int32_t local_arc_id) -> int32_t {
    if (cell_id < 0 || local_arc_id < 0) return -1;
    return cell_id_2_arc_id_start[cell_id].cast<int32_t>() + local_arc_id;
  };

  auto appendEndpointArc = [&](pybind11::list& target, const ista::TimingEndpointArcSnapshot& arc) {
    pybind11::list row;
    row.append(mapId(pin_ids, arc.source_pin));
    row.append(mapId(pin_ids, arc.sink_pin));
    row.append(mapId(cell_ids, arc.library_cell));
    row.append(lutId(mapId(cell_ids, arc.library_cell), arc.library_arc_id));
    row.append(arc.sense);
    row.append(arc.timing_type);
    row.append(arc.check_type);
    row.append(arc.library_arc_id);
    target.append(row);
  };
  auto appendConstraintArc = [&](const ista::TimingEndpointArcSnapshot& arc) {
    const int32_t clock_index = mapId(clock_pin_indices, arc.source_pin);
    const int32_t sink_pin = mapId(pin_ids, arc.sink_pin);
    if (clock_index < 0 || sink_pin < 0) {
      timing_diagnostics.append("constraint arc missing clock-local source or sink pin");
      return;
    }
    pybind11::list row;
    row.append(clock_index);
    row.append(sink_pin);
    row.append(mapId(cell_ids, arc.library_cell));
    row.append(lutId(mapId(cell_ids, arc.library_cell), arc.library_arc_id));
    row.append(arc.sense);
    row.append(arc.timing_type);
    row.append(arc.check_type);
    row.append(arc.library_arc_id);
    endpoints_constraint_arcs.append(row);
  };
  for (const auto& arc : snapshot.constraint_arcs) appendConstraintArc(arc);
  for (const auto& arc : snapshot.timing_check_arcs) appendEndpointArc(endpoints_timing_check_arcs, arc);

  std::vector<std::vector<std::pair<int32_t, int32_t>>> net_rows(net_names.size());
  for (const TimingNetArcSnapshot& arc : snapshot.net_arcs) {
    const int32_t source = mapId(pin_ids, arc.source_pin);
    const int32_t sink = mapId(pin_ids, arc.sink_pin);
    if (source < 0 || sink < 0) continue;
    const auto pin_it = timing_pins.find(arc.source_pin);
    const int32_t net_id = pin_it == timing_pins.end() ? -1 : mapId(net_ids, pin_it->second.net_name);
    if (net_id >= 0 && static_cast<std::size_t>(net_id) < net_rows.size()) net_rows[net_id].emplace_back(source, sink);
  }
  int32_t net_arc_count = 0;
  net_flat_arcs_start.append(0);
  net2driver_pin_map = pybind11::list();
  for (std::size_t net_id = 0; net_id < net_rows.size(); ++net_id) {
    int32_t driver = -1;
    if (!net_rows[net_id].empty()) driver = net_rows[net_id].front().first;
    net2driver_pin_map.append(driver);
    for (const auto& [source, sink] : net_rows[net_id]) {
      net_flat_arcs.append(pybind11::make_tuple(source, sink));
      ++net_arc_count;
    }
    net_flat_arcs_start.append(net_arc_count);
  }

  std::vector<std::vector<int32_t>> incoming(pin_names.size());
  std::vector<std::vector<int32_t>> outgoing(pin_names.size());
  std::vector<std::vector<int32_t>> incoming_arc(pin_names.size());
  std::vector<std::vector<int32_t>> outgoing_arc(pin_names.size());
  std::vector<std::vector<int32_t>> inst_arc_ids(node_names.size());
  std::vector<std::vector<TimingGraphArcSnapshot>> levels;
  for (const TimingGraphArcSnapshot& arc : snapshot.graph_arcs) {
    const int32_t source = mapId(pin_ids, arc.source_pin);
    const int32_t sink = mapId(pin_ids, arc.sink_pin);
    if (source < 0 || sink < 0) continue;
    arc_src_pin.append(source);
    arc_dst_pin.append(sink);
    const int32_t cell_id = cell_ids.contains(arc.library_cell) ? cell_ids.at(arc.library_cell) : -1;
    arc_libcell_id.append(cell_id);
    arc_libarc_id.append(lutId(cell_id, arc.library_arc_id));
    arc_inst_id.append(mapId(node_ids, arc.owner_name));
    arc_sense.append(arc.sense);
    arc_type.append(arc.timing_type);
    arc_offset.append(arc.library_arc_id);
    if (arc.arc_type == 1) {
      const int32_t owner_id = mapId(node_ids, arc.owner_name);
      if (owner_id < 0 || static_cast<std::size_t>(owner_id) >= inst_arc_ids.size()) {
        timing_diagnostics.append("cell arc omitted from instance CSR because its owner is not a PyPlaceDB node");
        continue;
      }
      if (static_cast<std::size_t>(arc.level) >= levels.size()) levels.resize(arc.level + 1);
      levels[arc.level].push_back(arc);
    }
  }
  arc_level_start.append(0);
  flat_inst_arcs_by_level_start.append(0);
  inst_topo_start.append(0);
  for (const auto& level : levels) {
    std::unordered_set<int32_t> level_nodes;
    for (const auto& arc : level) {
      const int32_t source = mapId(pin_ids, arc.source_pin);
      const int32_t sink = mapId(pin_ids, arc.sink_pin);
      const int32_t cell_id = cell_ids.contains(arc.library_cell) ? cell_ids.at(arc.library_cell) : -1;
      const int32_t arc_source = arc.is_clock_arc ? mapId(clock_pin_indices, arc.source_pin) : source;
      const int32_t owner_id = mapId(node_ids, arc.owner_name);
      const int32_t level_arc_id = static_cast<int32_t>(flat_inst_arcs_by_level.size());
      if (!arc.is_clock_arc && source >= 0 && sink >= 0) {
        outgoing[source].push_back(sink);
        outgoing_arc[source].push_back(level_arc_id);
        incoming[sink].push_back(source);
        incoming_arc[sink].push_back(level_arc_id);
      }
      if (owner_id >= 0) {
        inst_arc_ids[owner_id].push_back(level_arc_id);
        if (level_nodes.insert(owner_id).second) inst_topo_ids.append(owner_id);
      }
      flat_inst_arcs_by_level.append(pybind11::make_tuple(arc_source, sink, cell_id, lutId(cell_id, arc.library_arc_id), arc.sense, arc.timing_type,
                                                          arc.library_arc_id, owner_id));
      ++flat_arc_id;
    }
    flat_inst_arcs_by_level_start.append(flat_inst_arcs_by_level.size());
    arc_level_start.append(flat_inst_arcs_by_level.size());
    inst_topo_start.append(inst_topo_ids.size());
  }
  if (levels.empty()) {
    flat_inst_arcs_by_level_start = pybind11::list();
    flat_inst_arcs_by_level_start.append(0);
    arc_level_start = pybind11::list();
    arc_level_start.append(0);
  }
  inst_flat_arcs_start.append(0);
  for (const auto& ids : inst_arc_ids) {
    appendValues(inst_flat_arcs, ids);
    inst_flat_arcs_start.append(inst_flat_arcs.size());
  }

  for (const auto& rows : net_rows) {
    for (const auto& [source, sink] : rows) {
      outgoing[source].push_back(sink);
      outgoing_arc[source].push_back(-1);
      incoming[sink].push_back(source);
      incoming_arc[sink].push_back(-1);
    }
  }

  flat_pin_to_graph_start.append(0);
  flat_pin_to_graph_start_reverse.append(0);
  pin_pred_start.append(0);
  pin_succ_start.append(0);
  pin_to_inst_id = pybind11::list();
  pin_to_node_id = pybind11::list();
  for (std::size_t pin_id = 0; pin_id < pin_names.size(); ++pin_id) {
    for (int32_t value : outgoing[pin_id]) flat_pin_to_graph.append(value);
    for (int32_t value : incoming[pin_id]) flat_pin_to_graph_reverse.append(value);
    for (int32_t value : incoming[pin_id]) pin_pred_pin.append(value);
    for (int32_t value : incoming_arc[pin_id]) pin_pred_arc_id.append(value);
    for (int32_t value : outgoing[pin_id]) pin_succ_pin.append(value);
    for (int32_t value : outgoing_arc[pin_id]) pin_succ_arc_id.append(value);
    flat_pin_to_graph_start.append(flat_pin_to_graph.size());
    flat_pin_to_graph_start_reverse.append(flat_pin_to_graph_reverse.size());
    pin_pred_start.append(pin_pred_pin.size());
    pin_succ_start.append(pin_succ_pin.size());
    const std::string name = pin_names[pin_id].cast<std::string>();
    const std::size_t separator = name.find(':');
    const std::string owner = separator == std::string::npos ? name : name.substr(0, separator);
    pin_to_inst_id.append(mapId(node_ids, owner));
    pin_to_node_id.append(mapId(node_ids, owner));
  }
  endpoint_pin_ids = end_points;
  start_pin_ids = start_points;

  for (std::size_t i = 0; i < pin_names.size(); ++i) {
    const auto pin_it = timing_pins.find(pin_names[i].cast<std::string>());
    const TimingPinSnapshot pin = pin_it == timing_pins.end() ? TimingPinSnapshot{} : pin_it->second;
    if (pin_it == timing_pins.end()) timing_diagnostics.append("PyPlaceDB pin missing from timing snapshot");
    if (i >= pin2net_map.size()) pin2net_map.append(mapId(net_ids, pin.net_name));
  }
}

}  // namespace python_interface

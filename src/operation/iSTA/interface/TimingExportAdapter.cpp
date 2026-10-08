#include "TimingExportAdapter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "DataManager.hpp"
#include "STAInterface.hpp"
#include "advance/Arc.hpp"
#include "advance/ArcType.hpp"
#include "advance/AnalysisType.hpp"
#include "advance/Database.hpp"
#include "advance/Instance.hpp"
#include "advance/Net.hpp"
#include "advance/Pin.hpp"
#include "advance/TimingArc.hpp"
#include "advance/TimingArcSense.hpp"
#include "advance/TimingCell.hpp"
#include "advance/TimingCellArc.hpp"
#include "advance/TimingCellPort.hpp"
#include "advance/TimingCheckArc.hpp"
#include "advance/TimingCheckType.hpp"
#include "advance/TimingLibrary.hpp"
#include "advance/TimingPoint.hpp"
#include "advance/TimingTimeUnit.hpp"
#include "advance/TransType.hpp"
#include "LibertyExportAdapter.hpp"

namespace ista {
namespace {

constexpr double kUnconstrainedArrivalPs = -9.0e7;
constexpr double kUnconstrainedRequiredPs = 9.0e7;

double toPs(double value, TimingTimeUnit unit)
{
  switch (unit) {
    case TimingTimeUnit::kPS:
      return value;
    case TimingTimeUnit::kFS:
      return value * 1.0e-3;
    case TimingTimeUnit::kNS:
      return value * 1.0e3;
    default:
      return value * 1.0e3;
  }
}

double toPf(double value, TimingCapacitiveUnit unit)
{
  switch (unit) {
    case TimingCapacitiveUnit::kPF:
      return value;
    case TimingCapacitiveUnit::kFF:
      return value * 1.0e-3;
    case TimingCapacitiveUnit::kF:
      return value * 1.0e12;
    default:
      return value;
  }
}

double toOhm(double value, TimingResistanceUnit unit)
{
  return unit == TimingResistanceUnit::kkOHM ? value * 1.0e3 : value;
}

int32_t transCode(TransType trans)
{
  return trans == TransType::kRise ? 1 : trans == TransType::kFall ? -1 : 0;
}

int32_t senseCode(TimingArcSense sense)
{
  switch (sense) {
    case TimingArcSense::kPositive:
      return 1;
    case TimingArcSense::kNegative:
      return -1;
    case TimingArcSense::kNonUnate:
      return 0;
    default:
      return 0;
  }
}

double mapValue(const std::map<AnalysisType, std::map<TransType, double>>& values, AnalysisType analysis, TransType trans,
                double fallback)
{
  const auto analysis_it = values.find(analysis);
  if (analysis_it == values.end()) return fallback;
  const auto trans_it = analysis_it->second.find(trans);
  return trans_it == analysis_it->second.end() ? fallback : trans_it->second;
}

void addDiagnostic(TimingSnapshot& snapshot, std::string message)
{
  if (std::find(snapshot.diagnostics.begin(), snapshot.diagnostics.end(), message) == snapshot.diagnostics.end()) {
    snapshot.diagnostics.push_back(std::move(message));
  }
}

double normalizedTime(double value, TimingTimeUnit unit, double fallback, bool& used_fallback)
{
  const double converted = toPs(value, unit);
  if (!std::isfinite(converted)) {
    used_fallback = true;
    return fallback;
  }
  return converted;
}

const char* timeUnitName(TimingTimeUnit unit)
{
  switch (unit) {
    case TimingTimeUnit::kPS:
      return "ps";
    case TimingTimeUnit::kFS:
      return "fs";
    case TimingTimeUnit::kNS:
      return "ns";
    default:
      return "unknown";
  }
}

const char* capUnitName(TimingCapacitiveUnit unit)
{
  switch (unit) {
    case TimingCapacitiveUnit::kPF:
      return "pF";
    case TimingCapacitiveUnit::kFF:
      return "fF";
    case TimingCapacitiveUnit::kF:
      return "F";
    default:
      return "unknown";
  }
}

const char* resistanceUnitName(TimingResistanceUnit unit)
{
  return unit == TimingResistanceUnit::kkOHM ? "kOhm" : "Ohm";
}

}  // namespace

TimingSnapshot TimingExportAdapter::exportSnapshot()
{
  if (!STAI.isTimingReady()) {
    throw std::runtime_error("timing export requires initialized and executed current-main STA");
  }

  Database& database = STADM.getDatabase();
  TimingSnapshot snapshot;
  TimingLibrary& library = database.get_timing_library();
  const TimingTimeUnit time_unit = library.get_time_unit();
  const TimingCapacitiveUnit cap_unit = library.get_cap_unit();
  const TimingResistanceUnit resistance_unit = library.get_resistance_unit();
  snapshot.time_unit_ps = toPs(1.0, time_unit);
  snapshot.capacitance_unit_pf = toPf(1.0, cap_unit);
  snapshot.resistance_unit_ohm = toOhm(1.0, resistance_unit);
  snapshot.timing_unit_provenance = std::string("liberty time=") + timeUnitName(time_unit) + ", cap=" + capUnitName(cap_unit)
                                    + ", resistance=" + resistanceUnitName(resistance_unit);
  snapshot.sdc_path = database.get_timing_constraint().get_sdc_file_path();
  snapshot.spef_path = database.get_parasitic_library().get_spef_file_path();
  snapshot.parasitic_capacitance_unit = database.get_parasitic_library().get_capacitive_unit();
  snapshot.parasitic_resistance_unit = database.get_parasitic_library().get_resistance_unit();
  snapshot.timing_corner = STADM.getConfig().timing_corner;
  snapshot.parasitics_initialization = snapshot.spef_path.empty() ? "liberty_or_default" : "spef";
  if (!snapshot.parasitic_capacitance_unit.empty() || !snapshot.parasitic_resistance_unit.empty()) {
    snapshot.timing_unit_provenance += ", spef cap=" + snapshot.parasitic_capacitance_unit + ", spef resistance="
                                       + snapshot.parasitic_resistance_unit;
  }

  bool used_non_finite_fallback = false;
  bool used_missing_timing_point_fallback = false;
  bool used_min_analysis_fallback = false;
  std::unordered_map<std::string, int32_t> pin_ids;
  pin_ids.reserve(database.get_pin_map().size());
  for (auto& [name, pin] : database.get_pin_map()) {
    pin_ids.emplace(name, static_cast<int32_t>(snapshot.pins.size()));
    TimingPinSnapshot exported;
    exported.name = name;
    exported.net_name = pin.get_net_name();
    exported.instance_name = pin.get_instance_name();
    exported.direction = static_cast<int32_t>(pin.get_direction());
    exported.is_port = pin.get_is_port();
    if (exported.is_port && pin.get_direction() == PinDirection::kOutput) {
      const auto& port_constraints = database.get_timing_constraint().get_port_constraint_map();
      const auto constraint = port_constraints.find(name);
      exported.is_unconstrained_output = constraint == port_constraints.end() || !constraint->second.get_has_output_delay_max();
    }
    exported.max_rise_rat_ps = kUnconstrainedRequiredPs;
    exported.max_fall_rat_ps = kUnconstrainedRequiredPs;
    exported.min_rise_rat_ps = kUnconstrainedRequiredPs;
    exported.min_fall_rat_ps = kUnconstrainedRequiredPs;
    const auto point_it = database.get_timing_point_map().find(name);
    if (point_it != database.get_timing_point_map().end()) {
      TimingPoint& point = point_it->second;
      exported.level = point.get_level();
      exported.max_rise_aat_ps = normalizedTime(point.get_arrival(), time_unit, kUnconstrainedArrivalPs, used_non_finite_fallback);
      exported.max_fall_aat_ps = exported.max_rise_aat_ps;
      exported.max_rise_rat_ps = normalizedTime(point.get_required(), time_unit, kUnconstrainedRequiredPs, used_non_finite_fallback);
      exported.max_fall_rat_ps = exported.max_rise_rat_ps;
      exported.setup_check_time_ps = normalizedTime(point.get_setup_check_time(), time_unit, 0.0, used_non_finite_fallback);
      exported.max_rise_slew_ps = normalizedTime(mapValue(point.get_data_slew_map(), AnalysisType::kMax, TransType::kRise, 0.0), time_unit,
                                                 0.0, used_non_finite_fallback);
      exported.max_fall_slew_ps = normalizedTime(mapValue(point.get_data_slew_map(), AnalysisType::kMax, TransType::kFall, 0.0), time_unit,
                                                 0.0, used_non_finite_fallback);
      exported.min_rise_slew_ps = normalizedTime(mapValue(point.get_data_slew_map(), AnalysisType::kMin, TransType::kRise, 0.0), time_unit,
                                                 0.0, used_non_finite_fallback);
      exported.min_fall_slew_ps = normalizedTime(mapValue(point.get_data_slew_map(), AnalysisType::kMin, TransType::kFall, 0.0), time_unit,
                                                 0.0, used_non_finite_fallback);
      // TimingPoint keeps the effective max path scalar.  The min analysis
      // state is exported explicitly when available; otherwise the fallback
      // is recorded instead of silently pretending to have a min analysis.
      exported.min_rise_aat_ps = exported.max_rise_aat_ps;
      exported.min_fall_aat_ps = exported.max_fall_aat_ps;
      exported.min_rise_rat_ps = exported.max_rise_rat_ps;
      exported.min_fall_rat_ps = exported.max_fall_rat_ps;
      used_min_analysis_fallback = true;
    } else {
      used_missing_timing_point_fallback = true;
    }
    snapshot.pins.push_back(std::move(exported));
  }

  snapshot.lib_cells = LibertyExportAdapter::exportCells(library, time_unit, cap_unit);
  std::map<std::string, int32_t> cell_ids;
  for (std::size_t cell_id = 0; cell_id < snapshot.lib_cells.size(); ++cell_id) {
    cell_ids.emplace(snapshot.lib_cells[cell_id].name, static_cast<int32_t>(cell_id));
  }

  // Pin capacitance belongs to the instance's Liberty port, not to the
  // timing-point scalar.  Resolve it after the Liberty snapshot is built so
  // PyPlaceDB endpoint outcaps use the same normalized pF convention.
  for (auto& exported_pin : snapshot.pins) {
    if (exported_pin.instance_name.empty()) {
      continue;
    }
    const auto pin_it = database.get_pin_map().find(exported_pin.name);
    const auto instance_it = database.get_instance_map().find(exported_pin.instance_name);
    if (pin_it == database.get_pin_map().end() || instance_it == database.get_instance_map().end()) {
      continue;
    }
    const auto cell_it = library.get_cell_map().find(instance_it->second.get_cell_name());
    if (cell_it == library.get_cell_map().end()) {
      continue;
    }
    const auto port_it = cell_it->second.get_port_map().find(pin_it->second.get_pin_name());
    if (port_it == cell_it->second.get_port_map().end()) {
      continue;
    }
    exported_pin.capacitance_pf = port_it->second.get_capacitance();
    exported_pin.rise_capacitance_pf = exported_pin.capacitance_pf;
    exported_pin.fall_capacitance_pf = exported_pin.capacitance_pf;
    auto& caps = port_it->second.get_trans_capacitance_map();
    if (const auto it = caps.find(AnalysisType::kMax); it != caps.end()) {
      if (const auto rise = it->second.find(TransType::kRise); rise != it->second.end()) exported_pin.rise_capacitance_pf = rise->second;
      if (const auto fall = it->second.find(TransType::kFall); fall != it->second.end()) exported_pin.fall_capacitance_pf = fall->second;
    }
  }

  for (auto& [instance_name, instance] : database.get_instance_map()) {
    if (!instance.get_is_sequential()) continue;
    if (!instance.get_clock_pin_name().empty()) snapshot.clock_pins.push_back(instance.get_clock_pin_name());
    if (!instance.get_output_pin_name().empty()) snapshot.ff_output_pins.push_back(instance.get_output_pin_name());
  }
  for (auto& [clock_name, clock] : database.get_timing_constraint().get_clock_map()) {
    snapshot.clock_names.push_back(clock_name);
    TimingClockSnapshot exported_clock;
    exported_clock.name = clock_name;
    exported_clock.period_ps = normalizedTime(clock.get_period(), time_unit, 0.0, used_non_finite_fallback);
    exported_clock.rise_edge_ps = normalizedTime(clock.get_rise_edge(), time_unit, 0.0, used_non_finite_fallback);
    exported_clock.fall_edge_ps = normalizedTime(clock.get_fall_edge(), time_unit, 0.0, used_non_finite_fallback);
    exported_clock.setup_uncertainty_ps = normalizedTime(clock.get_setup_uncertainty(), time_unit, 0.0, used_non_finite_fallback);
    exported_clock.hold_uncertainty_ps = normalizedTime(clock.get_hold_uncertainty(), time_unit, 0.0, used_non_finite_fallback);
    for (double edge : clock.get_waveform()) {
      exported_clock.waveform_ps.push_back(normalizedTime(edge, time_unit, 0.0, used_non_finite_fallback));
    }
    snapshot.clock_metadata.push_back(std::move(exported_clock));
  }
  snapshot.start_points = database.get_start_point_list();
  snapshot.end_points = database.get_end_point_list();

  for (Arc& arc : database.get_arc_list()) {
    const auto source_it = pin_ids.find(arc.get_source_pin());
    const auto sink_it = pin_ids.find(arc.get_sink_pin());
    if (source_it == pin_ids.end() || sink_it == pin_ids.end()) {
      addDiagnostic(snapshot, "unsupported arc with missing pin identity");
      continue;
    }
    TimingGraphArcSnapshot exported;
    exported.source_pin = arc.get_source_pin();
    exported.sink_pin = arc.get_sink_pin();
    exported.owner_name = arc.get_owner_name();
    exported.arc_type = arc.get_type() == ArcType::kCell ? 1 : arc.get_type() == ArcType::kNet ? 2 : 0;
    exported.is_clock_arc = arc.get_is_clock_arc();
    exported.delay_max_ps = normalizedTime(arc.get_delay_max(), time_unit, 0.0, used_non_finite_fallback);
    exported.delay_min_ps = normalizedTime(arc.get_delay_min(), time_unit, 0.0, used_non_finite_fallback);
    const auto sink_point_it = database.get_timing_point_map().find(arc.get_sink_pin());
    exported.level = exported.is_clock_arc ? 0 : sink_point_it == database.get_timing_point_map().end() ? 0 : sink_point_it->second.get_level();
    const auto owner_it = database.get_instance_map().find(arc.get_owner_name());
    if (!arc.get_owner_name().empty() && owner_it != database.get_instance_map().end()) {
      exported.library_cell = owner_it->second.get_cell_name();
    } else if (arc.get_type() == ArcType::kCell && !arc.get_owner_name().empty()) {
      addDiagnostic(snapshot, "unsupported arc owner identity: " + arc.get_owner_name());
    }
    if (arc.get_timing_cell_arc() != nullptr) {
      TimingCellArc* cell_arc = arc.get_timing_cell_arc();
      const auto cell_it = cell_ids.find(exported.library_cell);
      if (cell_it != cell_ids.end()) {
        const auto& cell = snapshot.lib_cells.at(cell_it->second);
        auto library_cell = library.get_cell_map().find(exported.library_cell);
        std::size_t index = 0;
        bool mapped = false;
        if (library_cell != library.get_cell_map().end()) {
          for (TimingCellArc& candidate : library_cell->second.get_cell_arc_list()) {
            if (&candidate == cell_arc) {
              for (TimingArc& timing_arc : candidate.get_timing_arc_list()) {
                if (index >= cell.arcs.size()) break;
                TimingGraphArcSnapshot variant = exported;
                variant.library_arc_id = static_cast<int32_t>(index++);
                variant.sense = senseCode(timing_arc.get_sense());
                variant.timing_type = transCode(timing_arc.get_trigger_trans_type());
                snapshot.graph_arcs.push_back(std::move(variant));
                mapped = true;
              }
              break;
            }
            index += candidate.get_timing_arc_list().size();
          }
        }
        if (mapped) continue;
        addDiagnostic(snapshot, "unmapped instance timing arc: " + exported.owner_name);
      }
    }
    snapshot.graph_arcs.push_back(std::move(exported));
  }

  // iSTA launches sequential data from CK, while DreamPlace initializes Q
  // through level-0 clock-to-Q arcs and treats Q as the launch startpoint.
  std::unordered_map<std::string, std::string> clock_outputs;
  for (const auto& arc : snapshot.graph_arcs) {
    if (arc.is_clock_arc && arc.library_arc_id >= 0) clock_outputs.emplace(arc.source_pin, arc.sink_pin);
  }
  for (std::string& name : snapshot.start_points) {
    const auto it = clock_outputs.find(name);
    if (it != clock_outputs.end()) name = it->second;
  }

  for (auto& [net_name, net] : database.get_net_map()) {
    const std::string driver = net.get_driver_pin().empty() ? (net.get_driver_pin_list().empty() ? "" : net.get_driver_pin_list().front()) : net.get_driver_pin();
    if (driver.empty()) {
      addDiagnostic(snapshot, "net without a resolved driver: " + net_name);
      continue;
    }
    for (const std::string& load : net.get_load_pin_list()) {
      if (load != driver) snapshot.net_arcs.push_back({driver, load});
    }
  }

  for (auto& [instance_name, instance] : database.get_instance_map()) {
    const auto cell_it = cell_ids.find(instance.get_cell_name());
    if (cell_it == cell_ids.end()) continue;
    for (TimingCheckArc& check_arc : instance.get_check_arc_list()) {
      const std::string& source_pin = check_arc.get_clock_port();
      const std::string& sink_pin = check_arc.get_data_port();
      if (!pin_ids.contains(source_pin) || !pin_ids.contains(sink_pin)) {
        addDiagnostic(snapshot, "timing check arc with missing pin identity");
        continue;
      }
      for (TimingArc& timing_arc : check_arc.get_timing_arc_list()) {
        TimingEndpointArcSnapshot exported;
        exported.source_pin = source_pin;
        exported.sink_pin = sink_pin;
        exported.library_cell = instance.get_cell_name();
        if (check_arc.get_check_type() == TimingCheckType::kSetup) {
          const auto cell_id = cell_ids.find(instance.get_cell_name());
          if (cell_id != cell_ids.end()) {
            const auto& arcs = snapshot.lib_cells[cell_id->second].arcs;
            const auto& clock_port = check_arc.get_clock_port().substr(check_arc.get_clock_port().find(':') + 1);
            const auto& data_port = check_arc.get_data_port().substr(check_arc.get_data_port().find(':') + 1);
            for (std::size_t index = 0; index < arcs.size(); ++index) {
              if (arcs[index].check_type == static_cast<int32_t>(TimingCheckType::kSetup)
                  && arcs[index].source_port == clock_port && arcs[index].sink_port == data_port) {
                exported.library_arc_id = static_cast<int32_t>(index);
                break;
              }
            }
          }
        }
        exported.sense = senseCode(timing_arc.get_sense());
        exported.timing_type = transCode(timing_arc.get_trigger_trans_type());
        exported.check_type = static_cast<int32_t>(check_arc.get_check_type());
        if (check_arc.get_check_type() == TimingCheckType::kSetup && exported.library_arc_id >= 0) {
          snapshot.constraint_arcs.push_back(exported);
        }
        snapshot.timing_check_arcs.push_back(std::move(exported));
      }
    }
  }

  if (used_non_finite_fallback) {
    addDiagnostic(snapshot, "non-finite timing values use finite AAT/RAT sentinels (-9e7/9e7 ps)");
  }
  if (used_missing_timing_point_fallback) {
    addDiagnostic(snapshot, "pins without a timing point use zero slew/AAT and a finite RAT sentinel");
  }
  if (used_min_analysis_fallback) {
    addDiagnostic(snapshot, "min analysis AAT/RAT use max-analysis TimingPoint fallback");
  }

  std::sort(snapshot.start_points.begin(), snapshot.start_points.end());
  std::sort(snapshot.end_points.begin(), snapshot.end_points.end());
  std::sort(snapshot.clock_pins.begin(), snapshot.clock_pins.end());
  std::sort(snapshot.ff_output_pins.begin(), snapshot.ff_output_pins.end());
  std::sort(snapshot.clock_names.begin(), snapshot.clock_names.end());
  std::sort(snapshot.clock_metadata.begin(), snapshot.clock_metadata.end(),
            [](const TimingClockSnapshot& lhs, const TimingClockSnapshot& rhs) { return lhs.name < rhs.name; });
  std::sort(snapshot.graph_arcs.begin(), snapshot.graph_arcs.end(), [](const TimingGraphArcSnapshot& lhs, const TimingGraphArcSnapshot& rhs) {
    return std::tie(lhs.source_pin, lhs.sink_pin, lhs.owner_name, lhs.arc_type, lhs.library_arc_id, lhs.level)
           < std::tie(rhs.source_pin, rhs.sink_pin, rhs.owner_name, rhs.arc_type, rhs.library_arc_id, rhs.level);
  });
  std::sort(snapshot.net_arcs.begin(), snapshot.net_arcs.end(), [](const TimingNetArcSnapshot& lhs, const TimingNetArcSnapshot& rhs) {
    return std::tie(lhs.source_pin, lhs.sink_pin) < std::tie(rhs.source_pin, rhs.sink_pin);
  });
  auto endpoint_arc_less = [](const TimingEndpointArcSnapshot& lhs, const TimingEndpointArcSnapshot& rhs) {
    return std::tie(lhs.source_pin, lhs.sink_pin, lhs.library_cell, lhs.library_arc_id, lhs.check_type)
           < std::tie(rhs.source_pin, rhs.sink_pin, rhs.library_cell, rhs.library_arc_id, rhs.check_type);
  };
  std::sort(snapshot.constraint_arcs.begin(), snapshot.constraint_arcs.end(), endpoint_arc_less);
  std::sort(snapshot.timing_check_arcs.begin(), snapshot.timing_check_arcs.end(), endpoint_arc_less);

  if (snapshot.start_points.empty()) addDiagnostic(snapshot, "no constrained start points exported");
  if (snapshot.end_points.empty()) addDiagnostic(snapshot, "no constrained end points exported");
  if (snapshot.clock_pins.empty()) addDiagnostic(snapshot, "no sequential clock pins exported");
  return snapshot;
}

}  // namespace ista

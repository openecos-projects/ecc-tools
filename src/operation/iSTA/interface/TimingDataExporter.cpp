#include "TimingDataExporter.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Config.hpp"
#include "Database.hpp"
#include "EndpointQualifier.hpp"
#include "LibertyDataExporter.hpp"
#include "common/AnalysisType.hpp"
#include "common/TransType.hpp"
#include "graph/Arc.hpp"
#include "graph/ArcType.hpp"
#include "graph/TimingPoint.hpp"
#include "liberty/LibertyCell.hpp"
#include "liberty/LibertyLibrary.hpp"
#include "liberty/LibertyPort.hpp"
#include "liberty/LibertyUnits.hpp"
#include "liberty/TimingArc.hpp"
#include "liberty/TimingArcSense.hpp"
#include "liberty/TimingCellArc.hpp"
#include "liberty/TimingCheckArc.hpp"
#include "liberty/TimingCheckType.hpp"
#include "network/Instance.hpp"
#include "network/Net.hpp"
#include "network/Pin.hpp"

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

double mapValue(const std::map<AnalysisType, std::map<TransType, double>>& values, AnalysisType analysis, TransType trans, double fallback)
{
  const auto analysis_it = values.find(analysis);
  if (analysis_it == values.end())
    return fallback;
  const auto trans_it = analysis_it->second.find(trans);
  return trans_it == analysis_it->second.end() ? fallback : trans_it->second;
}

void addDiagnostic(TimingData& snapshot, std::string message)
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

// Instance checks retain the library's local timing-arc index and condition.
// Never merge conditional checks or infer their identity from CK/D alone.
bool sameCheckVariant(const TimingArc& left, const TimingArc& right)
{
  if (left.get_arc_idx() != right.get_arc_idx() || left.get_sense() != right.get_sense() || left.get_trigger_trans_type() != right.get_trigger_trans_type()
      || left.get_check_trans_type() != right.get_check_trans_type())
    return false;
  const auto& a = left.get_when_expression().get_term_list();
  const auto& b = right.get_when_expression().get_term_list();
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const auto& x, const auto& y) {
           return x.get_operation_type() == y.get_operation_type() && x.get_port_name() == y.get_port_name();
         });
}

int32_t checkArcIndex(LibertyCell& cell, TimingCheckArc& check, const TimingArc& variant)
{
  const auto clock_port = check.get_clock_port().substr(check.get_clock_port().find(':') + 1);
  const auto data_port = check.get_data_port().substr(check.get_data_port().find(':') + 1);
  int32_t index = 0;
  for (auto& delay : cell.get_cell_arc_list())
    index += delay.get_timing_arc_list().size();
  int32_t result = -1;
  for (auto& candidate : cell.get_check_arc_list()) {
    for (const auto& arc : candidate.get_timing_arc_list()) {
      if (candidate.get_check_type() == check.get_check_type() && candidate.get_clock_port() == clock_port && candidate.get_data_port() == data_port
          && candidate.get_clock_trans_type() == check.get_clock_trans_type() && sameCheckVariant(arc, variant)) {
        if (result >= 0)
          return -1;  // Ambiguous identity must not silently choose a LUT.
        result = index;
      }
      ++index;
    }
  }
  return result;
}

}  // namespace

TimingData TimingDataExporter::exportData(Database& database, const Config& config)
{
  TimingData snapshot;
  LibertyLibrary& library = database.liberty();
  const TimingTimeUnit time_unit = library.get_time_unit();
  const TimingCapacitiveUnit cap_unit = library.get_cap_unit();
  const TimingResistanceUnit resistance_unit = library.get_resistance_unit();
  snapshot.time_unit_ps = toPs(1.0, time_unit);
  snapshot.capacitance_unit_pf = toPf(1.0, cap_unit);
  snapshot.resistance_unit_ohm = toOhm(1.0, resistance_unit);
  snapshot.timing_unit_provenance
      = std::string("liberty time=") + timeUnitName(time_unit) + ", cap=" + capUnitName(cap_unit) + ", resistance=" + resistanceUnitName(resistance_unit);
  snapshot.sdc_path = database.sdc().get_sdc_file_path();
  snapshot.spef_path = database.parasitics().get_spef_file_path();
  snapshot.parasitic_capacitance_unit = database.parasitics().get_capacitive_unit();
  snapshot.parasitic_resistance_unit = database.parasitics().get_resistance_unit();
  snapshot.timing_corner = config.timing_corner;
  snapshot.parasitics_initialization = snapshot.spef_path.empty() ? "liberty_or_default" : "spef";
  if (!snapshot.parasitic_capacitance_unit.empty() || !snapshot.parasitic_resistance_unit.empty()) {
    snapshot.timing_unit_provenance += ", spef cap=" + snapshot.parasitic_capacitance_unit + ", spef resistance=" + snapshot.parasitic_resistance_unit;
  }

  bool used_non_finite_fallback = false;
  bool used_missing_timing_point_fallback = false;
  bool used_min_analysis_fallback = false;
  std::unordered_map<std::string, int32_t> pin_ids;
  pin_ids.reserve(database.network().pins().size());
  for (auto& [name, pin] : database.network().pins()) {
    pin_ids.emplace(name, static_cast<int32_t>(snapshot.pins.size()));
    TimingPinData exported;
    exported.name = name;
    exported.net_name = pin.get_net_name();
    exported.instance_name = pin.get_instance_name();
    exported.direction = static_cast<int32_t>(pin.get_direction());
    exported.is_port = pin.get_is_port();
    if (exported.is_port && (pin.get_direction() == PinDirection::kOutput || pin.get_direction() == PinDirection::kInout)) {
      const auto& port_constraints = database.sdc().get_port_constraint_map();
      const auto constraint = port_constraints.find(name);
      exported.is_unconstrained_output = constraint == port_constraints.end() || !constraint->second.get_has_output_delay_max();
      exported.max_qualification = qualifyMaxOutput(database, name);
    }
    exported.max_rise_rat_ps = kUnconstrainedRequiredPs;
    exported.max_fall_rat_ps = kUnconstrainedRequiredPs;
    exported.min_rise_rat_ps = kUnconstrainedRequiredPs;
    exported.min_fall_rat_ps = kUnconstrainedRequiredPs;
    const auto point_it = database.graph().points().find(name);
    if (point_it != database.graph().points().end()) {
      TimingPoint& point = point_it->second;
      exported.level = point.get_level();
      exported.max_rise_aat_ps = normalizedTime(point.get_arrival(), time_unit, kUnconstrainedArrivalPs, used_non_finite_fallback);
      exported.max_fall_aat_ps = exported.max_rise_aat_ps;
      exported.max_rise_rat_ps = normalizedTime(point.get_required(), time_unit, kUnconstrainedRequiredPs, used_non_finite_fallback);
      exported.max_fall_rat_ps = exported.max_rise_rat_ps;
      exported.setup_check_time_ps = normalizedTime(point.get_setup_check_time(), time_unit, 0.0, used_non_finite_fallback);
      exported.max_rise_slew_ps
          = normalizedTime(mapValue(point.get_data_slew_map(), AnalysisType::kMax, TransType::kRise, 0.0), time_unit, 0.0, used_non_finite_fallback);
      exported.max_fall_slew_ps
          = normalizedTime(mapValue(point.get_data_slew_map(), AnalysisType::kMax, TransType::kFall, 0.0), time_unit, 0.0, used_non_finite_fallback);
      exported.min_rise_slew_ps
          = normalizedTime(mapValue(point.get_data_slew_map(), AnalysisType::kMin, TransType::kRise, 0.0), time_unit, 0.0, used_non_finite_fallback);
      exported.min_fall_slew_ps
          = normalizedTime(mapValue(point.get_data_slew_map(), AnalysisType::kMin, TransType::kFall, 0.0), time_unit, 0.0, used_non_finite_fallback);
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

  snapshot.lib_cells = LibertyDataExporter::exportCells(library, time_unit, cap_unit);
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
    const auto pin_it = database.network().pins().find(exported_pin.name);
    const auto instance_it = database.network().instances().find(exported_pin.instance_name);
    if (pin_it == database.network().pins().end() || instance_it == database.network().instances().end()) {
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
      if (const auto rise = it->second.find(TransType::kRise); rise != it->second.end())
        exported_pin.rise_capacitance_pf = rise->second;
      if (const auto fall = it->second.find(TransType::kFall); fall != it->second.end())
        exported_pin.fall_capacitance_pf = fall->second;
    }
  }

  for (auto& [instance_name, instance] : database.network().instances()) {
    if (!instance.get_is_sequential())
      continue;
    if (!instance.get_clock_pin_name().empty())
      snapshot.clock_pins.push_back(instance.get_clock_pin_name());
    if (!instance.get_output_pin_name().empty())
      snapshot.ff_output_pins.push_back(instance.get_output_pin_name());
  }
  for (auto& [clock_name, clock] : database.sdc().get_clock_map()) {
    snapshot.clock_names.push_back(clock_name);
    TimingClockData exported_clock;
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
  snapshot.start_points = database.graph().startpoints();
  snapshot.end_points = database.graph().endpoints();

  for (Arc& arc : database.graph().arcs()) {
    const auto source_it = pin_ids.find(arc.get_source_pin());
    const auto sink_it = pin_ids.find(arc.get_sink_pin());
    if (source_it == pin_ids.end() || sink_it == pin_ids.end()) {
      addDiagnostic(snapshot, "unsupported arc with missing pin identity");
      continue;
    }
    TimingGraphArcData exported;
    exported.source_pin = arc.get_source_pin();
    exported.sink_pin = arc.get_sink_pin();
    exported.owner_name = arc.get_owner_name();
    exported.arc_type = arc.get_type() == ArcType::kCell ? 1 : arc.get_type() == ArcType::kNet ? 2 : 0;
    exported.is_clock_arc = arc.get_is_clock_arc();
    exported.delay_max_ps = normalizedTime(arc.get_delay_max(), time_unit, 0.0, used_non_finite_fallback);
    exported.delay_min_ps = normalizedTime(arc.get_delay_min(), time_unit, 0.0, used_non_finite_fallback);
    exported.level = exported.is_clock_arc ? 0 : database.graph().sinkPoint(arc).get_level();
    const auto owner_it = database.network().instances().find(arc.get_owner_name());
    if (!arc.get_owner_name().empty() && owner_it != database.network().instances().end()) {
      exported.library_cell = owner_it->second.get_cell_name();
    } else if (arc.get_type() == ArcType::kCell && !arc.get_owner_name().empty()) {
      addDiagnostic(snapshot, "unsupported arc owner identity: " + arc.get_owner_name());
    }
    TimingCellArc* cell_arc = arc.libertyArcId() ? library.findCellArc(*arc.libertyArcId()) : nullptr;
    if (cell_arc != nullptr) {
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
                if (index >= cell.arcs.size())
                  break;
                TimingGraphArcData variant = exported;
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
        if (mapped)
          continue;
        addDiagnostic(snapshot, "unmapped instance timing arc: " + exported.owner_name);
      }
    }
    snapshot.graph_arcs.push_back(std::move(exported));
  }

  // iSTA launches sequential data from CK, while DreamPlace initializes Q
  // through level-0 clock-to-Q arcs and treats Q as the launch startpoint.
  std::unordered_map<std::string, std::string> clock_outputs;
  for (const auto& arc : snapshot.graph_arcs) {
    if (arc.is_clock_arc && arc.library_arc_id >= 0)
      clock_outputs.emplace(arc.source_pin, arc.sink_pin);
  }
  for (std::string& name : snapshot.start_points) {
    const auto it = clock_outputs.find(name);
    if (it != clock_outputs.end())
      name = it->second;
  }

  for (auto& [net_name, net] : database.network().nets()) {
    if (net.get_driver_pin_list().empty()) {
      addDiagnostic(snapshot, "net without a resolved driver: " + net_name);
      continue;
    }
    for (const std::string& driver : net.get_driver_pin_list()) {
      if (database.graph().simValues().isDriverDisabled(driver))
        continue;
      for (const std::string& load : net.get_load_pin_list()) {
        if (load != driver)
          snapshot.net_arcs.push_back({driver, load});
      }
    }
  }

  for (auto& [instance_name, instance] : database.network().instances()) {
    const auto cell_it = cell_ids.find(instance.get_cell_name());
    if (cell_it == cell_ids.end())
      continue;
    for (TimingCheckArc& check_arc : instance.get_check_arc_list()) {
      const std::string& source_pin = check_arc.get_clock_port();
      const std::string& sink_pin = check_arc.get_data_port();
      if (!pin_ids.contains(source_pin) || !pin_ids.contains(sink_pin)) {
        addDiagnostic(snapshot, "timing check arc with missing pin identity");
        continue;
      }
      for (TimingArc& timing_arc : check_arc.get_timing_arc_list()) {
        TimingEndpointArcData exported;
        exported.source_pin = source_pin;
        exported.sink_pin = sink_pin;
        exported.library_cell = instance.get_cell_name();
        auto& cell = library.get_cell_map().at(instance.get_cell_name());
        exported.library_arc_id = checkArcIndex(cell, check_arc, timing_arc);
        if (exported.library_arc_id < 0)
          addDiagnostic(snapshot, "unmapped or ambiguous timing check: " + source_pin + " -> " + sink_pin);
        exported.sense = senseCode(timing_arc.get_sense());
        exported.timing_type = transCode(timing_arc.get_trigger_trans_type());
        exported.check_type = static_cast<int32_t>(check_arc.get_check_type());
        exported.max_qualification = qualifyMaxCheck(database, check_arc, timing_arc);
        auto& endpoint_qualification = snapshot.pins.at(pin_ids.at(sink_pin)).max_qualification;
        for (std::size_t edge = 0; edge < 2; ++edge) {
          if (!endpoint_qualification.valid[edge]
              && (exported.max_qualification.reason[edge] != TimingMaxQualificationReason::kNotMaxCheck
                  || endpoint_qualification.reason[edge] == TimingMaxQualificationReason::kUnconstrainedGraphTerminal)) {
            endpoint_qualification.valid[edge] = exported.max_qualification.valid[edge];
            endpoint_qualification.reason[edge] = exported.max_qualification.reason[edge];
          }
        }
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
            [](const TimingClockData& lhs, const TimingClockData& rhs) { return lhs.name < rhs.name; });
  std::sort(snapshot.graph_arcs.begin(), snapshot.graph_arcs.end(), [](const TimingGraphArcData& lhs, const TimingGraphArcData& rhs) {
    return std::tie(lhs.source_pin, lhs.sink_pin, lhs.owner_name, lhs.arc_type, lhs.library_arc_id, lhs.level)
           < std::tie(rhs.source_pin, rhs.sink_pin, rhs.owner_name, rhs.arc_type, rhs.library_arc_id, rhs.level);
  });
  std::sort(snapshot.net_arcs.begin(), snapshot.net_arcs.end(), [](const TimingNetArcData& lhs, const TimingNetArcData& rhs) {
    return std::tie(lhs.source_pin, lhs.sink_pin) < std::tie(rhs.source_pin, rhs.sink_pin);
  });
  auto endpoint_arc_less = [](const TimingEndpointArcData& lhs, const TimingEndpointArcData& rhs) {
    return std::tie(lhs.source_pin, lhs.sink_pin, lhs.library_cell, lhs.library_arc_id, lhs.check_type)
           < std::tie(rhs.source_pin, rhs.sink_pin, rhs.library_cell, rhs.library_arc_id, rhs.check_type);
  };
  std::sort(snapshot.constraint_arcs.begin(), snapshot.constraint_arcs.end(), endpoint_arc_less);
  std::sort(snapshot.timing_check_arcs.begin(), snapshot.timing_check_arcs.end(), endpoint_arc_less);

  if (snapshot.start_points.empty())
    addDiagnostic(snapshot, "no constrained start points exported");
  if (snapshot.end_points.empty())
    addDiagnostic(snapshot, "no constrained end points exported");
  if (snapshot.clock_pins.empty())
    addDiagnostic(snapshot, "no sequential clock pins exported");
  return snapshot;
}

}  // namespace ista

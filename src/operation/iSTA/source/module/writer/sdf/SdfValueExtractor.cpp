// SPDX-License-Identifier: MulanPSL-2.0
#include "SdfValueExtractor.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include "Logger.hpp"
#include "Utility.hpp"
namespace ista {
namespace {
// Derate scope of an arc is named by the pin it starts from, the way the factor
// lookup expects: -net for a net delay, -cell for a cell delay. Mirrors
// TimingStateQuery::derateContext so a factor resolved while writing the SDF
// matches the one resolved while propagating the same arc.
DerateContext derateContext(Database& database, const std::string& pin_name)
{
  DerateContext context;
  const auto& network = database.network();
  const auto pin = network.pins().find(pin_name);
  if (pin == network.pins().end())
    return context;
  context.net = pin->second.get_net_name();
  if (pin->second.get_is_port())
    return context;
  context.instance = pin->second.get_instance_name();
  const auto instance = network.instances().find(std::string(context.instance));
  if (instance != network.instances().end()) {
    const auto& cells = database.liberty().get_cell_map();
    const auto cell = cells.find(instance->second.get_cell_name());
    if (cell != cells.end())
      context.lib_cell = cell->second.get_library_name() + "/" + cell->first;
  }
  return context;
}

std::vector<TransType> inputTransitions(TimingArc& timing_arc)
{
  const auto trigger = timing_arc.get_trigger_trans_type();
  if (trigger != TransType::kNone)
    return {trigger};
  if (timing_arc.isThreeStateArc())
    return {TransType::kRise, TransType::kFall};
  if (timing_arc.get_sense() == TimingArcSense::kNonUnate) {
    // Non-unate arcs require a distinct SDF IOPATH for each source edge.
    return {TransType::kRise, TransType::kFall};
  }
  return {TransType::kNone};
}

inline bool adjustSdfHoldTimingCheckDelay(double minimum_setup_delay, double& minimum_hold_delay, double& maximum_hold_delay)
{
  const double minimum_valid_hold_delay = -minimum_setup_delay;
  bool adjusted = false;

  if (minimum_hold_delay < minimum_valid_hold_delay) {
    minimum_hold_delay = minimum_valid_hold_delay;
    adjusted = true;
  }
  if (maximum_hold_delay < minimum_valid_hold_delay) {
    maximum_hold_delay = minimum_valid_hold_delay;
    adjusted = true;
  }
  return adjusted;
}

}  // namespace

SdfCellArcDelayMap SdfValueExtractor::graphCellArcDelays(Arc& arc)
{
  SdfCellArcDelayMap values;
  auto* cell_arc = arc.libertyArcId() ? _database.liberty().findCellArc(*arc.libertyArcId()) : nullptr;
  const bool separate_families
      = cell_arc && std::any_of(cell_arc->get_timing_arc_list().begin(), cell_arc->get_timing_arc_list().end(), [&](TimingArc& timing) {
          return timing.isRetaining() || timing.get_sdf_cond() != cell_arc->get_timing_arc_list().front().get_sdf_cond();
        });
  if (cell_arc != nullptr) {
    for (TimingArc& timing_arc : cell_arc->get_timing_arc_list()) {
      for (TransType transition : inputTransitions(timing_arc)) {
        auto delay = timingArcDelay(arc, timing_arc, transition, !separate_families);
        mergeCellArcDelay(values, timing_arc, transition, delay);
      }
    }
  }
  if (values.empty() && !separate_families)
    values[{}] = arcDelay(arc);
  return values;
}

SdfCellArcDelayMap SdfValueExtractor::cellArcDelays(Instance& instance, TimingCellArc& cell_arc)
{
  SdfCellArcDelayMap values;
  if (cell_arc.get_timing_arc_list().empty()) {
    auto& delay = values[{}];
    delay.update(AnalysisType::kMin, TransType::kRise, cell_arc.get_delay_min());
    delay.update(AnalysisType::kMax, TransType::kRise, cell_arc.get_delay_max());
    delay.update(AnalysisType::kMin, TransType::kFall, cell_arc.get_delay_min());
    delay.update(AnalysisType::kMax, TransType::kFall, cell_arc.get_delay_max());
  }
  for (TimingArc& timing_arc : cell_arc.get_timing_arc_list()) {
    for (TransType transition : inputTransitions(timing_arc)) {
      auto delay = cellArcDelay(cell_arc, timing_arc, transition);
      mergeCellArcDelay(values, timing_arc, transition, delay);
    }
  }
  // These arcs exist in the library but were never built into the timing graph, so
  // nothing upstream derated them. Apply the same -cell_delay factors a graph arc
  // would have received, resolved from the pin the arc starts at.
  const DerateContext context = derateContext(_database, STAUTIL.getString(instance.get_instance_name(), ":", cell_arc.get_source_port()));
  for (auto& [key, delay] : values)
    derateDelay(delay, DerateDelay::kCell, context);
  return values;
}

SdfCheckValues SdfValueExtractor::checkValues(Instance& instance, TimingCheckArc& check_arc, TimingArc& timing_arc, TransType transition)
{
  SdfCheckValues values{checkDelay(instance, check_arc, timing_arc, AnalysisType::kMin, transition),
                        checkDelay(instance, check_arc, timing_arc, AnalysisType::kMax, transition)};
  // set_timing_derate reaches a timing check only through its own options:
  // -cell_check for setup/hold, -min_period for PERIOD and -min_pulse_width for
  // WIDTH. A -cell_delay entry must not move these numbers. Pulse checks carry a
  // single value, so one untyped factor covers both corners there; the edge checks
  // keep the early and late factors on their own corners.
  const TimingCheckType check_type = check_arc.get_check_type();
  const bool pulse = check_type == TimingCheckType::kPeriod || check_type == TimingCheckType::kWidth;
  const DerateDelay kind = check_type == TimingCheckType::kPeriod ? DerateDelay::kPeriod
                         : check_type == TimingCheckType::kWidth  ? DerateDelay::kWidth
                                                                  : DerateDelay::kCheck;
  const TimingDerates& factors = _database.sdc().timingDerates();
  DerateContext context{.instance = instance.get_instance_name(), .lib_cell = {}, .net = {}};
  const auto& cells = _database.liberty().get_cell_map();
  const auto cell = cells.find(instance.get_cell_name());
  if (cell != cells.end())
    context.lib_cell = cell->second.get_library_name() + "/" + cell->first;
  if (pulse) {
    values.minimum = factors.apply(values.minimum, AnalysisType::kNone, false, transition, kind, context);
    values.maximum = factors.apply(values.maximum, AnalysisType::kNone, false, transition, kind, context);
  } else {
    values.minimum = factors.apply(values.minimum, AnalysisType::kMin, false, transition, kind, context);
    values.maximum = factors.apply(values.maximum, AnalysisType::kMax, false, transition, kind, context);
  }
  // Preserve the format-specific policy: PERIOD keeps corner order; WIDTH and
  // edge checks sort corners, and HOLD additionally pairs with matching SETUP.
  if (check_arc.get_check_type() != TimingCheckType::kPeriod) {
    if (values.minimum > values.maximum)
      std::swap(values.minimum, values.maximum);
    adjustHoldDelay(instance, check_arc, timing_arc, transition, values.minimum, values.maximum);
  }
  return values;
}

void SdfValueExtractor::mergeCellArcDelay(SdfCellArcDelayMap& cell_arc_delay_map, TimingArc& timing_arc, TransType input_trans_type, SdfDelay& sdf_delay)
{
  if (!hasDelay(sdf_delay)) {
    return;
  }
  TransType sdf_input_trans_type = input_trans_type == TransType::kNone ? timing_arc.get_trigger_trans_type() : input_trans_type;
  SdfCellArcKey cell_arc_key{timing_arc.get_sdf_cond(), sdf_input_trans_type, timing_arc.isRetaining()};
  mergeDelay(cell_arc_delay_map[cell_arc_key], sdf_delay);
}

void SdfValueExtractor::mergeDelay(SdfDelay& target_delay, SdfDelay& source_delay)
{
  if (source_delay.get_rise_min_delay()) {
    target_delay.update(AnalysisType::kMin, TransType::kRise, *source_delay.get_rise_min_delay());
  }
  if (source_delay.get_rise_max_delay()) {
    target_delay.update(AnalysisType::kMax, TransType::kRise, *source_delay.get_rise_max_delay());
  }
  if (source_delay.get_fall_min_delay()) {
    target_delay.update(AnalysisType::kMin, TransType::kFall, *source_delay.get_fall_min_delay());
  }
  if (source_delay.get_fall_max_delay()) {
    target_delay.update(AnalysisType::kMax, TransType::kFall, *source_delay.get_fall_max_delay());
  }
}

void SdfValueExtractor::adjustHoldDelay(Instance& instance, TimingCheckArc& hold_timing_check_arc, TimingArc& hold_timing_arc, TransType data_trans_type,
                                        double& minimum_hold_delay, double& maximum_hold_delay)
{
  if (hold_timing_check_arc.get_check_type() != TimingCheckType::kHold) {
    return;
  }
  LibertyCell* timing_cell = timingCell(instance);
  if (timing_cell == nullptr) {
    return;
  }
  TimingCheckArc* setup_timing_check_arc = findSetupCheck(*timing_cell, hold_timing_check_arc);
  if (setup_timing_check_arc == nullptr) {
    return;
  }
  TimingArc default_setup_timing_arc;
  TimingArc* setup_timing_arc = findSetupArc(*setup_timing_check_arc, hold_timing_arc, data_trans_type);
  if (setup_timing_arc == nullptr) {
    if (!setup_timing_check_arc->get_timing_arc_list().empty()) {
      return;
    }
    setup_timing_arc = &default_setup_timing_arc;
  }

  // SETUP takes no HOLD adjustment, so this obtains its exported derates and
  // corner ordering without recursively pairing another check.
  const auto setup = checkValues(instance, *setup_timing_check_arc, *setup_timing_arc, data_trans_type);
  if (adjustSdfHoldTimingCheckDelay(setup.minimum, minimum_hold_delay, maximum_hold_delay)) {
    STALOG.warn(Loc::current(), "Adjusted HOLD timing-check delay for ", instance.get_instance_name(), ": ", hold_timing_check_arc.get_data_port(),
                " relative to ", hold_timing_check_arc.get_clock_port(), " would otherwise sum to less than zero with the matching SETUP delay.");
  }
}

TimingCheckArc* SdfValueExtractor::findSetupCheck(LibertyCell& timing_cell, TimingCheckArc& hold_timing_check_arc)
{
  for (TimingCheckArc& timing_check_arc : timing_cell.get_sdf_check_arc_list()) {
    if (timing_check_arc.get_check_type() != TimingCheckType::kSetup) {
      continue;
    }
    if (timing_check_arc.get_data_port() == hold_timing_check_arc.get_data_port() && timing_check_arc.get_clock_port() == hold_timing_check_arc.get_clock_port()
        && timing_check_arc.get_clock_trans_type() == hold_timing_check_arc.get_clock_trans_type()) {
      return &timing_check_arc;
    }
  }
  return nullptr;
}

TimingArc* SdfValueExtractor::findSetupArc(TimingCheckArc& setup_timing_check_arc, TimingArc& hold_timing_arc, TransType data_trans_type)
{
  for (TimingArc& timing_arc : setup_timing_check_arc.get_timing_arc_list()) {
    if (timing_arc.get_sdf_cond() != hold_timing_arc.get_sdf_cond() || timing_arc.get_check_trans_type() != hold_timing_arc.get_check_trans_type()) {
      continue;
    }
    if (timing_arc.get_check_table_map().empty()) {
      if (data_trans_type == TransType::kRise) {
        return &timing_arc;
      }
      continue;
    }
    if (timing_arc.get_check_table_map().count(data_trans_type) > 0) {
      return &timing_arc;
    }
  }
  return nullptr;
}

LibertyCell* SdfValueExtractor::timingCell(Instance& instance)
{
  Database& database = _database;
  std::map<std::string, LibertyCell>& timing_cell_map = database.liberty().get_cell_map();
  if (timing_cell_map.count(instance.get_cell_name()) == 0) {
    return nullptr;
  }
  return &timing_cell_map[instance.get_cell_name()];
}

bool SdfValueExtractor::hasCellArcPins(Instance& instance, TimingCellArc& timing_cell_arc)
{
  Database& database = _database;
  std::string source_pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_cell_arc.get_source_port());
  std::string sink_pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_cell_arc.get_sink_port());
  return database.network().pins().count(source_pin_name) > 0 && database.network().pins().count(sink_pin_name) > 0;
}

bool SdfValueExtractor::hasCheckPins(Instance& instance, TimingCheckArc& timing_check_arc)
{
  Database& database = _database;
  std::string data_pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_check_arc.get_data_port());
  if (database.network().pins().count(data_pin_name) == 0) {
    return false;
  }
  if (timing_check_arc.get_check_type() == TimingCheckType::kWidth || timing_check_arc.get_check_type() == TimingCheckType::kPeriod) {
    return true;
  }
  std::string clock_pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_check_arc.get_clock_port());
  return database.network().pins().count(clock_pin_name) > 0;
}

SdfDelay SdfValueExtractor::arcDelay(Arc& arc, TransType input_trans_type)
{
  SdfDelay sdf_delay;
  const DerateDelay kind = arc.get_type() == ArcType::kNet ? DerateDelay::kNet : DerateDelay::kCell;
  const DerateContext context = derateContext(_database, arc.get_source_pin());
  if (!arc.get_graph_delay_map().empty()) {
    updateDelay(sdf_delay, arc.get_graph_delay_map(), input_trans_type, kind, context);
  }
  if (!hasDelay(sdf_delay)) {
    updateDelay(sdf_delay, arc.get_input_output_delay_map(), input_trans_type, kind, context);
  }
  if (arc.get_type() == ArcType::kNet) {
    if (arc.rcFailed(AnalysisType::kMin, TransType::kRise))
      sdf_delay.set_rise_min_delay(0.);
    if (arc.rcFailed(AnalysisType::kMax, TransType::kRise))
      sdf_delay.set_rise_max_delay(0.);
    if (arc.rcFailed(AnalysisType::kMin, TransType::kFall))
      sdf_delay.set_fall_min_delay(0.);
    if (arc.rcFailed(AnalysisType::kMax, TransType::kFall))
      sdf_delay.set_fall_max_delay(0.);
  }
  return sdf_delay;
}

SdfDelay SdfValueExtractor::timingArcDelay(Arc& arc, TimingArc& timing_arc, TransType input_trans_type, bool allow_aggregate)
{
  SdfDelay sdf_delay;
  const DerateDelay kind = arc.get_type() == ArcType::kNet ? DerateDelay::kNet : DerateDelay::kCell;
  const DerateContext context = derateContext(_database, arc.get_source_pin());
  if (arc.get_timing_arc_delay_map().count(timing_arc.get_arc_idx()) > 0) {
    updateDelay(sdf_delay, arc.get_timing_arc_delay_map()[timing_arc.get_arc_idx()], input_trans_type, kind, context);
  }
  if (!hasDelay(sdf_delay) && allow_aggregate) {
    // arcDelay() already applies the factors to what it returns.
    return arcDelay(arc, input_trans_type);
  }
  return sdf_delay;
}

SdfDelay SdfValueExtractor::cellArcDelay(TimingCellArc& timing_cell_arc, TimingArc& timing_arc, TransType input_trans_type)
{
  static_cast<void>(input_trans_type);
  SdfDelay sdf_delay;
  for (AnalysisType analysis_type : {AnalysisType::kMin, AnalysisType::kMax}) {
    for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
      if (timing_arc.get_delay_table_map().count(trans_type) == 0) {
        continue;
      }
      double delay = timing_arc.get_delay_table_map()[trans_type].findValue(0.0, 0.0) / timing_arc.get_time_unit_scale();
      sdf_delay.update(analysis_type, trans_type, delay);
    }
  }
  if (!hasDelay(sdf_delay)) {
    sdf_delay.update(AnalysisType::kMin, TransType::kRise, timing_cell_arc.get_delay_min());
    sdf_delay.update(AnalysisType::kMax, TransType::kRise, timing_cell_arc.get_delay_max());
    sdf_delay.update(AnalysisType::kMin, TransType::kFall, timing_cell_arc.get_delay_min());
    sdf_delay.update(AnalysisType::kMax, TransType::kFall, timing_cell_arc.get_delay_max());
  }
  return sdf_delay;
}

void SdfValueExtractor::updateDelay(SdfDelay& sdf_delay, std::map<AnalysisType, std::map<TransType, std::map<TransType, double>>>& delay_map,
                                    TransType input_trans_type, DerateDelay kind, const DerateContext& context)
{
  const auto& derates = _database.sdc().timingDerates();
  for (std::pair<const AnalysisType, std::map<TransType, std::map<TransType, double>>>& analysis_delay_pair : delay_map) {
    AnalysisType analysis_type = analysis_delay_pair.first;
    if (analysis_type != AnalysisType::kMin && analysis_type != AnalysisType::kMax) {
      continue;
    }
    for (std::pair<const TransType, std::map<TransType, double>>& input_delay_pair : analysis_delay_pair.second) {
      if (input_trans_type != TransType::kNone && input_delay_pair.first != input_trans_type) {
        continue;
      }
      for (std::pair<const TransType, double>& output_delay_pair : input_delay_pair.second) {
        if (output_delay_pair.first == TransType::kRise || output_delay_pair.first == TransType::kFall) {
          // Vary each alternative before the entries collapse into one number per
          // corner. A factor above one grows a negative delay toward zero, so the
          // worst alternative can change; reducing first would hide it.
          sdf_delay.update(analysis_type, output_delay_pair.first,
                           derates.apply(output_delay_pair.second, analysis_type, false, output_delay_pair.first, kind, context));
        }
      }
    }
  }
}

void SdfValueExtractor::derateDelay(SdfDelay& sdf_delay, DerateDelay kind, const DerateContext& context)
{
  // set_timing_derate scales the calculated cell and net delays. It does not scale
  // transitions, and it reaches a timing check only through -cell_check, so only the
  // four delay entries are touched here. The nominal value stays in the graph: the
  // factor belongs to path consumption, so the copy written to the SDF is scaled and
  // the propagated state is left alone.
  //
  // Every arc lands in both a data-path and a clock-path reading, and the SDF carries
  // one number per corner, so the data-path form (clock_path = false) is used. That
  // matches the SDC here, where no derate names -data or -clock and the entry
  // therefore covers both.
  const auto& derates = _database.sdc().timingDerates();

  // Apply through TimingDerates::apply instead of scaling by hand. A scaled net
  // delay can be negative, and a plain multiply would let a late factor move such
  // a value earlier; apply() keeps the direction of the nominal value.
  auto scale = [&](std::optional<double>& value, AnalysisType analysis_type, TransType trans_type) {
    if (!value)
      return;
    value = derates.apply(*value, analysis_type, false, trans_type, kind, context);
  };
  scale(sdf_delay.get_rise_min_delay(), AnalysisType::kMin, TransType::kRise);
  scale(sdf_delay.get_rise_max_delay(), AnalysisType::kMax, TransType::kRise);
  scale(sdf_delay.get_fall_min_delay(), AnalysisType::kMin, TransType::kFall);
  scale(sdf_delay.get_fall_max_delay(), AnalysisType::kMax, TransType::kFall);
}

bool SdfValueExtractor::hasDelay(SdfDelay& sdf_delay)
{
  return sdf_delay.get_rise_min_delay() || sdf_delay.get_rise_max_delay() || sdf_delay.get_fall_min_delay() || sdf_delay.get_fall_max_delay();
}

double SdfValueExtractor::checkDelay(Instance& instance, TimingCheckArc& timing_check_arc, TimingArc& timing_arc, AnalysisType analysis_type,
                                     TransType data_trans_type)
{
  if (timing_arc.get_check_table_map().count(data_trans_type) == 0) {
    return timing_check_arc.get_check_time();
  }
  std::string data_pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_check_arc.get_data_port());
  // WIDTH/PERIOD are single-endpoint checks. The same clock pin drives both
  // lookup axes, so keep the historical clock-slew selection for both axes.
  // Data-waveform priority applies to two-endpoint checks such as SETUP/HOLD.
  double data_slew = timing_check_arc.get_check_type() == TimingCheckType::kWidth || timing_check_arc.get_check_type() == TimingCheckType::kPeriod
                         ? slew(data_pin_name, analysis_type, data_trans_type)
                         : dataSlew(data_pin_name, analysis_type, data_trans_type);
  double clock_slew = checkClockSlew(instance, timing_check_arc, analysis_type, data_trans_type);
  double delay = timing_arc.get_check_table_map()[data_trans_type].findValue(clock_slew * timing_arc.get_time_unit_scale(),
                                                                             data_slew * timing_arc.get_time_unit_scale());
  return delay / timing_arc.get_time_unit_scale();
}

double SdfValueExtractor::dataSlew(std::string& pin_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = _database;
  if (database.graph().points().count(pin_name) == 0) {
    return 0.0;
  }
  TimingPoint& timing_point = database.graph().points()[pin_name];
  if (timing_point.get_data_slew_map().count(analysis_type) > 0 && timing_point.get_data_slew_map()[analysis_type].count(trans_type) > 0) {
    return timing_point.get_data_slew_map()[analysis_type][trans_type];
  }
  // Missing data waveform keeps the existing zero fallback. ClockState's
  // physical waveform is a distinct value; selecting one here would change
  // timing-check export semantics and needs its own validation.
  return 0.0;
}

double SdfValueExtractor::checkClockSlew(Instance& instance, TimingCheckArc& timing_check_arc, AnalysisType analysis_type, TransType data_trans_type)
{
  if (timing_check_arc.get_check_type() == TimingCheckType::kWidth || timing_check_arc.get_check_type() == TimingCheckType::kPeriod) {
    std::string pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_check_arc.get_data_port());
    return slew(pin_name, analysis_type, data_trans_type);
  }
  std::string clock_pin_name = STAUTIL.getString(instance.get_instance_name(), ":", timing_check_arc.get_clock_port());
  return slew(clock_pin_name, captureAnalysis(analysis_type), timing_check_arc.get_clock_trans_type());
}

double SdfValueExtractor::slew(std::string& pin_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = _database;
  if (database.graph().points().count(pin_name) == 0) {
    return 0.0;
  }
  TimingPoint& timing_point = database.graph().points()[pin_name];
  if (const auto* state = timing_point.findDefaultClockState()) {
    if (const auto slew = state->slew(analysis_type, trans_type))
      return *slew;
  }
  if (timing_point.get_data_slew_map().count(analysis_type) > 0 && timing_point.get_data_slew_map()[analysis_type].count(trans_type) > 0) {
    return timing_point.get_data_slew_map()[analysis_type][trans_type];
  }
  return 0.0;
}

AnalysisType SdfValueExtractor::captureAnalysis(AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMax) {
    return AnalysisType::kMin;
  }
  if (analysis_type == AnalysisType::kMin) {
    return AnalysisType::kMax;
  }
  return AnalysisType::kNone;
}

}  // namespace ista

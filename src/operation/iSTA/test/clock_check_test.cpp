// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <stdexcept>

#include "ClockChecker.hpp"
using namespace ista;
static void near(double actual, double expected, const char* message)
{
  if (std::abs(actual - expected) > 1e-10)
    throw std::runtime_error(message);
}
int main()
{
  Database db;
  auto& instance = db.network().instances()["block/ff"];
  instance.set_instance_name("block/ff");
  instance.set_cell_name("FF");
  auto& cell = db.liberty().get_cell_map()["FF"];
  cell.set_library_name("lib");
  auto& pin = db.network().pins()["block/ff:CK"];
  pin.set_full_name("block/ff:CK");
  pin.set_instance_name("block/ff");
  pin.set_pin_name("CK");
  auto& clock = db.sdc().get_clock_map()["clk"];
  clock.set_clock_name("clk");
  clock.set_period(2.);
  clock.set_fall_edge(.8);
  auto& state = db.graph().points()["block/ff:CK"].get_clock_state("clk");
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      state.waveforms[edge].arrival_map[mode][edge] = 0.;
      state.waveforms[edge].slew_map[mode][edge] = .05;
    }
  for (auto type : {TimingCheckType::kWidth, TimingCheckType::kPeriod}) {
    TimingCheckArc arc;
    arc.set_data_port("CK");
    arc.set_clock_port("CK");
    arc.set_check_type(type);
    TimingArc table;
    table.get_check_table_map()[TransType::kRise].set_value_list({type == TimingCheckType::kWidth ? .6 : 1.2});
    table.get_check_table_map()[TransType::kFall].set_value_list({type == TimingCheckType::kWidth ? .7 : 1.4});
    arc.get_timing_arc_list().push_back(table);
    cell.get_sdf_check_arc_list().push_back(arc);
  }
  auto check = [&](bool derated) {
    const auto results = ClockChecker(db).check();
    if (results.size() != 4)
      throw std::runtime_error("missing clock period/pulse checks");
    for (const auto& result : results) {
      const bool period = result.type == TimingCheckType::kPeriod, rise = result.edge == TransType::kRise;
      const double nominal = period ? (rise ? 1.2 : 1.4) : (rise ? .6 : .7);
      const double factor = derated ? (period ? (rise ? 1.3 : 1.4) : (rise ? 1.5 : 1.6)) : 1.;
      const double actual = period ? 2. : rise ? .8 : 1.2;
      near(result.required, nominal * factor, "clock constraint derate not consumed");
      near(result.derate, factor, "wrong constraint factor");
      near(result.actual, actual, "wrong clock edge separation");
      near(result.slack(), actual - nominal * factor, "wrong pulse slack");
    }
  };
  check(false);
  clock.set_period(10);
  clock.set_waveform({0, 2, 6, 8});
  for (const auto& result : ClockChecker(db).check())
    near(result.actual, result.type == TimingCheckType::kPeriod ? 4. : 2., "multi-pulse check ignored a waveform edge");
  clock.set_waveform({});
  clock.set_period(2);
  clock.set_rise_edge(0);
  clock.set_fall_edge(.8);
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    db.sdc().timingDerates().set(AnalysisType::kNone, false, edge, DerateDelay::kPeriod, edge == TransType::kRise ? 1.3 : 1.4);
    db.sdc().timingDerates().set(AnalysisType::kNone, false, edge, DerateDelay::kWidth, 1.2);
    db.sdc().timingDerates().set(AnalysisType::kNone, false, edge, DerateDelay::kWidth, 1.4, DerateObject::kLibCell, "lib/FF");
    db.sdc().timingDerates().set(AnalysisType::kNone, false, edge, DerateDelay::kWidth, edge == TransType::kRise ? .1 : .2, DerateObject::kHierarchy, "block",
                                 true);
  }
  check(true);
  // With CRPR the common early/late latency cancels for period checks and
  // leaves only the edge-specific latency difference in high/low widths.
  clock.set_source_latency(AnalysisType::kMin, clockVariation(AnalysisType::kMin), TransType::kRise, .1);
  clock.set_source_latency(AnalysisType::kMax, clockVariation(AnalysisType::kMax), TransType::kRise, .2);
  clock.set_source_latency(AnalysisType::kMin, clockVariation(AnalysisType::kMin), TransType::kFall, .3);
  clock.set_source_latency(AnalysisType::kMax, clockVariation(AnalysisType::kMax), TransType::kFall, .4);
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
    for (auto edge : {TransType::kRise, TransType::kFall})
      state.waveforms[edge].arrival_map[mode][edge] = clock.get_source_latency(mode, clockVariation(mode), edge);
  for (const auto& result : ClockChecker(db).check()) {
    near(result.actual, result.type == TimingCheckType::kPeriod ? 2. : 1., "ideal latency CRPR missing");
    near(result.reconvergence, .1, "wrong ideal latency CRPR");
  }
  // Local propagated clocks do not receive the ideal-clock latency correction.
  db.sdc().clockPinConstraints()["block/ff:CK"][""].propagated = true;
  for (const auto& result : ClockChecker(db).check()) {
    near(result.reconvergence, 0., "local propagated clock incorrectly received ideal latency CRPR");
    near(result.actual, result.type == TimingCheckType::kPeriod ? 1.9 : .9, "local propagated pulse separation incorrect");
  }
  db.sdc().clockPinConstraints().clear();
  // PT's single-pin pulse lookup uses the checked edge's slew on both
  // related/constrained axes, not the closing edge's different slew.
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
    state.waveforms[TransType::kFall].slew_map[mode][TransType::kFall] = .08;
  auto& tables = cell.get_sdf_check_arc_list().front().get_timing_arc_list().front().get_check_table_map();
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    auto& table = tables[edge];
    table.set_variable_type1(TimingTableVariableType::kConstrainedTransition);
    table.set_variable_type2(TimingTableVariableType::kInputTransition);
    table.set_axis_list({{.01, .1}, {.01, .1}});
    table.set_value_list(edge == TransType::kRise ? std::vector<double>{.63, .81, .72, .90} : std::vector<double>{.73, .91, .82, 1.});
  }
  for (const auto& result : ClockChecker(db).check())
    if (result.type == TimingCheckType::kWidth)
      near(result.required, result.edge == TransType::kRise ? 1.125 : 1.504, "pulse lookup used the closing edge slew");
  // Conditional Liberty checks disappear when their when is false.
  LogicExpressionTerm enable;
  enable.set_operation_type(LogicOperationType::kPort);
  enable.set_port_name("EN");
  LogicExpression condition;
  condition.set_term_list({enable});
  for (auto& arc : cell.get_sdf_check_arc_list())
    arc.get_timing_arc_list().front().set_when_expression(condition);
  db.graph().simValues().setValue("block/ff:EN", TimingCaseValue::kZero);
  if (!ClockChecker(db).check().empty())
    throw std::runtime_error("false clock check condition ignored");
  db.graph().simValues().setValue("block/ff:EN", TimingCaseValue::kOne);
  if (ClockChecker(db).check().size() != 4)
    throw std::runtime_error("true clock check condition lost");
  // A derived insertion-only clock must not become a leaf check candidate.
  state.is_source_path_only = true;
  if (!ClockChecker(db).check().empty())
    throw std::runtime_error("insertion-only clock created leaf checks");
}

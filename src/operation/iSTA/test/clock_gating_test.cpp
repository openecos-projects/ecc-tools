// SPDX-License-Identifier: MulanPSL-2.0
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

#include "ClockGatingChecker.hpp"
#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "MinTimingDataExporter.hpp"
#include "PathChecker.hpp"
#include "SparseTimingSnapshot.hpp"
#include "TimingDataExporter.hpp"
#include "sdc/ExceptionMatcher.hpp"
using namespace ista;
namespace {
void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}
void near(double actual, double expected, const char* message)
{
  require(std::abs(actual - expected) < 1e-9, message);
}
Database design(bool high, bool invert_clock, bool invert_output)
{
  Database db;
  auto& gate = db.network().instances()["gate"];
  gate.set_instance_name("gate");
  gate.set_cell_name("GATE");
  auto& ff = db.network().instances()["ff"];
  ff.set_instance_name("ff");
  ff.set_is_sequential(true);
  ff.set_clock_pin_name("ff:CK");
  for (const auto& name : {"gate:C", "gate:E", "gate:Y", "ff:CK"}) {
    const std::string full = name;
    auto& pin = db.network().pins()[full];
    pin.set_full_name(full);
    pin.set_instance_name(full.substr(0, full.find(':')));
    pin.set_pin_name(full.substr(full.find(':') + 1));
    db.graph().points()[full];
  }
  db.network().pins()["in"].set_is_port(true);
  auto& output = db.liberty().get_cell_map()["GATE"].get_port_map()["Y"];
  output.set_is_output(true);
  LogicExpression expression;
  auto term = [&](LogicOperationType op, const char* port = "") {
    LogicExpressionTerm value;
    value.set_operation_type(op);
    value.set_port_name(port);
    expression.get_term_list().push_back(value);
  };
  term(LogicOperationType::kPort, "C");
  if (invert_clock)
    term(LogicOperationType::kNot);
  term(LogicOperationType::kPort, "E");
  term(LogicOperationType::kNot);  // NAND2B-style inverted enable.
  term(high ? LogicOperationType::kAnd : LogicOperationType::kOr);
  if (invert_output)
    term(LogicOperationType::kNot);
  output.set_function_expression(expression);
  auto arc = [&](const char* from, const char* to, ArcType type) {
    Arc a;
    a.set_source_pin(from);
    a.set_sink_pin(to);
    a.set_type(type);
    auto i = db.graph().arcs().size();
    db.graph().arcs().push_back(a);
    db.graph().incomingArcs()[to].push_back(i);
    db.graph().outgoingArcs()[from].push_back(i);
  };
  arc("gate:C", "gate:Y", ArcType::kCell);
  arc("gate:E", "gate:Y", ArcType::kCell);
  arc("gate:Y", "ff:CK", ArcType::kNet);
  for (const auto* name : {"clk", "launch"}) {
    auto& clock = db.sdc().get_clock_map()[name];
    clock.set_clock_name(name);
    clock.set_period(10);
    clock.set_fall_edge(3);
  }
  for (const auto* pin : {"gate:C", "gate:Y", "ff:CK"}) {
    auto& point = db.graph().points()[pin];
    point.set_is_clock_point(true);
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
      for (auto edge : {TransType::kRise, TransType::kFall}) {
        auto& wave = point.get_clock_state("clk").waveforms[edge];
        wave.arrival_map[mode][edge] = 0.;
        wave.slew_map[mode][edge] = .03;
      }
  }
  db.graph().endpoints().push_back("ff:D");  // Existing structural endpoint must survive reset.
  return db;
}

void checkExport(const Database& source, TimingPathState path)
{
  // Test both full STA and structural preparation, including the single-clock
  // case that must select the sparse contract solely because of this gate.
  for (bool structural : {false, true}) {
    for (bool single_clock : {false, true}) {
      for (auto exception : {std::optional<TimingExceptionType>{}, std::optional{TimingExceptionType::kMinDelay}, std::optional{TimingExceptionType::kMaxDelay},
                             std::optional{TimingExceptionType::kFalsePath}}) {
        DataManager::initInst();
        auto& db = STADM.getDatabase();
        db = source;
        if (single_clock) {
          db.sdc().get_clock_map().erase("launch");
          path.set_clock_name("clk");
        } else {
          path.set_clock_name("launch");
        }
        if (structural)
          for (auto& [name, point] : db.graph().points())
            for (auto& [clock, state] : point.get_clock_state_map()) {
              state.waveforms.clear();
              state.is_structural = true;
            }
        if (exception) {
          TimingException budget;
          budget.set_type(*exception);
          budget.set_delay(.4);
          budget.set_to_objects({{ExceptionObjectType::kPin, "gate:E"}});
          db.sdc().get_path_exception_list().push_back(budget);
        }
        ClockGatingChecker(db).infer();
        for (auto mode : {AnalysisType::kMax, AnalysisType::kMin})
          for (auto edge : {TransType::kRise, TransType::kFall}) {
            auto& state = db.graph().points().at("gate:E").get_path_state_map()[mode][PathSourceType::kInput][edge]["gate"];
            state = path;
            state.set_trans_type(edge);
            state.set_path_state_tag("gate");
            state.set_exception_state_list(ExceptionMatcher(db.sdc(), db.network()).initState("in", path.get_clock_name(), TransType::kRise, mode, edge));
            auto& launch = db.graph().points().at("ff:CK").get_path_state_map()[mode][PathSourceType::kRegister][edge]["clock"];
            launch.set_start_point("ff:CK");
            launch.set_clock_name("clk");
            launch.set_trans_type(edge);
            launch.set_arrival(0.);
            launch.set_path_state_tag("clock");
          }
        auto snapshot = TimingDataExporter::exportData(db, STADM.getConfig());
        if (structural) {
          exportMinTimingModel(snapshot, db, STADM.getConfig());
          SparseTimingExportAdapter::compileModel(snapshot);
        } else {
          exportMinTimingData(snapshot, db, STADM.getConfig());
          SparseTimingExportAdapter::compile(snapshot);
        }
        require(snapshot.schema_version == 3 && snapshot.sparse.enabled, "gate check did not select sparse export");
        for (const auto& message : snapshot.sparse.unsupported)
          require(false, message.c_str());
        for (const auto& message : snapshot.sparse_min.unsupported)
          require(false, message.c_str());
        require(snapshot.timing_check_arcs.empty(), "inferred gate fabricated a Liberty constraint LUT");
        if (exception == TimingExceptionType::kFalsePath) {
          require(snapshot.sparse.checks.empty() && snapshot.min_contexts.empty(), "false-path gate leaked into optimization");
        } else {
          require(snapshot.sparse.checks.size() == 2 && snapshot.min_contexts.size() == 2, "gate lost a setup/hold transition");
          DelayCalculator delay(db, STADM.getConfig());
          PathChecker checker(db, delay);
          for (const auto& rule : snapshot.sparse.checks) {
            const auto edge = rule.transition == 0 ? TransType::kRise : TransType::kFall;
            const auto& state = db.graph().points().at("gate:E").get_path_state_map()[AnalysisType::kMax][PathSourceType::kInput][edge]["gate"];
            const auto expected = checker.checkPath(state, "gate:E", AnalysisType::kMax);
            require(expected && rule.valid && rule.endpoint_arc == -1, "gate opening rule lost its native context");
            near(rule.check_coefficient, 0., "gate opening rule requires a fabricated LUT");
            near(rule.intercept_ps, expected->required() * snapshot.time_unit_ps, "gate setup export changed required time");
            near(rule.reference_required_ps, rule.intercept_ps, "gate setup model disagrees with native reference");
          }
          for (const auto& context : snapshot.min_contexts) {
            const auto edge = context.data_transition == 1 ? TransType::kRise : TransType::kFall;
            const auto& state = db.graph().points().at("gate:E").get_path_state_map()[AnalysisType::kMin][PathSourceType::kInput][edge]["gate"];
            const auto expected = checker.checkPath(state, "gate:E", AnalysisType::kMin);
            require(expected && context.check_type == static_cast<int32_t>(TimingCheckType::kClockGatingHold), "gate hold lost its check class");
            require(context.capture_pin == "gate:C" && context.capture_clock == expected->captureClock(), "gate hold lost capture identity");
            near(context.required_ps, expected->required() * snapshot.time_unit_ps, "gate hold export changed required time");
            near(context.normal_required_ps, expected->check().required * snapshot.time_unit_ps, "gate hold lost the unadjusted window");
          }
          auto pin = std::find_if(snapshot.pins.begin(), snapshot.pins.end(), [](const auto& p) { return p.name == "gate:E"; });
          require(
              pin != snapshot.pins.end() && pin->max_qualification.valid == std::array{true, true} && pin->min_qualification.valid == std::array{true, true},
              "exported gate rules did not qualify both endpoint transitions");
        }
        DataManager::destroyInst();
      }
    }
  }
}

void checkGate(bool high, bool invert_clock, bool invert_output)
{
  auto db = design(high, invert_clock, invert_output);
  ClockGatingChecker(db).infer();
  require(db.graph().clockGatingChecks().at("gate:E").size() == 2, "missing inferred setup/hold pair");
  require(TimingStateQuery(db).isConstrainedEndPoint("gate:E"), "inferred gate is not a constrained endpoint");
  Config config;
  DelayCalculator delay(db, config);
  PathChecker checker(db, delay);
  TimingPathState path;
  path.set_start_point("in");
  path.set_clock_name("launch");
  path.set_launch_clock_trans_type(TransType::kRise);
  path.set_trans_type(TransType::kRise);
  path.set_slew(.03);
  const bool active_high = high != invert_clock;
  for (double launch : {0., 3., 7.}) {
    db.sdc().get_clock_map().at("launch").set_rise_edge(launch);
    db.sdc().get_clock_map().at("launch").set_fall_edge(launch + 2.);
    path.set_arrival(launch + .2);
    path.set_launch_time(launch);
    const auto hold = checker.checkPath(path, "gate:E", AnalysisType::kMin);
    const auto setup = checker.checkPath(path, "gate:E", AnalysisType::kMax);
    require(hold && setup, "clock gate disappeared from path checking");
    const double hold_edge = active_high ? 3. : launch < 3. ? 0. : 10.;
    const double setup_edge = active_high ? 10. : launch < 3. ? 3. : 13.;
    near(hold->required(), hold_edge, "wrong gate closing edge");
    near(setup->required(), setup_edge, "wrong gate opening edge");
    require(hold->check().type == TimingCheckType::kClockGatingHold, "gate lost its report identity");
  }
  TimingException budget;
  budget.set_type(TimingExceptionType::kMinDelay);
  budget.set_delay(.4);
  budget.set_to_objects({{ExceptionObjectType::kPin, "gate:E"}});
  db.sdc().get_path_exception_list().push_back(budget);
  path.set_exception_state_list(ExceptionMatcher(db.sdc(), db.network()).initState("in", "launch", TransType::kRise, AnalysisType::kMin, TransType::kRise));
  near(checker.checkPath(path, "gate:E", AnalysisType::kMin)->required(), 7.4, "gate min-delay margin contains an extra clock cycle");
  db.sdc().get_path_exception_list().front().set_type(TimingExceptionType::kFalsePath);
  require(!checker.checkPath(path, "gate:E", AnalysisType::kMin), "false path did not disable gate check");
  ClockGatingChecker(db).infer();
  require(std::count(db.graph().endpoints().begin(), db.graph().endpoints().end(), "gate:E") == 1, "reanalysis duplicated gate endpoint");
  Graph copied = db.graph();
  copied.clearTiming();
  require(copied.clockGatingChecks().empty() && copied.endpoints() == std::vector<std::string>{"ff:D"}, "graph copy/reset retained derived endpoint");
  require(db.graph().clockGatingChecks().size() == 1, "graph copy reset mutated source");
  db.sdc().get_path_exception_list().clear();
  path.set_exception_state_list({});
  require(checker.checkPath(path, "gate:E", AnalysisType::kMin).has_value(), "export fixture has no native gate check");
  checkExport(db, path);
  db.graph().points().at("gate:E").get_path_state_map()[AnalysisType::kMin][PathSourceType::kInput][TransType::kRise][path.get_path_state_tag()] = path;
  // Model preparation has clock topology but deliberately no numeric waveforms.
  for (const auto& id : db.graph().points().sortedIds())
    for (auto& [clock, state] : db.graph().points().get(id).get_clock_state_map()) {
      state.waveforms.clear();
      state.is_structural = true;
    }
  ClockGatingChecker(db).infer();
  require(db.graph().clockGatingChecks().size() == 1, "structural model silently lost gate endpoints");
  db.graph().simValues().setValue("gate:E", TimingCaseValue::kZero);
  ClockGatingChecker(db).infer();
  require(db.graph().clockGatingChecks().empty(), "constant enable kept a gate check");
  db.graph().simValues().clear();
  db.graph().arcs().back().set_is_disable_arc(true);
  ClockGatingChecker(db).infer();
  require(db.graph().clockGatingChecks().empty(), "gate with no active sequential clock fanout was checked");
}
}  // namespace
int main()
{
  for (bool high : {true, false})
    for (bool inverted_clock : {false, true})
      for (bool inverted_output : {false, true})
        checkGate(high, inverted_clock, inverted_output);
  {
    auto db = design(true, false, false);
    // Exported clocks qualify even without a generated-clock definition or a
    // downstream register. Ordinary data fanout does not qualify.
    db.network().instances().at("ff").set_is_sequential(false);
    ClockGatingChecker(db).infer();
    require(db.graph().clockGatingChecks().empty(), "ordinary data fanout enabled gate checks");
    auto& output = db.network().pins().at("ff:CK");
    output.set_is_port(true);
    output.set_direction(PinDirection::kOutput);
    ClockGatingChecker(db).infer();
    require(db.graph().clockGatingChecks().contains("gate:E"), "exported clock lost gate checks without a generated clock");
    db.graph().arcs().back().set_is_disable_arc(true);
    ClockGatingChecker(db).infer();
    require(db.graph().clockGatingChecks().empty(), "disabled output net enabled gate checks");
    db.graph().arcs().back().set_is_disable_arc(false);
    auto& root = db.sdc().get_clock_map()["independent"];
    root.set_clock_name("independent");
    root.set_source_objects({"ff:CK"});
    ClockGatingChecker(db).infer();
    require(db.graph().clockGatingChecks().empty(), "output primary-clock boundary enabled upstream gate checks");
  }
  {
    auto db = design(true, false, false);
    ClockGatingChecker(db).infer();
    Config config;
    DelayCalculator delay(db, config);
    PathChecker checker(db, delay);
    TimingPathState path;
    path.set_start_point("in");
    path.set_clock_name("launch");
    path.set_launch_clock_trans_type(TransType::kRise);
    path.set_trans_type(TransType::kRise);
    auto& launch = db.sdc().get_clock_map().at("launch");
    launch.set_period(6.);
    for (const auto [phase, required] : {std::pair{0., 3.}, std::pair{1., 3.}, std::pair{3., 5.}}) {
      launch.set_rise_edge(phase);
      launch.set_fall_edge(phase + 2.);
      near(checker.checkPath(path, "gate:E", AnalysisType::kMin)->required(), required, "different periods lost the worst gate hold window across cycles");
    }
    launch.set_period(10.);
    db.sdc().get_clock_map().at("clk").set_waveform({0., 2., 6., 8.});
    for (const auto [phase, required] : {std::pair{0., 2.}, std::pair{3., 2.}, std::pair{6., 8.}, std::pair{9., 8.}}) {
      launch.set_rise_edge(phase);
      launch.set_fall_edge(phase + .5);
      near(checker.checkPath(path, "gate:E", AnalysisType::kMin)->required(), required, "multi-pulse clock paired gate hold with another active window");
    }
  }
  for (bool high : {true, false}) {
    auto db = design(high, false, false);
    auto& capture = db.sdc().get_clock_map().at("clk");
    capture.set_period(6.);
    capture.set_fall_edge(2.);
    ClockGatingChecker(db).infer();
    Config config;
    DelayCalculator delay(db, config);
    PathChecker checker(db, delay);
    TimingPathState path;
    path.set_start_point("in");
    path.set_clock_name("launch");
    path.set_launch_clock_trans_type(TransType::kRise);
    path.set_trans_type(TransType::kRise);
    for (double phase : {0., 1., 3.}) {
      auto& launch = db.sdc().get_clock_map().at("launch");
      launch.set_rise_edge(phase);
      launch.set_fall_edge(phase + .5);
      const double required = high ? (phase < 3. ? 2. : 4.) : (phase < 3. ? 4. : 6.);
      near(checker.checkPath(path, "gate:E", AnalysisType::kMin)->required(), required, "reverse period ratio selected a wrong hold cycle");
    }
  }
  {
    auto db = design(true, false, false);
    auto& state = db.graph().points().at("gate:C").get_clock_state("clk");
    for (auto source : {TransType::kRise, TransType::kFall}) {
      auto& wave = state.waveforms[source];
      wave.arrival_map.clear();
      wave.slew_map.clear();
      for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
        const auto local = source == TransType::kRise ? TransType::kFall : TransType::kRise;
        wave.arrival_map[mode][local] = 0.;
        wave.slew_map[mode][local] = .03;
      }
    }
    ClockGatingChecker(db).infer();
    Config config;
    DelayCalculator delay(db, config);
    PathChecker checker(db, delay);
    TimingPathState path;
    path.set_start_point("in");
    path.set_clock_name("launch");
    path.set_launch_clock_trans_type(TransType::kRise);
    path.set_trans_type(TransType::kRise);
    auto check = checker.checkPath(path, "gate:E", AnalysisType::kMin);
    require(check && check->check().capture_transition == TransType::kRise && check->check().capture_pin_transition == TransType::kFall,
            "inverted clock lost local/source edge identity");
    near(check->required(), 0., "inverted clock closed on the wrong source edge");
  }
  auto db = design(true, false, false);
  auto& second = db.sdc().get_clock_map()["second"];
  second.set_clock_name("second");
  second.set_period(20);
  second.set_fall_edge(9);
  auto& input = db.graph().points().at("gate:C");
  input.get_clock_state("second") = input.get_clock_state("clk");
  ClockGatingChecker(db).infer();
  require(db.graph().clockGatingChecks().at("gate:E").size() == 2, "unreachable capture clock added a gate check");
  Config config;
  DelayCalculator delay(db, config);
  PathChecker checker(db, delay);
  TimingPathState path;
  path.set_start_point("in");
  path.set_clock_name("launch");
  path.set_launch_clock_trans_type(TransType::kRise);
  path.set_trans_type(TransType::kRise);
  auto checks = checker.checkPathCandidates(path, "gate:E", AnalysisType::kMin);
  require(checks.size() == 1 && checks.front().captureClock() == "clk", "gate borrowed another clock on the same input pin");
  auto old = db.graph().points().at("ff:CK").get_clock_state("clk");
  db.graph().points().at("ff:CK").clearClockStates();
  db.graph().points().at("ff:CK").get_clock_state("second") = old;
  ClockGatingChecker(db).infer();
  require(db.graph().clockGatingChecks().empty(), "gate traversed a separate primary-clock boundary");
  db.graph().points().at("gate:Y").clearClockStates();
  db.graph().points().at("gate:Y").get_clock_state("second") = old;
  input.get_clock_state_map().erase("second");
  ClockGatingChecker(db).infer();
  require(db.graph().clockGatingChecks().empty(), "gate traversed a new primary clock at its output");
  db.network().instances().at("ff").set_is_sequential(false);
  auto& generated = db.sdc().get_clock_map()["generated"];
  generated.set_clock_name("generated");
  generated.set_master_clock_name("clk");
  generated.set_source_list({"gate:Y"});
  ClockGatingChecker(db).infer();
  require(db.graph().clockGatingChecks().contains("gate:E"), "exported generated clock lost upstream gate checks without register fanout");
}

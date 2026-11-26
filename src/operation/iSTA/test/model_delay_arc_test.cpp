// SPDX-License-Identifier: MulanPSL-2.0
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "DelayArcExtractor.hpp"
#include "DelayCalculator.hpp"
#include "Logger.hpp"
#include "PathAnalyzer.hpp"
#include "PathChecker.hpp"
#include "sdc/ExceptionMatcher.hpp"

namespace {
using namespace ista;
using ArcKey = std::tuple<std::string, std::string, std::string, std::string>;
using Tables = std::map<std::string, double>;
using Arcs = std::map<ArcKey, Tables>;

void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

void addPort(Database& db, const std::string& name, PinDirection direction)
{
  auto& pin = db.network().pins()[name];
  pin.set_full_name(name);
  pin.set_pin_name(name);
  pin.set_is_port(true);
  pin.set_direction(direction);
}

void addOutput(Database& db, const std::string& name)
{
  addPort(db, name, PinDirection::kOutput);
  db.graph().endpoints().push_back(name);
  std::vector<TimingIoDelay> delays;
  for (auto analysis : {AnalysisType::kMax, AnalysisType::kMin}) {
    for (auto transition : {TransType::kRise, TransType::kFall}) {
      TimingIoDelay delay;
      delay.set_clock_name("clk");
      delay.set_analysis_type(analysis);
      delay.set_trans_type(transition);
      delay.set_delay(0.0);
      delays.push_back(delay);
    }
  }
  db.sdc().get_port_constraint_map()[name].set_output_delays(delays, false);
}

void addPath(Database& db, const std::string& from, const std::string& to, PathSourceType source, TransType input, TransType output, double maximum,
             double minimum)
{
  const auto index = db.graph().arcs().size();
  Arc arc;
  arc.set_source_pin(from);
  arc.set_sink_pin(to);
  arc.set_type(ArcType::kNet);
  db.graph().arcs().push_back(arc);
  const std::string tag = "path_" + std::to_string(index);
  const ExceptionMatcher matcher(db.sdc(), db.network());
  for (auto analysis : {AnalysisType::kMax, AnalysisType::kMin}) {
    TimingPathState start;
    start.set_start_point(from);
    start.set_clock_name("clk");
    start.set_trans_type(input);
    start.set_path_state_tag(tag);
    start.set_arrival(0.0);
    start.set_exception_state_list(matcher.initState(from, "clk", input, analysis, TransType::kRise));
    db.graph().points()[from].get_path_state_map()[analysis][source][input][tag] = start;
    auto end = start;
    end.set_trans_type(output);
    end.set_predecessor(from);
    end.set_predecessor_arc_idx(index);
    end.set_predecessor_trans_type(input);
    end.set_predecessor_path_state_tag(tag);
    end.set_predecessor_arc_delay(analysis == AnalysisType::kMax ? maximum : minimum);
    end.set_arrival(analysis == AnalysisType::kMax ? maximum : minimum);
    db.graph().points()[to].get_path_state_map()[analysis][source][output][tag] = end;
  }
}

Arcs extract(Database& db, AnalysisType analysis)
{
  Config config;
  DelayCalculator delay(db, config);
  PathChecker check(db, delay);
  CharacterizedLibrary library;
  DelayArcExtractor(db).extract(library, analysis, check);
  Arcs result;
  for (auto& arc : library.get_timing_arc_list()) {
    Tables tables;
    for (auto& table : arc.get_scalar_table_list())
      tables.emplace(table.get_table_name(), table.get_value());
    require(result.emplace(ArcKey{arc.get_source_port(), arc.get_sink_port(), arc.get_timing_type(), arc.get_timing_sense()}, tables).second,
            "duplicate delay arc");
  }
  return result;
}

Tables both(double rise, double fall)
{
  return {{"cell_rise", rise}, {"cell_fall", fall}, {"rise_transition", 0.0}, {"fall_transition", 0.0}};
}

void testExtraction()
{
  Database db;
  Config config;
  // Propagated states are complete; reporting must not repair or mutate them.
  db.graph().setPathStatesPruned(true);
  addPort(db, "clk", PinDirection::kInput);
  auto& clock = db.sdc().get_clock_map()["clk"];
  clock.set_clock_name("clk");
  clock.set_source_list({"clk"});
  clock.set_period(10.0);
  for (const auto* name : {"a", "b", "disabled"})
    addPort(db, name, PinDirection::kInput);
  for (const auto* name : {"comb", "inverted", "mixed", "registered", "rise_only"})
    addOutput(db, name);
  auto& ff = db.network().instances()["ff"];
  ff.set_is_sequential(true);
  ff.set_output_pin_name("ff:Q");
  auto& q = db.network().pins()["ff:Q"];
  q.set_full_name("ff:Q");
  q.set_pin_name("Q");
  q.set_instance_name("ff");
  TimingArc trigger;
  trigger.set_trigger_trans_type(TransType::kRise);
  ff.get_clock_to_q_arc().get_timing_arc_list().push_back(trigger);

  TimingException cut;
  cut.set_type(TimingExceptionType::kFalsePath);
  cut.set_from_objects({{ExceptionObjectType::kPin, "disabled"}});
  db.sdc().get_path_exception_list().push_back(cut);
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    const bool rise = edge == TransType::kRise;
    addPath(db, "a", "comb", PathSourceType::kInput, edge, edge, rise ? 3.0 : 5.0, rise ? 0.25 : 0.5);
    addPath(db, "b", "comb", PathSourceType::kInput, edge, edge, rise ? 1.0 : 2.0, rise ? 0.75 : 1.0);
    addPath(db, "disabled", "comb", PathSourceType::kInput, edge, edge, 99.0, -99.0);
    addPath(db, "a", "inverted", PathSourceType::kInput, rise ? TransType::kFall : TransType::kRise, edge, rise ? 2.0 : 4.0, rise ? 0.5 : 1.0);
    addPath(db, "a", "mixed", PathSourceType::kInput, edge, edge, 2.0, 1.0);
    addPath(db, "a", "mixed", PathSourceType::kInput, rise ? TransType::kFall : TransType::kRise, edge, 4.0, 0.5);
    addPath(db, "ff:Q", "registered", PathSourceType::kRegister, edge, edge, rise ? 3.0 : 6.0, rise ? 1.0 : 2.0);
  }
  addPath(db, "a", "rise_only", PathSourceType::kInput, TransType::kRise, TransType::kRise, 2.0, 0.5);
  DelayCalculator delay(db, config);
  PathChecker check(db, delay);
  PathAnalyzer(db, check).analyze();
  const auto reports = [&] {
    std::vector<std::tuple<std::string, AnalysisType, TransType, double>> result;
    for (auto& group : db.pathGroups())
      for (auto& [end, paths] : group.endpointPaths())
        for (auto& path : paths)
          result.emplace_back(end, path.get_analysis_type(), path.get_trans_type(), path.get_path_delay());
    return result;
  };
  const auto before = reports();
  require(before.size() == 10, "reporting must retain one path per analysis/source/endpoint");
  const Arcs maximum{{{"a", "comb", "combinational", "positive_unate"}, both(3.0, 5.0)},
                     {{"b", "comb", "combinational", "positive_unate"}, both(1.0, 2.0)},
                     {{"a", "inverted", "combinational", "negative_unate"}, both(2.0, 4.0)},
                     {{"a", "mixed", "combinational", "non_unate"}, both(4.0, 4.0)},
                     {{"clk", "registered", "rising_edge", "non_unate"}, both(3.0, 6.0)},
                     {{"a", "rise_only", "combinational", "positive_unate"}, {{"cell_rise", 2.0}, {"rise_transition", 0.0}}}};
  const Arcs minimum{{{"a", "comb", "combinational", "positive_unate"}, both(0.25, 0.5)},
                     {{"b", "comb", "combinational", "positive_unate"}, both(0.75, 1.0)},
                     {{"a", "inverted", "combinational", "negative_unate"}, both(0.5, 1.0)},
                     {{"a", "mixed", "combinational", "non_unate"}, both(0.5, 0.5)},
                     {{"clk", "registered", "rising_edge", "non_unate"}, both(1.0, 2.0)},
                     {{"a", "rise_only", "combinational", "positive_unate"}, {{"cell_rise", 0.5}, {"rise_transition", 0.0}}}};
  require(extract(db, AnalysisType::kMax) == maximum, "maximum Liberty arcs lost a source, transition, delay or sense");
  require(extract(db, AnalysisType::kMin) == minimum, "minimum Liberty arcs lost a source, transition, delay or sense");
  require(extract(db, AnalysisType::kMax) == maximum && reports() == before, "extraction changed reports or retained stale results");
}
}  // namespace

int main()
{
  Logger::initInst();
  testExtraction();
  Logger::destroyInst();
}

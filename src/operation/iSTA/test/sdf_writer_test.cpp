// SPDX-License-Identifier: MulanPSL-2.0
#include <sstream>
#include <stdexcept>
#include <string>

#include "SdfValueExtractor.hpp"
#include "SdfWriter.hpp"
#include "graph/TimingStateQuery.hpp"

static void storedWireDelay()
{
  using namespace ista;
  Database db;
  Config config;
  db.network().setName("independent");
  auto& arc = db.graph().arcs().emplace_back();
  arc.set_type(ArcType::kNet);
  arc.set_source_pin("in");
  arc.set_sink_pin("out");
  for (const auto* name : {"in", "out"}) {
    auto& pin = db.network().pins()[name];
    pin.set_is_port(true);
    pin.set_pin_name(name);
  }
  arc.get_graph_delay_map()[AnalysisType::kMax][TransType::kRise][TransType::kRise] = 0.25;
  arc.get_input_output_delay_map()[AnalysisType::kMax][TransType::kRise][TransType::kRise] = 9.0;
  SdfValueExtractor values(db);
  if (values.arcDelay(arc).get_rise_max_delay() != 0.25)
    throw std::runtime_error("stored graph delay lost priority");
  arc.setRcFailed(AnalysisType::kMax, TransType::kRise);
  if (TimingStateQuery(db).getOutputTransTypeList(arc, AnalysisType::kMax, TransType::kRise).empty()
      || TimingStateQuery(db).getArcDelay(arc, AnalysisType::kMax, TransType::kRise, TransType::kRise) != 0.)
    throw std::runtime_error("failed RC must retain an explicit zero-delay nonbounding path");
  if (values.arcDelay(arc).get_rise_max_delay() != 0.)
    throw std::runtime_error("failed RC SDF did not emit the explicit placeholder");
  arc.setRcFailed(AnalysisType::kMax, TransType::kFall);
  if (TimingStateQuery(db).maxDataArcDelay(arc) != 0.)
    throw std::runtime_error("failed RC scalar query reused a stale calculated delay");
  arc.clearRcFailures();
  SdfWriter writer(db, config);
  std::ostringstream first;
  writer.write(first);
  if (first.str().find("(DESIGN \"independent\")") == std::string::npos
      || first.str().find("(INTERCONNECT in out (0.2500000000::0.2500000000))") == std::string::npos)
    throw std::runtime_error("independent SDF stream output changed");
  arc.get_graph_delay_map().clear();
  if (values.arcDelay(arc).get_rise_max_delay() != 9.0)
    throw std::runtime_error("stored delay fallback lost");
  std::ostringstream second;
  writer.write(second);
  if (second.str().find("(INTERCONNECT in out (9.0000000000::9.0000000000))") == std::string::npos)
    throw std::runtime_error("repeated write retained an old value");
}

static void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}

static void cellArcAndCheckValues()
{
  using namespace ista;
  Database db;
  Config config;
  auto& instance = db.network().instances()["u"];
  instance.set_instance_name("u");
  instance.set_cell_name("gate");
  for (const auto* port : {"A", "Y", "CK", "D"}) {
    auto& pin = db.network().pins()[std::string("u:") + port];
    pin.set_pin_name(port);
    pin.set_instance_name("u");
  }
  auto& cell = db.liberty().get_cell_map()["gate"];
  auto& cell_arc = cell.get_cell_arc_list().emplace_back();
  cell_arc.set_source_port("A");
  cell_arc.set_sink_port("Y");
  cell_arc.set_is_timing_graph_arc(true);
  for (int index : {0, 1}) {
    auto& timing = cell_arc.get_timing_arc_list().emplace_back();
    timing.set_arc_idx(index);
    timing.set_sdf_cond("A == 1'b1");
    timing.set_sense(TimingArcSense::kNonUnate);
  }
  auto& arc = db.graph().arcs().emplace_back();
  arc.set_type(ArcType::kCell);
  arc.set_owner_name("u");
  arc.set_source_pin("u:A");
  arc.set_sink_pin("u:Y");
  arc.setLibertyArcId(LibertyArcId{"gate", 0});
  for (int index : {0, 1}) {
    auto& values = arc.get_timing_arc_delay_map()[index];
    values[AnalysisType::kMin][TransType::kRise][TransType::kRise] = 3.0 - index;
    values[AnalysisType::kMax][TransType::kRise][TransType::kRise] = 4.0 + index;
    values[AnalysisType::kMin][TransType::kFall][TransType::kFall] = 6.0 - index;
    values[AnalysisType::kMax][TransType::kFall][TransType::kFall] = 7.0 + index;
  }
  SdfValueExtractor extractor(db);
  auto records = extractor.graphCellArcDelays(arc);
  require(records.size() == 2, "non-unate input edges or same-condition reduction changed");
  auto& rise = records.at({"A == 1'b1", TransType::kRise});
  auto& fall = records.at({"A == 1'b1", TransType::kFall});
  require(rise.get_rise_min_delay() == 2.0 && rise.get_rise_max_delay() == 5.0 && !rise.get_fall_min_delay(), "rise reduction changed");
  require(fall.get_fall_min_delay() == 5.0 && fall.get_fall_max_delay() == 8.0 && !fall.get_rise_max_delay(), "fall reduction changed");

  TimingCheckArc setup;
  setup.set_data_port("D");
  setup.set_clock_port("CK");
  setup.set_clock_trans_type(TransType::kRise);
  setup.set_check_type(TimingCheckType::kSetup);
  setup.set_check_time(2.0);
  cell.get_sdf_check_arc_list().push_back(setup);
  auto hold = setup;
  hold.set_check_type(TimingCheckType::kHold);
  hold.set_check_time(-3.0);
  cell.get_sdf_check_arc_list().push_back(hold);
  TimingArc default_arc;
  const auto hold_values = extractor.checkValues(instance, hold, default_arc, TransType::kRise);
  require(hold_values.minimum == -2.0 && hold_values.maximum == -2.0, "HOLD/SETUP adjustment changed");

  auto width = setup;
  width.set_data_port("CK");
  width.set_check_type(TimingCheckType::kWidth);
  TimingArc table_arc;
  TimingTable table;
  table.set_variable_type1(TimingTableVariableType::kInputTransition);
  table.set_axis_list({{0.0, 1.0}});
  table.set_value_list({4.0, 2.0});
  table_arc.get_check_table_map()[TransType::kRise] = table;
  width.get_timing_arc_list().push_back(table_arc);
  auto& clock = db.graph().points()["u:CK"];
  clock.setLaunchClock("clock");
  clock.setDefaultClock("clock");
  clock.get_clock_state("clock").slew_map[AnalysisType::kMin][TransType::kRise] = 0.0;
  clock.get_clock_state("clock").slew_map[AnalysisType::kMax][TransType::kRise] = 1.0;
  auto width_values = extractor.checkValues(instance, width, table_arc, TransType::kRise);
  require(width_values.minimum == 2.0 && width_values.maximum == 4.0, "WIDTH reversed corners were not ordered");
  cell.get_sdf_check_arc_list().push_back(width);
  auto period = width;
  period.set_check_type(TimingCheckType::kPeriod);
  auto period_values = extractor.checkValues(instance, period, table_arc, TransType::kRise);
  require(period_values.minimum == 4.0 && period_values.maximum == 2.0, "PERIOD corner order changed");
  cell.get_sdf_check_arc_list().push_back(period);

  std::ostringstream out;
  SdfWriter(db, config).write(out);
  for (const auto* expected :
       {"(COND A == 1'b1 (IOPATH (posedge A) Y (2.0000000000::5.0000000000)))", "(COND A == 1'b1 (IOPATH (negedge A) Y () (5.0000000000::8.0000000000)))",
        "(HOLD (posedge D) (posedge CK) (-2.0000000000::-2.0000000000))", "(WIDTH (posedge CK) (2.0000000000::4.0000000000))",
        "(PERIOD CK (4.0000000000::2.0000000000))"})
    require(out.str().find(expected) != std::string::npos, expected);
}

class FailingBuffer : public std::streambuf
{
  std::size_t remaining = 32;
  int_type overflow(int_type character) override
  {
    if (!remaining--)
      return traits_type::eof();
    return character;
  }
};

static void writeFailures()
{
  ista::Database db;
  ista::Config config;
  ista::SdfWriter writer(db, config);
  for (bool initially_bad : {false, true}) {
    FailingBuffer buffer;
    std::ostream stream(&buffer);
    if (initially_bad)
      stream.setstate(std::ios::badbit);
    bool failed = false;
    try {
      writer.write(stream);
    } catch (const std::exception&) {
      failed = true;
    }
    require(failed, "SDF writer reported success on a failed stream");
  }
}

int main()
{
  writeFailures();
  storedWireDelay();
  cellArcAndCheckValues();
}

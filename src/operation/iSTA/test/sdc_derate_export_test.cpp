// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <sstream>

#include "SdcTestDesign.hpp"
#include "SdfValueExtractor.hpp"
#include "SdfWriter.hpp"
#include "graph/TimingStateQuery.hpp"

namespace {
void near(double actual, double expected, const std::string& message)
{
  require(std::abs(actual - expected) < 1e-12, message + ": got " + std::to_string(actual) + ", expected " + std::to_string(expected));
}

TimingTable constantTable(double value)
{
  TimingTable table;
  table.set_variable_type1(TimingTableVariableType::kInputTransition);
  table.set_axis_list({{0., 1.}});
  table.set_value_list({value, value});
  return table;
}

std::string exportedInstance(const std::string& name)
{
  std::ostringstream output;
  SdfWriter(STADM.getDatabase(), STADM.getConfig()).write(output);
  const auto text = output.str();
  std::string escaped;
  for (char character : name) {
    if (character == '/')
      escaped += '\\';
    escaped += character;
  }
  const auto start = text.find("(INSTANCE " + escaped + ")");
  require(start != std::string::npos, "missing exported instance " + name);
  const auto end = text.find("(INSTANCE ", start + 1);
  return text.substr(start, end == std::string::npos ? end : end - start);
}

void delayFactors()
{
  auto& db = STADM.getDatabase();
  eval("set_timing_derate -early 0.9");
  eval("set_timing_derate -late 1.1");
  eval("set_timing_derate -early -clock 0.7");
  eval("set_timing_derate -late -clock 1.8");
  auto& family = db.liberty().get_cell_map().at("COMB").get_cell_arc_list().front();
  family.set_is_timing_graph_arc(true);
  auto& variant = family.get_timing_arc_list().emplace_back();
  variant.set_arc_idx(0);
  variant.set_sense(TimingArcSense::kPositive);
  auto& arc = db.graph().arcs().emplace_back();
  arc.set_type(ArcType::kCell);
  arc.set_owner_name("cdc/buf");
  arc.set_source_pin("cdc/buf:A");
  arc.set_sink_pin("cdc/buf:Y");
  arc.setLibertyArcId(LibertyArcId{"COMB", 0});
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
    arc.get_graph_delay_map()[mode][TransType::kRise][TransType::kRise] = -2.;
    arc.get_graph_delay_map()[mode][TransType::kFall][TransType::kFall] = 4.;
  }
  const auto nominal = arc.get_graph_delay_map();
  arc.get_input_output_delay_map() = nominal;
  arc.get_timing_arc_delay_map()[0] = nominal;
  auto& point = db.graph().points()["cdc/buf:A"];
  point.get_data_slew_map()[AnalysisType::kMin][TransType::kRise] = .25;
  const auto slews = point.get_data_slew_map();
  SdfValueExtractor values(db);
  TimingStateQuery query(db);
  near(query.getDataArcDelay(arc, AnalysisType::kMin, TransType::kRise, TransType::kRise), -2.2, "early data propagation");
  near(query.deratedDelay(-2., AnalysisType::kMax, true, TransType::kRise, DerateDelay::kCell, "cdc/buf:A"), -.4,
       "independent late clock factor");
  // Each path must consume the factor once, including both cache fallbacks.
  for (int source = 0; source < 3; ++source) {
    if (source == 1)
      arc.get_timing_arc_delay_map().clear();
    if (source == 2)
      arc.get_graph_delay_map().clear();
    for (int repeat = 0; repeat < 2; ++repeat) {
      auto records = values.graphCellArcDelays(arc);
      require(records.size() == 1, "single cell arc split during export");
      auto& delay = records.begin()->second;
      near(*delay.get_rise_min_delay(), -2.2, "negative early SDF delay");
      near(*delay.get_rise_max_delay(), -1.8, "negative late SDF delay");
      near(*delay.get_fall_min_delay(), 3.6, "positive early SDF delay");
      near(*delay.get_fall_max_delay(), 4.4, "positive late SDF delay");
    }
    const auto text = exportedInstance("cdc/buf");
    require(text.find("(IOPATH A Y (-2.2000000000::-1.8000000000) (3.6000000000::4.4000000000))") != std::string::npos,
            "serialized cell derate changed");
    require(arc.get_input_output_delay_map() == nominal && point.get_data_slew_map() == slews,
            "SDF export changed nominal cache or slew");
    if (source == 0)
      require(arc.get_timing_arc_delay_map().at(0) == nominal && arc.get_graph_delay_map() == nominal,
              "SDF export rewrote graph or variant delays");
  }

  Arc wire;
  wire.set_type(ArcType::kNet);
  wire.set_source_pin("cdc/buf:Y");
  wire.set_sink_pin("cdc/reconv:A");
  wire.get_input_output_delay_map()[AnalysisType::kMax][TransType::kRise][TransType::kRise] = 3.;
  eval("set_timing_derate -late -net_delay -rise 1.4 [get_nets buf_net]");
  near(*values.arcDelay(wire).get_rise_max_delay(), 4.2, "scoped net fallback factor");
  wire.setRcFailed(AnalysisType::kMax, TransType::kRise);
  near(*values.arcDelay(wire).get_rise_max_delay(), 0., "failed RC placeholder was derated into a delay");

  // A large factor reverses the order of negative alternatives. Reduction must
  // see the varied values, not just vary the nominal maximum afterwards.
  eval("set_timing_derate -late -data -cell_delay -rise 3.0");
  arc.get_graph_delay_map()[AnalysisType::kMax][TransType::kRise][TransType::kRise] = -3.;
  arc.get_graph_delay_map()[AnalysisType::kMax][TransType::kFall][TransType::kRise] = -1.;
  near(*values.arcDelay(arc).get_rise_max_delay(), 3., "derate applied after alternative reduction");
}

void fallbackFactors()
{
  auto& db = STADM.getDatabase();
  db.network().hierarchyPins()["cdc:boundary"] = {"cdc", "boundary", PinDirection::kInput, "clock_net"};
  auto& family = db.liberty().get_cell_map().at("COMB").get_cell_arc_list().front();
  family.set_is_timing_graph_arc(false);
  family.set_delay_min(2.);
  family.set_delay_max(2.);
  eval("set_timing_derate -early 0.9");
  eval("set_timing_derate -late 1.1");
  eval("set_timing_derate -late -cell_delay 1.2 [get_cells cdc]");
  eval("set_timing_derate -late 1.4 [get_lib_cells */COMB]");
  eval("set_timing_derate -late -increment 0.05");
  eval("set_timing_derate -late -increment 0.1 [get_cells cdc/buf]");
  for (bool use_table : {false, true}) {
    if (use_table) {
      auto& variant = family.get_timing_arc_list().emplace_back();
      variant.set_sense(TimingArcSense::kPositive);
      for (auto edge : {TransType::kRise, TransType::kFall})
        variant.get_delay_table_map()[edge] = constantTable(2.);
    }
    for (int repeat = 0; repeat < 2; ++repeat) {
      require(exportedInstance("cdc/buf").find("(IOPATH A Y (1.8000000000::3.0000000000) (1.8000000000::3.0000000000))") != std::string::npos,
              "instance/library factors missing from non-graph cell export");
      require(exportedInstance("cdc/reconv").find("(IOPATH A Y (1.8000000000::2.9000000000) (1.8000000000::2.9000000000))") != std::string::npos,
              "local increment leaked into sibling instance");
    }
  }
  eval("set_timing_derate -late 1.2 [get_cells cdc/buf]");
  require(exportedInstance("cdc/buf").find("(IOPATH A Y (1.8000000000::2.6000000000) (1.8000000000::2.6000000000))") != std::string::npos,
          "instance baseline lost to library baseline");
  near(family.get_delay_min(), 2., "nominal scalar minimum changed");
  near(family.get_timing_arc_list().front().get_delay_table_map().at(TransType::kRise).findValue(0., 0.), 2.,
       "nominal table value changed");
}

void checkFactors()
{
  auto& db = STADM.getDatabase();
  auto& instance = db.network().instances().at("cdc/source_reg");
  TimingCheckArc check;
  check.set_data_port("D");
  check.set_clock_port("CK");
  check.set_clock_trans_type(TransType::kRise);
  check.set_check_type(TimingCheckType::kSetup);
  check.set_check_time(-2.);
  TimingArc variant;
  SdfValueExtractor values(db);
  eval("set_timing_derate -early 0.5");
  eval("set_timing_derate -late 1.5");
  auto result = values.checkValues(instance, check, variant, TransType::kRise);
  near(result.minimum, -2., "ordinary early derate leaked into timing check");
  near(result.maximum, -2., "ordinary late derate leaked into timing check");
  eval("set_timing_derate -early -cell_check -rise 0.8 [get_cells cdc/source_reg]");
  eval("set_timing_derate -late -cell_check -rise 1.3 [get_lib_cells */FF]");
  for (bool use_table : {false, true}) {
    if (use_table)
      for (auto edge : {TransType::kRise, TransType::kFall})
        variant.get_check_table_map()[edge] = constantTable(-2.);
    result = values.checkValues(instance, check, variant, TransType::kRise);
    near(result.minimum, -2.4, "early check derate missing");
    near(result.maximum, -1.4, "late check derate missing");
    result = values.checkValues(instance, check, variant, TransType::kFall);
    near(result.minimum, -2., "rise check factor leaked into fall");
    near(result.maximum, -2., "rise check factor leaked into fall");
  }
  check.get_timing_arc_list().push_back(variant);
  db.liberty().get_cell_map().at("FF").get_sdf_check_arc_list().push_back(check);
  require(exportedInstance("cdc/source_reg").find("(SETUP (posedge D) (posedge CK) (-2.4000000000::-1.4000000000))") != std::string::npos,
          "serialized check did not consume derates");

  check.set_data_port("CK");
  check.set_check_time(2.);
  variant.get_check_table_map().clear();
  check.set_check_type(TimingCheckType::kWidth);
  result = values.checkValues(instance, check, variant, TransType::kRise);
  near(result.minimum, 2., "cell-check factor leaked into pulse width");
  eval("set_timing_derate -min_pulse_width -rise 1.5 [get_lib_cells */FF]");
  eval("set_timing_derate -min_period -rise 1.25 [get_cells cdc/source_reg]");
  for (bool use_table : {false, true}) {
    if (use_table)
      variant.get_check_table_map()[TransType::kRise] = constantTable(2.);
    check.set_check_type(TimingCheckType::kWidth);
    result = values.checkValues(instance, check, variant, TransType::kRise);
    near(result.minimum, 3., "dedicated width factor missing");
    near(result.maximum, 3., "dedicated width factor missing");
    check.set_check_type(TimingCheckType::kPeriod);
    result = values.checkValues(instance, check, variant, TransType::kRise);
    near(result.minimum, 2.5, "dedicated period factor missing");
    near(result.maximum, 2.5, "dedicated period factor missing");
  }
  // Pair exported HOLD with exported SETUP after both have consumed derates.
  auto& pairs = db.liberty().get_cell_map().at("FF").get_sdf_check_arc_list();
  pairs.clear();
  TimingCheckArc setup;
  setup.set_data_port("D");
  setup.set_clock_port("CK");
  setup.set_clock_trans_type(TransType::kRise);
  setup.set_check_type(TimingCheckType::kSetup);
  setup.set_check_time(1.);
  pairs.push_back(setup);
  auto hold = setup;
  hold.set_check_type(TimingCheckType::kHold);
  hold.set_check_time(-.75);
  pairs.push_back(hold);
  TimingArc scalar;
  eval("set_timing_derate -early -cell_check -rise 0.5 [get_cells cdc/source_reg]");
  const auto exported_setup = values.checkValues(instance, pairs[0], scalar, TransType::kRise);
  const auto exported_hold = values.checkValues(instance, pairs[1], scalar, TransType::kRise);
  near(exported_setup.minimum, .5, "positive setup derate changed");
  near(exported_hold.minimum, -.5, "HOLD was paired with nominal rather than exported SETUP");
  require(exported_setup.minimum + exported_hold.minimum >= 0., "exported setup/hold pair has negative total aperture");

}
}  // namespace

int main(int argc, char** argv)
{
  Tcl_FindExecutable(argv[0]);
  Logger::initInst();
  DataManager::initInst();
  SdcCommand::initInst();
  sdc::registerSdcCommands(SdcCommand::getInst());
  buildDesign();
  require(argc == 2, "expected delays, fallback, or checks");
  const std::string mode = argv[1];
  if (mode == "delays")
    delayFactors();
  else if (mode == "fallback")
    fallbackFactors();
  else if (mode == "checks")
    checkFactors();
  else
    throw std::runtime_error("unknown derate export case");
}

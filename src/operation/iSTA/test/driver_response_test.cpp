// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <stdexcept>
#include <string>

#include "ArnoldiDelayCalculator.hpp"
#include "ArnoldiResponseCalculator.hpp"
#include "ArnoldiWaveformSolver.hpp"
#include "DmpResponseCalculator.hpp"
#include "InputPortResponseCalculator.hpp"
#include "PinCapacitanceCalculator.hpp"
#include "WireDelayCalculator.hpp"

namespace {
void require(bool ok, const char* message)
{
  if (!ok)
    throw std::runtime_error(message);
}
void testDisabledMultidriverFallback()
{
  using namespace ista;
  Database db;
  Config config;
  std::string source = "driver:Y", sink = "receiver:PAD", net_name = "pad_wire";
  for (const auto& name : {source, sink}) {
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_net_name(net_name);
    pin.set_instance_name(name.substr(0, name.find(':')));
    pin.set_pin_name(name.substr(name.find(':') + 1));
  }
  db.network().instances()["driver"].set_cell_name("core");
  db.network().instances()["receiver"].set_cell_name("io");
  db.liberty().get_cell_map()["io"].get_port_map()["PAD"].set_capacitance(1.45889);
  auto& net = db.network().nets()[net_name];
  net.set_net_name(net_name);
  net.set_driver_pin_list({source, sink});
  net.set_load_pin_list({sink});
  net.set_pin_name_list({source, sink});
  db.graph().simValues().setDriverDisabled(sink, true);
  auto& parasitic = db.parasitics().nets()[net_name];
  parasitic.set_net_name(net_name);
  parasitic.get_node_map()[source].set_capacitance(.005);
  parasitic.get_node_map()[sink].set_capacitance(.0061278);
  ParasiticResistor r;
  r.set_source_node(source);
  r.set_sink_node(sink);
  r.set_resistance(146.253);
  parasitic.get_resistor_list().push_back(r);
  PinCapacitanceCalculator caps(db);
  RcTreeAnalyzer rc(db, caps);
  RcModelBuilder models(db, config, rc);
  ReceiverThresholdCalculator thresholds(db);
  ArnoldiResponseCalculator arnoldi(db, config, rc, models, thresholds);
  DmpResponseCalculator dmp(db, rc, models, thresholds);
  InputPortResponseCalculator input(db, rc, models, thresholds);
  WireDelayCalculator wire(db, rc, dmp, arnoldi, input);
  TimingArc timing;
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    timing.get_delay_table_map()[edge].set_value_list({.2});
    timing.get_slew_table_map()[edge].set_value_list({1.});
  }
  Arc arc;
  arc.set_source_pin(source);
  arc.set_sink_pin(sink);
  arc.set_owner_name(net_name);
  arc.set_type(ArcType::kNet);
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      require(!arnoldi.response(source, timing, mode, edge, .1).get_is_valid(), "disabled alternate driver entered Arnoldi");
      require(!dmp.response(source, timing, mode, edge, .1, 1.4700178).get_is_valid(), "disabled alternate driver entered DMP");
      require(std::abs(wire.calcNetArcDelay(arc, mode, edge, 1.) - .2149945133034) < 1e-12,
              "balanced fallback did not use total wire R and receiver plus wire C");
      require(wire.calcNetArcSlew(arc, mode, edge, 1.) == 1., "balanced fallback manufactured RC slew degradation");
    }
  auto& core = db.liberty().get_cell_map()["core"];
  core.set_library_name("core_lib");
  core.set_nom_voltage(1.2);
  core.set_slew_derate_from_library(.5);
  auto& io = db.liberty().get_cell_map()["io"];
  io.set_library_name("io_lib");
  io.set_nom_voltage(3.3);
  io.set_slew_derate_from_library(1.);
  io.set_slew_lower_threshold_pct_rise(.1);
  io.set_slew_upper_threshold_pct_rise(.9);
  io.set_slew_lower_threshold_pct_fall(.1);
  io.set_slew_upper_threshold_pct_fall(.9);
  // PT2018 fixed-slew sweep. The two voltage domains are metadata, not pin names.
  const double base = .2149945133034;
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
    for (double slew : {.1, .5, 1., 2., 4.}) {
      require(std::abs(wire.calcNetArcDelay(arc, mode, TransType::kRise, slew) - (base + 2.1875 * slew)) < 1e-12,
              "PAD rise reference omitted rail or used degraded slew");
      require(std::abs(wire.calcNetArcDelay(arc, mode, TransType::kFall, slew) - (base - 2.1875 * slew)) < 1e-12,
              "PAD negative fall reference was clamped or mis-scaled");
      for (auto edge : {TransType::kRise, TransType::kFall})
        require(std::abs(wire.calcNetArcSlew(arc, mode, edge, slew) - 2.75 * slew) < 1e-12, "PAD slew window/voltage conversion incorrect");
    }
  db.graph().simValues().setDriverDisabled(sink, false);
  arnoldi.clear();
  dmp.clear();
  require(arnoldi.response(source, timing, AnalysisType::kMax, TransType::kRise, .1).get_is_valid(), "enabled alternate driver misclassified as disabled");
  db.graph().simValues().setDriverDisabled(sink, std::nullopt);
  require(!rc.hasDisabledDriver(net_name, source), "unknown alternate driver misclassified as disabled");
  db.graph().simValues().setDriverDisabled(sink, true);
  db.parasitics().nets().clear();
  require(std::abs(wire.calcNetArcDelay(arc, AnalysisType::kMax, TransType::kRise, 1.) - 2.1875) < 1e-12,
          "unextracted disabled-multidriver lost reference conversion");
  db.network().pins()[source].set_is_port(true);
  db.liberty().set_nom_voltage(1.2);
  db.liberty().set_slew_derate_from_library(.5);
  // PT2018/PT2022 both preserve the port waveform across an unextracted
  // port-to-PAD connection. A disabled PAD output must not turn the port
  // into a physical cell driver in the main library's voltage domain.
  for (bool driving_cell : {false, true}) {
    TimingDrivingCell driver;
    driver.set_cell_name("core");
    driver.set_to_pin("Y");
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
      for (auto edge : {TransType::kRise, TransType::kFall}) {
        if (driving_cell)
          db.sdc().get_port_constraint_map()[source].set_driving_cell(mode, edge, driver);
        require(wire.calcNetArcDelay(arc, mode, edge, 1.) == 0.
                    && wire.calcNetArcSlew(arc, mode, edge, 1.) == 1.,
                "unextracted port-to-PAD connection changed the port waveform");
      }
  }
}

void testDistributedVoltageDomains()
{
  using namespace ista;
  Database db;
  Config config;
  config.min_slew_degradation = 1;
  std::string source = "port", sink = "u:A", name = "n";
  db.network().pins()[source].set_is_port(true);
  db.network().pins()[source].set_net_name(name);
  db.network().pins()[sink].set_instance_name("u");
  db.network().pins()[sink].set_pin_name("A");
  db.network().instances()["u"].set_cell_name("core");
  auto& cell = db.liberty().get_cell_map()["core"];
  cell.set_library_name("core_lib");
  cell.set_nom_voltage(1.2);
  cell.set_slew_derate_from_library(.5);
  db.liberty().set_nom_voltage(1.2);  // A driving cell must override this main-library domain.
  auto& net = db.parasitics().nets()[name];
  net.set_net_name(name);
  net.get_node_map()[source].set_capacitance(.01);
  net.get_node_map()[sink].set_capacitance(.04);
  ParasiticResistor r;
  r.set_source_node(source);
  r.set_sink_node(sink);
  r.set_resistance(.001);
  net.get_resistor_list().push_back(r);
  TimingArc timing;
  timing.set_library_name("io_lib");
  timing.set_nom_voltage(3.3);
  timing.set_slew_derate(1.);
  timing.set_slew_lower_threshold_pct_rise(.1);
  timing.set_slew_upper_threshold_pct_rise(.9);
  timing.set_slew_lower_threshold_pct_fall(.1);
  timing.set_slew_upper_threshold_pct_fall(.9);
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    timing.get_delay_table_map()[edge].set_value_list({.2});
    timing.get_slew_table_map()[edge].set_value_list({.8});
  }
  PinCapacitanceCalculator caps(db);
  RcTreeAnalyzer rc(db, caps);
  RcModelBuilder models(db, config, rc);
  ReceiverThresholdCalculator thresholds(db);
  ArnoldiResponseCalculator arnoldi(db, config, rc, models, thresholds);
  DmpResponseCalculator dmp(db, rc, models, thresholds);
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      const auto response = arnoldi.response(source, timing, mode, edge, .1);
      require(response.get_is_valid(), "valid driving-cell waveform rejected");
      const double delay = response.get_wire_delay_map().at(sink);
      require(edge == TransType::kRise ? delay < -.2 : delay > .2,
              ("distributed receiver crossing lost driving-cell voltage domain: " + std::to_string(static_cast<int>(edge)) + " delay=" + std::to_string(delay)
               + " slew=" + std::to_string(response.get_load_slew_map().at(sink)))
                  .c_str());
      const auto& model = models.arnoldiModel(net, source, mode, edge);
      const auto fitted = ArnoldiDelayCalculator{}.calculate(timing, edge, .1, model, config.adjust_driver_resistance);
      require(fitted.has_value(), "receiver threshold reference has no driver");
      // Wire R is 1e-6 kOhm, so use the independent lumped-RC closed form.
      // A falling exponential does not scale slew linearly with voltage swing.
      const double tau = fitted->drive_resistance * .05, ramp = fitted->driver_ramp;
      auto crossing = [&](double threshold) {
        double low = 0., high = ramp + 40. * tau;
        for (int iteration = 0; iteration < 80; ++iteration) {
          const double time = .5 * (low + high);
          const double voltage = time < ramp ? 1. - (time + tau * std::expm1(-time / tau)) / ramp
                                            : tau / ramp * (-std::expm1(-ramp / tau)) * std::exp(-(time - ramp) / tau);
          if (voltage > threshold)
            low = time;
          else
            high = time;
        }
        return .5 * (low + high);
      };
      const double ratio = 1.2 / 3.3;
      const double expected_slew = edge == TransType::kRise ? (crossing(1. - .7 * ratio) - crossing(1. - .3 * ratio)) / .5
                                                          : (crossing(.3 * ratio) - crossing(.7 * ratio)) / .5;
      require(std::abs(response.get_load_slew_map().at(sink) - expected_slew) < 2e-6,
              "distributed receiver slew disagrees with physical voltage crossings");
      const auto dmp_result = dmp.response(source, timing, mode, edge, .1, .05);
      require(dmp_result.get_is_valid(), "valid DMP fixture rejected");
      const double dmp_delay = dmp_result.get_wire_delay_map().at(sink);
      require(edge == TransType::kRise ? dmp_delay < -.2 : dmp_delay > .2, "DMP receiver lost voltage domain");
    }
  // Ordinary 1.2V -> 3.3V input has no reachable 50% crossing, unlike skipped RC on a PAD.
  const double midpoint_delay = arnoldi.response(source, timing, AnalysisType::kMax, TransType::kRise, .1).get_wire_delay_map().at(sink);
  timing.set_output_threshold_pct_rise(.25);
  arnoldi.clear();
  require(arnoldi.response(source, timing, AnalysisType::kMax, TransType::kRise, .1).get_wire_delay_map().at(sink) > midpoint_delay + .1,
          "cross-domain wire delay ignored non-midpoint source reference");
  timing.set_output_threshold_pct_rise(.5);
  timing.set_nom_voltage(1.2);
  cell.set_nom_voltage(3.3);
  arnoldi.clear();
  const auto failed = arnoldi.response(source, timing, AnalysisType::kMax, TransType::kRise, .1);
  require(failed.get_is_valid() && !failed.get_wire_delay_map().contains(sink), "unreachable receiver published successful delay");
  dmp.clear();
  const auto dmp_failed = dmp.response(source, timing, AnalysisType::kMax, TransType::kRise, .1, .05);
  require(dmp_failed.get_is_valid() && dmp_failed.loadFailed(sink), "unreachable DMP receiver published success");
  InputPortResponseCalculator input(db, rc, models, thresholds);
  WireDelayCalculator wire(db, rc, dmp, arnoldi, input);
  Arc arc;
  arc.set_type(ArcType::kNet);
  arc.set_owner_name(name);
  arc.set_source_pin(source);
  arc.set_sink_pin(sink);
  wire.calcNetArcDelay(arc, AnalysisType::kMax, TransType::kRise, failed.get_driver_slew());
  require(arc.rcFailed(AnalysisType::kMax, TransType::kRise) && !arc.rcFailed(AnalysisType::kMin, TransType::kRise),
          "RC failure became a valid fallback or leaked across min/max");
}

void testShieldedLoadWithSlewLimitedDriver()
{
  using namespace ista;
  Database db;
  Config config;
  PinCapacitanceCalculator caps(db);
  RcTreeAnalyzer rc(db, caps);
  RcModelBuilder models(db, config, rc);
  std::string source = "driver", sink = "load";
  db.network().pins()[source].set_is_port(true);
  db.network().pins()[sink].set_is_port(false);
  db.network().pins()[sink].set_instance_name("receiver");
  db.network().pins()[sink].set_pin_name("A");
  db.network().instances()["receiver"].set_cell_name("receiver_cell");
  db.liberty().get_cell_map()["receiver_cell"].get_port_map()["A"].set_capacitance(0.);
  ParasiticNet net;
  net.set_net_name("shielded");
  net.get_node_map()[source].set_capacitance(.1);
  net.get_node_map()[sink].set_capacitance(.9);
  ParasiticResistor wire;
  wire.set_source_node(source);
  wire.set_sink_node(sink);
  wire.set_resistance(10000.);
  net.get_resistor_list().push_back(wire);
  TimingArc arc;
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    auto& delay = arc.get_delay_table_map()[edge];
    delay.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
    delay.set_axis_list({{0., 1.}});
    delay.set_value_list({.1, 4.1});
    arc.get_slew_table_map()[edge].set_value_list({.2});
    const auto& model = models.arnoldiModel(net, source, AnalysisType::kMax, edge);
    const auto result = ArnoldiDelayCalculator{}.calculate(arc, edge, .1, model);
    require(result.has_value(), "valid slew-limited driver failed");
    // Independently solve C*dV/dt + G*V = input/Rdrive for the two nodes.
    // Match the lumped 30--70% slew (.2 ns), then charge/driver voltage at
    // the end of the fitted ramp. Rdrive=.2/ln(7/3), ramp=.49984361145 ns.
    // This catches bypassing Ceff just because slew bounds Rdrive below dD/dC.
    require(std::abs(result->effective_capacitance - .12347214593492879) < 1e-7, "slew-limited driver ignored RC shielding or used unconverged charge");
    require(std::abs(result->delays[0] - .5936348392718512) < 1e-6 && std::abs(result->delays[1] - 6.9749183578722596) < 1e-6,
            "cell and wire delay do not share the converged driver waveform");
    require(std::abs(result->slews[0] - .2045474917959873) < 1e-6 && std::abs(result->slews[1] - 7.806143103340313) < 1e-6,
            "shielded RC slew does not match the nodal-admittance solution");
  }
  db.network().pins()[source].set_net_name("shielded");
  db.parasitics().nets()["shielded"] = net;
  ReceiverThresholdCalculator thresholds(db);
  ArnoldiResponseCalculator response(db, config, rc, models, thresholds);
  const auto minimum = response.response(source, arc, AnalysisType::kMin, TransType::kRise, .1);
  const auto maximum = response.response(source, arc, AnalysisType::kMax, TransType::kRise, .1);
  require(minimum.get_is_valid() && maximum.get_is_valid(), "shielded response missing");
  require(minimum.get_load_slew_map().at(sink) > minimum.get_driver_slew(), "minimum slew copied an unadjusted driver");
  require(maximum.get_load_slew_map().at(sink) > maximum.get_driver_slew()
              && std::abs(minimum.get_wire_delay_map().at(sink) - maximum.get_wire_delay_map().at(sink)) < 1e-12,
          "minimum slew policy changed physical net delay or maximum slew");
  config.min_slew_degradation = 1;
  response.clear();
  models.clear();
  const auto degraded = response.response(source, arc, AnalysisType::kMin, TransType::kRise, .1);
  require(degraded.get_is_valid() && degraded.get_load_slew_map().at(sink) == maximum.get_load_slew_map().at(sink),
          "explicit minimum slew degradation was ignored");
}
void testZeroCapacitanceDriver()
{
  using namespace ista;
  Database db;
  Config config;
  PinCapacitanceCalculator caps(db);
  RcTreeAnalyzer rc(db, caps);
  RcModelBuilder models(db, config, rc);
  std::string source = "source", load = "load";
  db.network().pins()[source].set_is_port(true);
  db.network().pins()[load].set_is_port(true);
  ParasiticNet net;
  net.set_net_name("wire");
  net.get_node_map()[source].set_capacitance(0.);
  net.get_node_map()[load].set_capacitance(.05);
  ParasiticResistor resistor;
  resistor.set_source_node(source);
  resistor.set_sink_node(load);
  resistor.set_resistance(100.);
  net.get_resistor_list().push_back(resistor);
  const auto& model = models.arnoldiModel(net, source, AnalysisType::kMax, TransType::kRise);
  const auto poles = ArnoldiWaveformSolver::poleResidue(model, .2);
  // Exact one-RC circuit: source resistance .2 kOhm, wire .1 kOhm,
  // load .05 pF. The zero-cap driver has an instantaneous feedthrough of 1/3.
  const double ramp = .02, tau = .015;
  auto crossing = [&](bool driver, double threshold) {
    double low = 0., high = 1.;
    for (int iteration = 0; iteration < 100; ++iteration) {
      const double time = (low + high) * .5;
      const double input = std::max(0., 1. - time / ramp);
      const double output
          = time <= ramp ? 1. - (time + tau * std::expm1(-time / tau)) / ramp : tau / ramp * (-std::expm1(-ramp / tau)) * std::exp(-(time - ramp) / tau);
      const double voltage = driver ? input / 3. + output * (2. / 3.) : output;
      if (voltage > threshold)
        low = time;
      else
        high = time;
    }
    return (low + high) * .5;
  };
  double driver_time = 0., load_time = 0.;
  ArnoldiWaveformSolver::crossingTime(ramp, poles, 0, .5, driver_time);
  ArnoldiWaveformSolver::crossingTime(ramp, poles, 1, .5, load_time);
  require(std::abs(driver_time - crossing(true, .5)) < 1e-7 && std::abs(load_time - crossing(false, .5)) < 1e-7,
          "zero-capacitance driver lost algebraic feedthrough and collapsed the wire delay");
  for (double threshold : {.1, .2, .8, .9}) {
    ArnoldiWaveformSolver::crossingTime(ramp, poles, 1, threshold, load_time);
    require(std::abs(load_time - crossing(false, threshold)) < 1e-7, "non-midpoint crossing disagrees with exact one-RC response");
  }
}

void testStrongShieldingCharge()
{
  using namespace ista;
  Database db;
  Config config;
  PinCapacitanceCalculator caps(db);
  RcTreeAnalyzer rc(db, caps);
  RcModelBuilder models(db, config, rc);
  std::string source = "source", sink = "sink";
  db.network().pins()[source].set_is_port(true);
  db.network().pins()[sink].set_is_port(true);
  ParasiticNet net;
  net.set_net_name("slow_branch");
  net.get_node_map()[source].set_capacitance(.001);
  net.get_node_map()[sink].set_capacitance(.999);
  ParasiticResistor wire;
  wire.set_source_node(source);
  wire.set_sink_node(sink);
  wire.set_resistance(100000.);
  net.get_resistor_list().push_back(wire);
  TimingArc arc;
  auto& delay = arc.get_delay_table_map()[TransType::kRise];
  delay.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
  delay.set_axis_list({{0., 1.}});
  delay.set_value_list({.1, 4.1});
  auto& slew = arc.get_slew_table_map()[TransType::kRise];
  slew.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
  slew.set_axis_list({{0., 1.}});
  slew.set_value_list({0., .001});
  const auto& model = models.arnoldiModel(net, source, AnalysisType::kMax, TransType::kRise);
  const auto result = ArnoldiDelayCalculator{}.calculate(arc, TransType::kRise, .1, model);
  require(result.has_value(), "charge cancellation caused a valid shielded network to fall back");
  // The far-node time constant is 99.9 ns; during the ~1e-6 ns driver
  // ramp only the .001 pF near capacitor charges appreciably.
  require(std::abs(result->effective_capacitance - .001) < 1e-7 && std::abs(result->delays[0] - .104) < 1e-5,
          "shielded remote load was treated as a lumped capacitor");
  const double slow_time_constant = (100. + .001 / std::log(7. / 3.)) * .999;
  require(std::abs((result->delays[1] - result->delays[0]) - slow_time_constant * std::log(2.)) < 1e-6,
          "charge convergence failure replaced the physical crossing with an Elmore delay");

  // A monotone nonlinear slew table can make fixed-point charge iteration
  // oscillate even though the waveform/charge equation has a valid root.
  net.get_node_map()[source].set_capacitance(.01);
  net.get_node_map()[sink].set_capacitance(.99);
  net.get_resistor_list().front().set_resistance(10.);
  slew.set_axis_list({{0., .01, .03, .1, .3, 1.}});
  slew.set_value_list({.001, .001001, .001009, .0011, .0019, .011});
  models.clear();
  rc.clear();
  const auto& nonlinear_model = models.arnoldiModel(net, source, AnalysisType::kMax, TransType::kRise);
  const auto nonlinear = ArnoldiDelayCalculator{}.calculate(arc, TransType::kRise, .1, nonlinear_model);
  require(nonlinear.has_value(), "oscillating Ceff iteration discarded a valid charge root");
  require(std::abs(nonlinear->effective_capacitance - .09068340400164446) < 1e-7, "nonlinear slew table did not converge to the charge-equivalent load");
}

void testZeroCapacitancePortTap()
{
  using namespace ista;
  // Ideal ramp source -- 100 ohm -- algebraic tap -- 100 ohm -- 0.05 pF.
  // The tap voltage is (source + load)/2; its direct feedthrough is 1/2.
  ParasiticArnoldiModel model;
  const double root_cap = std::sqrt(.05), tau = .01, ramp = .02;
  model.set_is_valid(true);
  model.set_order(1);
  model.set_total_capacitance(.05);
  model.set_sqrt_total_capacitance(root_cap);
  model.set_diagonal_list({tau});
  model.set_off_diagonal_list({});
  model.set_projection_list({{1. / root_cap, 1. / root_cap, 1. / root_cap}});
  model.set_term_node_list({"source", "tap", "load"});
  model.setZeroCapacitanceTerms({true, true, false});
  model.setResponseProjection({{0., .005 / root_cap, .01 / root_cap}});
  auto crossing = [&](double threshold, double direct) {
    double low = 0., high = .2;
    for (int i = 0; i < 80; ++i) {
      const double time = (low + high) / 2.;
      const double input = std::min(time / ramp, 1.);
      const double output = time < ramp ? time / ramp - tau / ramp * (1. - std::exp(-time / tau))
                                        : 1. - tau / ramp * (1. - std::exp(-ramp / tau)) * std::exp(-(time - ramp) / tau);
      if (direct * input + (1. - direct) * output < threshold)
        low = time;
      else
        high = time;
    }
    return (low + high) / 2.;
  };
  WaveformThresholds thresholds{.5, .1, .9, 1.};
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    const auto response = ArnoldiWaveformSolver::inputPortResponse(model, edge, .016, thresholds, {thresholds, thresholds, thresholds});
    require(response.has_value(), "ideal-source algebraic tap response missing");
    for (int term : {1, 2}) {
      const double direct = term == 1 ? .5 : 0.;
      require(std::abs(response->delays[term] - (crossing(.5, direct) - ramp / 2.)) < 1e-10, "ideal-source algebraic tap delay collapsed to the load");
      require(std::abs(response->slews[term] - (crossing(.9, direct) - crossing(.1, direct))) < 1e-10,
              "ideal-source algebraic tap lost instantaneous feedthrough");
    }
  }
}

void testPortReceiverThresholds()
{
  using namespace ista;
  Database db;
  Config config;
  PinCapacitanceCalculator caps(db);
  RcTreeAnalyzer rc(db, caps);
  RcModelBuilder models(db, config, rc);
  ReceiverThresholdCalculator threshold(db);
  InputPortResponseCalculator port(db, rc, models, threshold);
  std::string source = "in", node = "hier/load/A", pin = "hier/load:A";
  db.network().pins()[source].set_is_port(true);
  auto& load = db.network().pins()[pin];
  load.set_is_port(false);
  load.set_instance_name("hier/load");
  load.set_pin_name("A");
  db.network().instances()["hier/load"].set_cell_name("receiver");
  auto& cell = db.liberty().get_cell_map()["receiver"];
  cell.set_input_threshold_pct_rise(25.0);
  cell.set_input_threshold_pct_fall(25.0);
  cell.set_slew_lower_threshold_pct_rise(20.0);
  cell.set_slew_lower_threshold_pct_fall(20.0);
  cell.set_slew_upper_threshold_pct_rise(80.0);
  cell.set_slew_upper_threshold_pct_fall(80.0);
  cell.set_slew_derate_from_library(2.0);
  ParasiticNet net;
  net.set_net_name("wire");
  net.get_node_map()[source].set_capacitance(0.0);
  net.get_node_map()[node].set_capacitance(0.05);
  ParasiticResistor resistor;
  resistor.set_source_node(source);
  resistor.set_sink_node(node);
  resistor.set_resistance(100.0);
  net.get_resistor_list().push_back(resistor);
  for (auto transition : {TransType::kRise, TransType::kFall}) {
    const double expected_delay = transition == TransType::kRise ? 0.0014384103622589045 : 0.006931471805599453;
    for (int repeat = 0; repeat < 2; ++repeat) {
      const auto delay = port.delay(net, source, node, AnalysisType::kMax, transition, 0.0);
      const auto slew = port.slew(net, source, pin, AnalysisType::kMax, transition, 0.0);
      require(delay && std::abs(*delay - expected_delay) < 1e-10, "port response lost receiver threshold or node identity");
      require(slew && std::abs(*slew - 0.0034657359027997266) < 1e-10, "port response lost pin alias or receiver derate");
      require(port.delay(net, source, pin, AnalysisType::kMax, transition, 0.0) == delay, "node and pin aliases disagree");
      const auto positive = port.slew(net, source, pin, AnalysisType::kMax, transition, 0.01);
      const auto negative = port.slew(net, source, pin, AnalysisType::kMax, transition, -0.01);
      require(positive && negative && *positive == -*negative, "cached response lost signed slew convention");
      require(port.delay(net, source, pin, AnalysisType::kMax, transition, 0.01) == port.delay(net, source, pin, AnalysisType::kMax, transition, -0.01),
              "slew sign changed cached delay");
      port.clear();
    }
  }
  cell.set_slew_derate_from_library(0.0);
  require(!port.delay(net, source, node, AnalysisType::kMax, TransType::kRise, 0.0), "invalid receiver published partial response");
  cell.set_slew_derate_from_library(2.0);
  port.clear();
  require(port.delay(net, source, node, AnalysisType::kMax, TransType::kRise, 0.0).has_value(), "invalid response survived clear");
  db.liberty().set_nom_voltage(3.3);
  db.liberty().set_slew_lower_threshold_pct_rise(.1);
  db.liberty().set_slew_upper_threshold_pct_rise(.9);
  db.liberty().set_slew_lower_threshold_pct_fall(.1);
  db.liberty().set_slew_upper_threshold_pct_fall(.9);
  db.liberty().set_slew_derate_from_library(1.);
  cell.set_nom_voltage(1.2);
  cell.set_input_threshold_pct_rise(.5);
  cell.set_input_threshold_pct_fall(.5);
  cell.set_slew_lower_threshold_pct_rise(.3);
  cell.set_slew_upper_threshold_pct_rise(.7);
  cell.set_slew_lower_threshold_pct_fall(.3);
  cell.set_slew_upper_threshold_pct_fall(.7);
  cell.set_slew_derate_from_library(.5);
  port.clear();
  // 1 ns linear ramp through tau=.005 ns; all crossings lie well past the transient.
  const auto rise = port.delay(net, source, node, AnalysisType::kMax, TransType::kRise, .8);
  const auto fall = port.delay(net, source, node, AnalysisType::kMax, TransType::kFall, .8);
  require(rise && fall && std::abs(*rise + .313181818181818) < 1e-8 && std::abs(*fall - .323181818181818) < 1e-8, "ideal input waveform ignored receiver rail");
  require(std::abs(*port.slew(net, source, node, AnalysisType::kMax, TransType::kRise, .8) - .290909090909091) < 1e-8,
          "ideal input waveform used a scalar threshold ratio instead of crossings");
  db.liberty().set_nom_voltage(1.2);
  cell.set_nom_voltage(3.3);
  port.clear();
  require(
      !port.delay(net, source, node, AnalysisType::kMax, TransType::kRise, .8) && port.loadFailed(net, source, pin, AnalysisType::kMax, TransType::kRise, .8),
      "unreachable port receiver was treated as a cache miss");
}
}  // namespace

int main()
{
  using namespace ista;
  testDisabledMultidriverFallback();
  testZeroCapacitanceDriver();
  testDistributedVoltageDomains();
  testShieldedLoadWithSlewLimitedDriver();
  testStrongShieldingCharge();
  testZeroCapacitancePortTap();
  testPortReceiverThresholds();
  Database db;
  Config config;
  PinCapacitanceCalculator caps(db);
  RcTreeAnalyzer rc(db, caps);
  RcModelBuilder models(db, config, rc);
  ReceiverThresholdCalculator threshold(db);
  DmpResponseCalculator dmp(db, rc, models, threshold);
  ArnoldiResponseCalculator arnoldi(db, config, rc, models, threshold);
  InputPortResponseCalculator port(db, rc, models, threshold);
  Arc arc;
  arc.set_source_pin("missing");
  arc.set_sink_pin("receiver");
  TimingArc timing;
  std::string driver = "missing", sink = "receiver";
  ParasiticNet net;
  for (int iteration = 0; iteration < 2; ++iteration) {
    if (dmp.response(driver, timing, AnalysisType::kMax, TransType::kRise, 0.1, 0.2).get_is_valid()
        || arnoldi.response(driver, timing, AnalysisType::kMax, TransType::kRise, 0.1).get_is_valid()
        || dmp.wireDelay(arc, AnalysisType::kMax, TransType::kRise, 0.1) || arnoldi.loadSlew(arc, AnalysisType::kMax, TransType::kRise, 0.1)
        || port.delay(net, driver, sink, AnalysisType::kMax, TransType::kRise, 0.1) || port.slew(net, driver, sink, AnalysisType::kMax, TransType::kRise, 0.1))
      throw std::runtime_error("response fabricated for a missing driver");
    dmp.clear();
    arnoldi.clear();
    port.clear();
  }
  db.network().pins()[driver].set_is_port(false);
  if (port.delay(net, driver, sink, AnalysisType::kMax, TransType::kRise, 0.1))
    throw std::runtime_error("instance pin was treated as an input waveform source");
}

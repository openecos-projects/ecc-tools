// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>

#include "ArnoldiDelayCalculator.hpp"
#include "ArnoldiResponseCalculator.hpp"
#include "ArnoldiWaveformSolver.hpp"
#include "DmpResponseCalculator.hpp"
#include "InputPortResponseCalculator.hpp"
#include "LoadCalculator.hpp"
#include "PinCapacitanceCalculator.hpp"
#include "WireDelayCalculator.hpp"

namespace {
void require(bool ok, const char* message)
{
  if (!ok)
    throw std::runtime_error(message);
}

double waveformMoment(const ista::ArnoldiDelayResult& result, std::size_t terminal = 0)
{
  double moment = 0.;
  for (std::size_t i = 0; i < result.waveform.get_pole_list().size(); ++i)
    moment += result.waveform.get_residue_list()[terminal][i] / result.waveform.get_pole_list()[i];
  return moment;
}
// Integrate the delivered current over the source ramp in long double.
// Q = integral((u-v)/R) must equal Ceff times the source voltage change.
void checkChargeBalance(const ista::ArnoldiDelayResult& result)
{
  const auto integral = [](long double x) {
    return x > 1e-4L ? x + std::expm1(-x) : x * x * (.5L + x * (-1.L / 6.L + x * (1.L / 24.L - x / 120.L)));
  };
  const long double ramp = result.driver_ramp, resistance = result.drive_resistance;
  long double charge = 0., voltage = 0.;
  for (std::size_t i = 0; i < result.waveform.get_pole_list().size(); ++i) {
    const long double pole = result.waveform.get_pole_list()[i];
    if (std::isinf(pole)) {
      voltage += result.waveform.get_residue_list()[0][i];
      continue;
    }
    voltage += result.waveform.get_residue_list()[0][i] * integral(pole * ramp) / (pole * ramp);
    charge += result.waveform.get_residue_list()[0][i] * integral(pole * ramp) / (pole * pole * ramp * resistance);
  }
  const long double lumped_charge = result.effective_capacitance * voltage;
  require(std::abs(charge - lumped_charge) < 1e-7L * lumped_charge, "driver fit violated charge equivalence");
}

void testDistinctRcBranches()
{
  using namespace ista;
  Database db;
  Config config;
  std::string source = "driver:Y", name = "small";
  auto& net = db.network().nets()[name];
  net.set_net_name(name);
  net.set_driver_pin_list({source});
  net.set_load_pin_list({"a:A", "b:A", "c:A"});
  net.set_pin_name_list({source, "a:A", "b:A", "c:A"});
  auto& parasitic = db.parasitics().nets()[name];
  parasitic.set_net_name(name);
  int index = 0;
  for (const auto& pin_name : net.get_pin_name_list()) {
    auto& pin = db.network().pins()[pin_name];
    pin.set_full_name(pin_name);
    pin.set_net_name(name);
    pin.set_instance_name(pin_name.substr(0, pin_name.find(':')));
    pin.set_pin_name(pin_name.substr(pin_name.find(':') + 1));
    db.network().instances()[pin.get_instance_name()].set_cell_name("cell");
    parasitic.get_node_map()[pin_name].set_capacitance(index < 2 ? .01 : .02);
    if (index > 0) {
      ParasiticResistor resistor;
      resistor.set_source_node(source);
      resistor.set_sink_node(pin_name);
      resistor.set_resistance(index == 1 ? 10. : index == 2 ? 20. : 60.);
      parasitic.get_resistor_list().push_back(resistor);
    }
    ++index;
  }
  auto& cell = db.liberty().get_cell_map()["cell"];
  cell.set_library_name("test");
  cell.set_slew_derate_from_library(.5);
  cell.get_port_map()["A"].set_capacitance(.005);
  TimingArc timing;
  timing.set_library_name("test");
  timing.set_slew_derate(.5);
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    auto& delay = timing.get_delay_table_map()[edge];
    delay.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
    delay.set_axis_list({{0., 1.}});
    delay.set_value_list({.2, 3.2});
    auto& slew = timing.get_slew_table_map()[edge];
    slew.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
    slew.set_axis_list({{0., 1.}});
    slew.set_value_list({.1, 4.1});
  }
  PinCapacitanceCalculator caps(db);
  RcTreeAnalyzer rc(db, caps);
  RcModelBuilder models(db, config, rc);
  ReceiverThresholdCalculator thresholds(db);
  ArnoldiResponseCalculator calculator(db, config, rc, models, thresholds);
  // Distinct RC branches must retain distinct waveforms at every scale.
  // A topology-independent lumped delay would make all three receivers equal.
  for (double scale : {.05, .2, 1., 1.2}) {
    parasitic.get_resistor_list()[0].set_resistance(10. * scale);
    parasitic.get_resistor_list()[1].set_resistance(20. * scale);
    parasitic.get_resistor_list()[2].set_resistance(60. * scale);
    calculator.clear();
    models.clear();
    rc.clear();
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
      for (auto edge : {TransType::kRise, TransType::kFall}) {
        const auto& result = calculator.response(source, timing, mode, edge, .1);
        require(result.get_is_valid() && std::isfinite(result.get_gate_delay()) && result.get_driver_slew() > 0.,
                "passive RC network did not produce a finite driver response");
        require(result.get_effective_capacitance() > 0. && result.get_effective_capacitance() <= .075 + 1e-12,
                "effective capacitance is outside the physical load bounds");
        if (scale == .05) {
          // 0.06 pF parasitic + three 0.005 pF pin loads: this electrically
          // short network must use the LUT at its complete 0.075 pF load.
          require(std::abs(result.get_gate_delay() - .425) < 1e-12 && std::abs(result.get_driver_slew() - .4) < 1e-12
                      && std::abs(result.get_effective_capacitance() - .075) < 1e-12,
                  "short RC network did not use the total-capacitance cell tables");
          // Each star branch has only its own 0.015/0.025/0.025 pF load.
          // During this slow linear ramp, propagation equals Rbranch*Cbranch.
          require(std::abs(result.get_wire_delay_map().at("a:A") - .0000075) < 1e-12
                      && std::abs(result.get_wire_delay_map().at("b:A") - .000025) < 1e-12
                      && std::abs(result.get_wire_delay_map().at("c:A") - .000075) < 1e-12,
                  "short RC response did not preserve the independent branch moments");
        }
        const auto& delays = result.get_wire_delay_map();
        require(delays.at("a:A") < delays.at("b:A") && delays.at("b:A") < delays.at("c:A"),
                "unequal RC branches collapsed to the same receiver delay");
        for (const auto& sink : net.get_load_pin_list())
          require(std::isfinite(result.get_load_slew_map().at(sink)) && result.get_load_slew_map().at(sink) > 0.,
                  "passive RC receiver lost its finite transition");
      }
  }

  const auto evaluate = [&](double limit_ps, double rc_bound_ps) {
    config.small_net_limit_ps = limit_ps;
    const double scale = rc_bound_ps / 6.75;
    parasitic.get_resistor_list()[0].set_resistance(10. * scale);
    parasitic.get_resistor_list()[1].set_resistance(20. * scale);
    parasitic.get_resistor_list()[2].set_resistance(60. * scale);
    calculator.clear();
    models.clear();
    rc.clear();
    return calculator.response(source, timing, AnalysisType::kMax, TransType::kRise, .1);
  };
  const auto is_table_load = [](const DriverResponse& result) {
    return result.get_is_valid() && std::abs(result.get_gate_delay() - .425) < 1e-12
           && std::abs(result.get_driver_slew() - .4) < 1e-12 && std::abs(result.get_effective_capacitance() - .075) < 1e-12;
  };
  const auto same_response = [](const DriverResponse& left, const DriverResponse& right) {
    return left.get_is_valid() == right.get_is_valid() && left.get_gate_delay() == right.get_gate_delay()
           && left.get_driver_slew() == right.get_driver_slew() && left.get_effective_capacitance() == right.get_effective_capacitance()
           && left.get_wire_delay_map() == right.get_wire_delay_map() && left.get_load_slew_map() == right.get_load_slew_map();
  };
  require(is_table_load(evaluate(2., 1.999)), "ps-valued limit did not include a network below the boundary");
  require(same_response(evaluate(2., 2.001), evaluate(0., 2.001)), "network above the boundary did not use the distributed solver");
  const auto distributed = evaluate(0., 3.);
  require(same_response(evaluate(2., 3.), distributed) && is_table_load(evaluate(5., 3.)),
          "2 ps and 5 ps limits did not select their intended RC ranges");
  require(same_response(evaluate(5., 5.001), evaluate(0., 5.001)), "5 ps range leaked beyond its boundary");

  cell.set_library_name("receiver");
  cell.set_slew_derate_from_library(.25);
  const auto rescaled = evaluate(2., .1);
  require(is_table_load(rescaled) && std::abs(rescaled.get_load_slew_map().at("a:A") - .8) < 1e-12,
          "capacitive response did not convert the receiver's slew derate");
  cell.set_slew_derate_from_library(.5);
  cell.set_input_threshold_pct_rise(.4);
  require(same_response(evaluate(2., .1), evaluate(0., .1)), "incompatible receiver threshold bypassed the distributed solver");
  cell.set_input_threshold_pct_rise(.5);
  timing.set_nom_voltage(1.);
  cell.set_nom_voltage(1.2);
  require(same_response(evaluate(2., .1), evaluate(0., .1)), "mixed voltage domains entered the capacitive shortcut");
  cell.set_nom_voltage(1.);
  timing.get_delay_table_map()[TransType::kRise].set_value_list({-.3, 2.7});
  require(std::abs(evaluate(2., .1).get_gate_delay() + .075) < 1e-12, "valid negative cell delay was rejected or clamped");
  timing.get_delay_table_map()[TransType::kRise].set_value_list({.2, 3.2});
  parasitic.get_node_map()["isolated"].set_capacitance(.1);
  require(same_response(evaluate(2., .1), evaluate(0., .1)), "isolated internal capacitance entered the small network approximation");
  parasitic.get_node_map().erase("isolated");
  timing.get_slew_table_map()[TransType::kRise].set_value_list({0., 0.});
  const auto step = evaluate(2., .3375);
  require(step.get_is_valid() && step.get_driver_slew() == 0.
              && std::abs(step.get_wire_delay_map().at("a:A") - .00000519860385419959) < 1e-15,
          "zero-duration source did not retain the RC step-response delay");
  timing.get_slew_table_map()[TransType::kRise].set_value_list({});
  require(same_response(evaluate(2., .1), evaluate(0., .1)), "empty slew table was interpreted as a valid ideal step");
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
  // Linear voltage conversion uses the library rails and transition windows.
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
  // An unextracted port-to-PAD connection preserves the port waveform.
  // A disabled PAD output must not turn the port
  // into a physical cell driver in the main library's voltage domain.
  for (bool driving_cell : {false, true}) {
    TimingDrivingCell driver;
    driver.set_cell_name("core");
    driver.set_to_pin("Y");
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
      for (auto edge : {TransType::kRise, TransType::kFall}) {
        if (driving_cell)
          db.sdc().get_port_constraint_map()[source].set_driving_cell(mode, edge, driver);
        require(wire.calcNetArcDelay(arc, mode, edge, 1.) == 0. && wire.calcNetArcSlew(arc, mode, edge, 1.) == 1.,
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
  for (int domain : {0, 1, 2}) {
    timing.set_nom_voltage(domain == 0 ? 3.3 : 1.2);
    timing.set_library_name(domain == 2 ? "core_lib" : "io_lib");
    arnoldi.clear();
    dmp.clear();
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      const auto response = arnoldi.response(source, timing, mode, edge, .1);
      require(response.get_is_valid(), "valid driving-cell waveform rejected");
      const double delay = response.get_wire_delay_map().at(sink);
      const auto& model = models.arnoldiModel(net, source, mode, edge);
      const auto fitted = ArnoldiDelayCalculator{}.calculate(timing, edge, .1, model);
      require(fitted.has_value(), "receiver threshold reference has no driver");
      // Wire R is 1e-6 kOhm, so use the independent lumped-RC closed form.
      // A falling exponential does not scale slew linearly with voltage swing.
      const double tau = fitted->drive_resistance * .05, ramp = fitted->driver_ramp;
      auto crossing = [&](double threshold) {
        double low = 0., high = ramp + 40. * tau;
        for (int iteration = 0; iteration < 80; ++iteration) {
          const double time = .5 * (low + high);
          const double voltage
              = time < ramp ? 1. - (time + tau * std::expm1(-time / tau)) / ramp : tau / ramp * (-std::expm1(-ramp / tau)) * std::exp(-(time - ramp) / tau);
          if (voltage > threshold)
            low = time;
          else
            high = time;
        }
        return .5 * (low + high);
      };
      const double ratio = 1.2 / timing.get_nom_voltage();
      const double expected_delay = crossing(edge == TransType::kRise ? 1. - .5 * ratio : .5 * ratio) - crossing(.5);
      require(std::abs(delay - expected_delay) < 2e-6, "distributed receiver crossing lost the driving-cell voltage domain");
      const double expected_slew
          = edge == TransType::kRise ? (crossing(1. - .7 * ratio) - crossing(1. - .3 * ratio)) / .5 : (crossing(.3 * ratio) - crossing(.7 * ratio)) / .5;
      require(std::abs(response.get_load_slew_map().at(sink) - expected_slew) < 2e-6, "distributed receiver slew disagrees with physical voltage crossings");
      const auto dmp_result = dmp.response(source, timing, mode, edge, .1, .05);
      require(dmp_result.get_is_valid(), "valid DMP fixture rejected");
      const double dmp_delay = dmp_result.get_wire_delay_map().at(sink);
      if (domain == 0)
        require(edge == TransType::kRise ? dmp_delay < -.2 : dmp_delay > .2, "DMP receiver lost voltage domain");
    }
  }
  timing.set_nom_voltage(3.3);
  timing.set_library_name("io_lib");
  arnoldi.clear();
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

  // An unreachable threshold invalidates the shared receiver response for
  // this driver/edge. A control with all receivers reachable must recover.
  std::string peer = "v:A";
  db.network().pins()[peer].set_instance_name("v");
  db.network().pins()[peer].set_pin_name("A");
  db.network().instances()["v"].set_cell_name("peer");
  db.liberty().get_cell_map()["peer"].set_nom_voltage(1.2);
  net.get_node_map()[peer].set_capacitance(.02);
  r.set_sink_node(peer);
  r.set_resistance(100.);
  net.get_resistor_list().push_back(r);
  models.clear();
  rc.clear();
  arnoldi.clear();
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      const auto mixed = arnoldi.response(source, timing, mode, edge, .1);
      require(mixed.get_is_valid() && mixed.loadFailed(sink) && mixed.loadFailed(peer) && !mixed.get_wire_delay_map().contains(peer)
                  && !mixed.loadFailed(source) && mixed.get_wire_delay_map().contains(source),
              "failed shared receiver solve published a sibling's RC delay");
      Arc peer_arc;
      peer_arc.set_type(ArcType::kNet);
      peer_arc.set_owner_name(name);
      peer_arc.set_source_pin(source);
      peer_arc.set_sink_pin(peer);
      require(wire.calcNetArcDelay(peer_arc, mode, edge, mixed.get_driver_slew()) == 0.
                  && wire.calcNetArcSlew(peer_arc, mode, edge, mixed.get_driver_slew()) == mixed.get_driver_slew() && peer_arc.rcFailed(mode, edge),
              "shared RC failure lost zero-delay/source-slew propagation or its diagnostic");
    }
  // Only the fall window is unreachable; the rise solve and source response
  // must stay valid even after the fall result has entered the cache.
  cell.set_input_threshold_pct_rise(.2);
  cell.set_slew_lower_threshold_pct_rise(.1);
  cell.set_slew_upper_threshold_pct_rise(.3);
  arnoldi.clear();
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
    const auto fall = arnoldi.response(source, timing, mode, TransType::kFall, .1);
    const auto rise = arnoldi.response(source, timing, mode, TransType::kRise, .1);
    require(fall.loadFailed(peer) && !rise.loadFailed(peer) && !rise.loadFailed(sink) && rise.get_wire_delay_map().at(peer) > 0.,
            "unreachable fall receiver contaminated the rise response");
  }
  // Invalid library metadata is a separate failure from unreachable voltage.
  // The remote mixed-domain probe does not justify invalidating its siblings.
  cell.set_slew_derate_from_library(0.);
  arnoldi.clear();
  const auto invalid = arnoldi.response(source, timing, AnalysisType::kMax, TransType::kRise, .1);
  require(invalid.loadFailed(sink) && !invalid.loadFailed(peer) && invalid.get_wire_delay_map().at(peer) > 0.,
          "invalid receiver metadata was widened into a network-wide failure");
  cell.set_slew_derate_from_library(.5);
  cell.set_input_threshold_pct_rise(.5);
  cell.set_slew_lower_threshold_pct_rise(.3);
  cell.set_slew_upper_threshold_pct_rise(.7);
  cell.set_nom_voltage(1.2);
  arnoldi.clear();
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      const auto control = arnoldi.response(source, timing, mode, edge, .1);
      require(control.get_is_valid() && !control.loadFailed(peer) && control.get_wire_delay_map().at(peer) > 0.,
              "reachable control retained a stale network failure");
    }
}

void testDistributedDriverLibraryConsistency()
{
  using namespace ista;
  Database db;
  Config config;
  PinCapacitanceCalculator caps(db);
  RcTreeAnalyzer rc(db, caps);
  RcModelBuilder models(db, config, rc);
  std::string source = "driver", sink = "load";
  db.network().pins()[source].set_is_port(true);
  db.network().pins()[sink].set_instance_name("receiver");
  db.network().pins()[sink].set_pin_name("A");
  db.network().instances()["receiver"].set_cell_name("receiver_cell");
  db.liberty().get_cell_map()["receiver_cell"].get_port_map()["A"].set_capacitance(0.);
  ParasiticNet net;
  net.set_net_name("delay_equivalent");
  net.get_node_map()[source].set_capacitance(.1);
  net.get_node_map()[sink].set_capacitance(.9);
  ParasiticResistor wire;
  wire.set_source_node(source);
  wire.set_sink_node(sink);
  wire.set_resistance(10000.);
  net.get_resistor_list().push_back(wire);
  TimingArc arc;
  for (const auto edge : {TransType::kRise, TransType::kFall}) {
    auto& delay = arc.get_delay_table_map()[edge];
    delay.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
    delay.set_axis_list({{0., 1.}});
    delay.set_value_list({.2, .6});
    arc.get_slew_table_map()[edge].set_value_list({1.});
    const auto& model = models.arnoldiModel(net, source, AnalysisType::kMax, edge);
    const auto result = ArnoldiDelayCalculator{}.calculate(arc, edge, .1, model);
    require(result.has_value(), "identifiable distributed driver failed");
    // The library defines the cell delay at its effective load. The network
    // response must meet this same source crossing before adding wire delay.
    const double expected_delay = .2 + .4 * result->effective_capacitance;
    require(std::abs(result->delays.at(0) - expected_delay) < 1e-7,
            "distributed driver source delay disagrees with the library at its effective capacitance");
  }
}

void testLoadSensitivityKnot()
{
  using namespace ista;
  Database db;
  Config config;
  PinCapacitanceCalculator caps(db);
  RcTreeAnalyzer rc(db, caps);
  RcModelBuilder models(db, config, rc);
  std::string source = "driver", sink = "load";
  db.network().pins()[source].set_is_port(true);
  db.network().pins()[sink].set_is_port(true);
  ParasiticNet net;
  net.set_net_name("sensitivity_knot");
  net.get_node_map()[source].set_capacitance(.2);
  net.get_node_map()[sink].set_capacitance(.8);
  ParasiticResistor wire;
  wire.set_source_node(source);
  wire.set_sink_node(sink);
  wire.set_resistance(2000.);
  net.get_resistor_list().push_back(wire);
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    TimingArc arc;
    auto& delay = arc.get_delay_table_map()[edge];
    delay.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
    arc.get_slew_table_map()[edge].set_value_list({1.});
    const auto& model = models.arnoldiModel(net, source, AnalysisType::kMax, edge);
    for (bool identified_above_knot : {false, true}) {
      // Both tables are continuous. Their derivatives jump: neither a model
      // switch nor a sensitivity jump is a zero of the effective-load residual.
      delay.set_axis_list({identified_above_knot ? std::vector<double>{.1, .61, 1.} : std::vector<double>{.1, .5, 1.}});
      delay.set_value_list(identified_above_knot ? std::vector<double>{.21, .261, .456} : std::vector<double>{.21, .25, 1.25});
      const auto result = ArnoldiDelayCalculator{}.calculate(arc, edge, .1, model);
      require(result.has_value(), "continuous table lost its RC response at a load-sensitivity knot");
      require(result->effective_capacitance > .2 && result->effective_capacitance < 1., "knot fallback violated passive load bounds");
      checkChargeBalance(*result);
    }
  }
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
    require(result->effective_capacitance > .1 && result->effective_capacitance < 1., "shielded capacitance left the physical interval");
    require(std::abs(result->drive_resistance * model.get_total_capacitance() * std::log(7. / 3.) - .2) < 1e-8,
            "charge fallback changed the resistance characterized at total load");
    checkChargeBalance(*result);
    // The two-node nodal-admittance matrix gives these two positive poles.
    // Integrate their exponentials under a linear ramp independently of the
    // reduced model, then compare receiver crossings and physical slews.
    const long double resistance = result->drive_resistance, ramp = result->driver_ramp;
    const long double sum = (1.L / resistance + 1.L / 10.L) / .1L + 1.L / (10.L * .9L);
    const long double product = 1.L / (resistance * 10.L * .1L * .9L);
    const long double fast = (sum + std::sqrt(sum * sum - 4.L * product)) / 2.L;
    const long double slow = product / fast;
    const auto crossing = [&](bool receiver, long double threshold) {
      const long double slope = receiver ? 0.L : 1.L / (resistance * .1L);
      const long double slow_weight = (fast - slope) / (fast - slow);
      const auto decay = [&](long double pole, long double t) {
        return t < ramp ? 1.L - t / ramp - std::expm1(-pole * t) / (pole * ramp)
                        : -std::expm1(-pole * ramp) / (pole * ramp) * std::exp(-pole * (t - ramp));
      };
      long double low = 0.L, high = ramp + 100.L / slow;
      for (int i = 0; i < 160; ++i) {
        const long double t = (low + high) / 2.L;
        const long double v = slow_weight * decay(slow, t) + (1.L - slow_weight) * decay(fast, t);
        if (v > threshold) low = t;
        else high = t;
      }
      return static_cast<double>((low + high) / 2.L);
    };
    require(std::abs(result->delays[1] - result->delays[0] - crossing(true, .5L) + crossing(false, .5L)) < 1e-7,
            "wire delay disagrees with the nodal-admittance solution");
    for (int terminal = 0; terminal < 2; ++terminal)
      require(std::abs(result->slews[terminal] - crossing(terminal == 1, .3L) + crossing(terminal == 1, .7L)) < 1e-7,
              "shielded RC slew disagrees with the nodal-admittance solution");

  }
  db.network().pins()[source].set_net_name("shielded");
  db.parasitics().nets()["shielded"] = net;
  ReceiverThresholdCalculator thresholds(db);
  ArnoldiResponseCalculator response(db, config, rc, models, thresholds);
  const auto physical_minimum = response.response(source, arc, AnalysisType::kMin, TransType::kRise, .1);
  require(physical_minimum.get_load_slew_map().at(sink) > physical_minimum.get_driver_slew(),
          "default minimum analysis replaced the physical receiver waveform with the driver slew");
  config.min_slew_degradation = 0;
  response.clear();
  models.clear();
  const auto minimum = response.response(source, arc, AnalysisType::kMin, TransType::kRise, .1);
  const auto maximum = response.response(source, arc, AnalysisType::kMax, TransType::kRise, .1);
  require(minimum.get_is_valid() && maximum.get_is_valid(), "shielded response missing");
  require(minimum.get_load_slew_map().at(sink) > minimum.get_driver_slew(), "minimum slew lost the receiver's RC degradation");
  require(maximum.get_load_slew_map().at(sink) > maximum.get_driver_slew()
              && std::abs(minimum.get_wire_delay_map().at(sink) - maximum.get_wire_delay_map().at(sink)) < 1e-12,
          "minimum analysis changed physical net delay or maximum slew");
  config.min_slew_degradation = 1;
  response.clear();
  models.clear();
  const auto degraded = response.response(source, arc, AnalysisType::kMin, TransType::kRise, .1);
  require(degraded.get_is_valid() && degraded.get_load_slew_map().at(sink) == maximum.get_load_slew_map().at(sink),
          "legacy minimum slew setting changed the physical receiver waveform");
}
void testOrdinaryDriverCrossing()
{
  using namespace ista;
  ArnoldiDelayCalculator calculator;
  Database db;
  Config config;
  PinCapacitanceCalculator caps(db);
  RcTreeAnalyzer rc(db, caps);
  RcModelBuilder models(db, config, rc);
  std::string source = "source", sink = "sink";
  db.network().pins()[source].set_is_port(true);
  db.network().pins()[sink].set_is_port(true);
  ParasiticNet net;
  net.set_net_name("ordinary");
  net.get_node_map()[source].set_capacitance(.05);
  net.get_node_map()[sink].set_capacitance(.25);
  ParasiticResistor wire;
  wire.set_source_node(source);
  wire.set_sink_node(sink);
  wire.set_resistance(500.);
  net.get_resistor_list().push_back(wire);
  TimingArc arc;
  arc.set_slew_derate(.5);
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    auto& delay = arc.get_delay_table_map()[edge];
    delay.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
    delay.set_axis_list({{0., 1.}});
    auto& slew = arc.get_slew_table_map()[edge];
    slew.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
    slew.set_axis_list({{0., 1.}});
    slew.set_value_list({.3, 5.9});
    const auto& model = models.arnoldiModel(net, source, AnalysisType::kMax, edge);
    std::optional<ArnoldiDelayResult> baseline;
    // Adding a constant to every library delay translates arrivals only.
    for (double offset : {0., -.9, -1.4}) {
      delay.set_value_list({.2 + offset, 2.7 + offset});
      const auto result = calculator.calculate(arc, edge, .1, model);
      require(result.has_value(), "ordinary driver failed to fit");
      if (!baseline)
        baseline = result;
      require(std::abs(result->effective_capacitance - baseline->effective_capacitance) < 1e-8
                  && std::abs(result->drive_resistance - baseline->drive_resistance) < 1e-8
                  && std::abs(result->driver_ramp - baseline->driver_ramp) < 1e-8,
              "an arrival offset changed the physical driver model");
      for (std::size_t terminal = 0; terminal < result->delays.size(); ++terminal)
        require(std::abs(result->delays[terminal] - baseline->delays[terminal] - offset) < 1e-8
                    && std::abs(result->slews[terminal] - baseline->slews[terminal]) < 1e-8,
                "delay translation changed the receiver transition");
      require(std::abs(waveformMoment(*result) / model.get_total_capacitance() - result->drive_resistance) < 1e-8,
              "reported driver resistance differs from the waveform conductance");
    }
    delay.set_value_list({.2, 2.7});
    for (double resistance : {500., 1450., 1500., 1450.}) {
      net.get_resistor_list().front().set_resistance(resistance);
      models.clear();
      rc.clear();
      const auto& changed_model = models.arnoldiModel(net, source, AnalysisType::kMax, edge);
      const auto result = calculator.calculate(arc, edge, .1, changed_model);
      require(result.has_value(), "conductance update lost a valid driver fit");
      // First moments follow directly from the two-node conductance matrix.
      require(std::abs(waveformMoment(*result) / .3 - result->drive_resistance) < 1e-8,
              "a changed resistance retained an old network response");
      require(std::abs(waveformMoment(*result, 1) - waveformMoment(*result) - resistance * 1e-3 * .25) < 1e-9,
              "source and receiver responses use inconsistent network conductances");
    }
    net.get_resistor_list().front().set_resistance(500.);
    models.clear();
    rc.clear();
  }
}

void testNonlinearEffectiveLoadConvergence()
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
  net.set_net_name("slow_charge");
  net.get_node_map()[source].set_capacitance(.05);
  net.get_node_map()[sink].set_capacitance(.25);
  ParasiticResistor wire;
  wire.set_source_node(source);
  wire.set_sink_node(sink);
  wire.set_resistance(2000.);
  net.get_resistor_list().push_back(wire);
  TimingArc arc;
  arc.set_slew_derate(1.);
  arc.set_output_threshold_pct_rise(.4);
  auto& delay = arc.get_delay_table_map()[TransType::kRise];
  delay.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
  delay.set_axis_list({{0., 1.}});
  delay.set_value_list({.2, 1.2});
  auto& slew = arc.get_slew_table_map()[TransType::kRise];
  slew.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
  slew.set_axis_list({{0., .1, .2, .3}});
  slew.set_value_list({.05, .2, .4, 1.});
  const auto& model = models.arnoldiModel(net, source, AnalysisType::kMax, TransType::kRise);
  const auto result = ArnoldiDelayCalculator{}.calculate(arc, TransType::kRise, .1, model);
  require(result && result->effective_capacitance > .05 && result->effective_capacitance < .3,
          "nonlinear transition table lost the physical effective-load solution");
  require(std::abs(result->delays.at(0) - .2 - result->effective_capacitance) < 1e-7,
          "nonlinear transition table lost its source delay constraint");
  // A physically valid root can lie above a nonphysical LUT extrapolation.
  // The near capacitor alone is larger than the positive-slew domain boundary.
  net.get_node_map()[source].set_capacitance(.15);
  net.get_node_map()[sink].set_capacitance(.15);
  slew.set_axis_list({{.1, .3}});
  slew.set_value_list({.01, .4});
  models.clear();
  rc.clear();
  const auto& bounded_model = models.arnoldiModel(net, source, AnalysisType::kMax, TransType::kRise);
  const auto bounded = ArnoldiDelayCalculator{}.calculate(arc, TransType::kRise, .1, bounded_model);
  require(bounded && bounded->effective_capacitance > .15 && bounded->effective_capacitance < .3,
          "invalid zero-load extrapolation hid a valid interior charge solution");
  checkChargeBalance(*bounded);


}

void testOrdinaryDriverPolicy()
{
  using namespace ista;
  Database db;
  Config config;
  config.small_net_limit_ps = 0.;
  PinCapacitanceCalculator caps(db);
  RcTreeAnalyzer rc(db, caps);
  RcModelBuilder models(db, config, rc);
  std::string source = "source", sink = "sink";
  db.network().pins()[source].set_is_port(true);
  db.network().pins()[source].set_net_name("distributed");
  db.network().pins()[sink].set_instance_name("receiver");
  db.network().pins()[sink].set_pin_name("A");
  db.network().instances()["receiver"].set_cell_name("receiver_cell");
  auto& receiver = db.liberty().get_cell_map()["receiver_cell"];
  receiver.set_slew_derate_from_library(.5);
  receiver.get_port_map()["A"].set_capacitance(0.);
  auto& net = db.parasitics().nets()["distributed"];
  net.set_net_name("distributed");
  net.get_node_map()[source].set_capacitance(.1);
  net.get_node_map()[sink].set_capacitance(.61);
  ParasiticResistor wire;
  wire.set_source_node(source);
  wire.set_sink_node(sink);
  wire.set_resistance(1000.);
  net.get_resistor_list().push_back(wire);
  TimingArc arc;
  arc.set_slew_derate(.5);
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    auto& delay = arc.get_delay_table_map()[edge];
    delay.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
    delay.set_axis_list({{0., 1.}});
    delay.set_value_list({.05, .85});
    auto& slew = arc.get_slew_table_map()[edge];
    slew.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
    slew.set_axis_list({{0., 1.}});
    slew.set_value_list({.005, 1.525});
  }
  ReceiverThresholdCalculator thresholds(db);
  ArnoldiResponseCalculator response(db, config, rc, models, thresholds);
  auto check_legacy_settings = [&]() {
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
      for (auto edge : {TransType::kRise, TransType::kFall}) {
        config.adjust_driver_resistance = 0;
        config.min_slew_degradation = 0;
        response.clear();
        const auto ordinary = response.response(source, arc, mode, edge, .1);
        require(ordinary.get_is_valid() && !ordinary.loadFailed(sink), "ordinary driver lost a reachable receiver waveform");
        require(std::isfinite(ordinary.get_wire_delay_map().at(sink)) && ordinary.get_load_slew_map().at(sink) > 0.,
                "ordinary driver produced an invalid receiver delay or slew");
        // Legacy settings remain parseable, but every combination must retain
        // the ordinary driver fit and the physical receiver waveform.
        for (int adjust : {0, 1}) {
          for (int degrade : {0, 1}) {
            config.adjust_driver_resistance = adjust;
            config.min_slew_degradation = degrade;
            response.clear();
            const auto selected = response.response(source, arc, mode, edge, .1);
            require(selected.get_is_valid() && !selected.loadFailed(sink), "legacy settings lost the ordinary waveform");
            require(selected.get_effective_capacitance() == ordinary.get_effective_capacitance()
                        && selected.get_gate_delay() == ordinary.get_gate_delay() && selected.get_driver_slew() == ordinary.get_driver_slew(),
                    "legacy settings changed the ordinary driver fit");
            require(selected.get_wire_delay_map() == ordinary.get_wire_delay_map() && selected.get_load_slew_map() == ordinary.get_load_slew_map(),
                    "legacy settings replaced the physical receiver waveform");
          }
        }
      }
    }
  };
  check_legacy_settings();
  arc.set_library_name("driver_library");
  receiver.set_library_name("receiver_library");
  for (double derate : {.25, 1.}) {
    receiver.set_slew_derate_from_library(derate);
    check_legacy_settings();
  }
  arc.set_nom_voltage(3.3);
  receiver.set_nom_voltage(1.2);
  for (double window : {.4, .8}) {
    receiver.set_slew_lower_threshold_pct_rise((1. - window) / 2.);
    receiver.set_slew_upper_threshold_pct_rise((1. + window) / 2.);
    receiver.set_slew_lower_threshold_pct_fall((1. - window) / 2.);
    receiver.set_slew_upper_threshold_pct_fall((1. + window) / 2.);
    for (double derate : {.25, .5, 1.}) {
      receiver.set_slew_derate_from_library(derate);
      check_legacy_settings();
    }
  }
}

void testFlatDelayDriver()
{
  using namespace ista;
  Database db;
  Config config;
  PinCapacitanceCalculator caps(db);
  RcTreeAnalyzer rc(db, caps);
  RcModelBuilder models(db, config, rc);
  std::string source = "pad", sink = "receiver";
  db.network().pins()[source].set_is_port(true);
  db.network().pins()[sink].set_is_port(true);
  ParasiticNet net;
  net.set_net_name("flat_delay");
  const double capacitance = .02;
  net.get_node_map()[source].set_capacitance(capacitance * .5);
  net.get_node_map()[sink].set_capacitance(capacitance * .5);
  ParasiticResistor wire;
  wire.set_source_node(source);
  wire.set_sink_node(sink);
  wire.set_resistance(.001);
  net.get_resistor_list().push_back(wire);
  TimingArc arc;
  arc.set_slew_lower_threshold_pct_rise(.1);
  arc.set_slew_upper_threshold_pct_rise(.9);
  auto& delay = arc.get_delay_table_map()[TransType::kRise];
  delay.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
  delay.set_axis_list({{0., capacitance}});
  delay.set_value_list({.004, .004});
  auto& slew = arc.get_slew_table_map()[TransType::kRise];
  slew.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
  slew.set_axis_list({{0., capacitance}});
  slew.set_value_list({1.8, 1.8});
  const auto& model = models.arnoldiModel(net, source, AnalysisType::kMax, TransType::kRise);
  const auto result = ArnoldiDelayCalculator{}.calculate(arc, TransType::kRise, 0., model);
  require(result.has_value(), "flat delay / long slew driver failed to fit");
  // With no delay sensitivity, the explicit limit is the RC step resistance
  // inferred from the physical 10--90% slew. A finite plateau ramp preserves it.
  require(std::abs(result->drive_resistance * model.get_total_capacitance() * std::log(9.) - 1.8) < 1e-8,
          "flat delay driver did not use the defined slew-limited model");
  require(result->driver_ramp > 0. && std::abs(result->slews[0] - 1.8) < 1e-7 && std::isfinite(result->delays[0]),
          "flat delay driver lost the library transition");

  // An initially valid fit can enter a locally decreasing delay-table segment
  // after shielding reduces the load. Preserve the physical charge solution.
  net.get_node_map()[source].set_capacitance(.01);
  net.get_node_map()[sink].set_capacitance(.99);
  net.get_resistor_list().front().set_resistance(100000.);
  delay.set_axis_list({{0., .2, 1., 1.01}});
  delay.set_value_list({.15, .05, .45, .455});
  slew.set_axis_list({{0., 1.}});
  slew.set_value_list({1.2, 1.2});
  arc.set_slew_lower_threshold_pct_rise(.3);
  arc.set_slew_upper_threshold_pct_rise(.7);
  models.clear();
  rc.clear();
  const auto& shielded_model = models.arnoldiModel(net, source, AnalysisType::kMax, TransType::kRise);
  const auto shielded = ArnoldiDelayCalculator{}.calculate(arc, TransType::kRise, 0., shielded_model);
  require(shielded.has_value(), "joint fit rejected a shielded load instead of retaining the charge solution");
  require(shielded->effective_capacitance < .2, "charge fallback did not retain the shielded load");
  require(std::abs(waveformMoment(*shielded) / shielded_model.get_total_capacitance() - shielded->drive_resistance) < 1e-9,
          "charge fallback retained the partial joint-fit network conductance");

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
  checkChargeBalance(*nonlinear);
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

void testIncompleteNetwork()
{
  using namespace ista;
  Database db;
  Config config;
  std::string source = "in", net_name = "n";
  auto& net = db.network().nets()[net_name];
  net.set_net_name(net_name);
  net.set_pin_name_list({source, "a", "b", "missing"});
  net.set_driver_pin_list({source});
  net.set_load_pin_list({"a", "b", "missing"});
  for (const auto& name : net.get_pin_name_list()) {
    auto& pin = db.network().pins()[name];
    pin.set_is_port(true);
    pin.set_full_name(name);
    pin.set_net_name(net_name);
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
      for (auto edge : {TransType::kRise, TransType::kFall})
        db.sdc().get_port_constraint_map()[name].set_load(mode, edge, name == "missing" ? .2 : 0., false);
  }
  auto& parasitic = db.parasitics().nets()[net_name];
  parasitic.set_net_name(net_name);
  parasitic.set_lumped_capacitance(.041454);
  for (const auto& name : {source, std::string("a"), std::string("b")})
    parasitic.get_node_map()[name].set_capacitance(.041454 / 3.);
  for (const auto& name : {"a", "b"}) {
    ParasiticResistor resistor;
    resistor.set_source_node(source);
    resistor.set_sink_node(name);
    resistor.set_resistance(318.515 / 2.);
    parasitic.get_resistor_list().push_back(resistor);
  }
  PinCapacitanceCalculator caps(db);
  RcTreeAnalyzer rc(db, caps);
  RcModelBuilder models(db, config, rc);
  ReceiverThresholdCalculator thresholds(db);
  ArnoldiResponseCalculator arnoldi(db, config, rc, models, thresholds);
  DmpResponseCalculator dmp(db, rc, models, thresholds);
  InputPortResponseCalculator input(db, rc, models, thresholds);
  WireDelayCalculator wire(db, rc, dmp, arnoldi, input);
  LoadCalculator load(db, caps, rc);
  TimingArc timing;
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    timing.get_delay_table_map()[edge].set_value_list({.2});
    timing.get_slew_table_map()[edge].set_value_list({.1});
  }
  require(!arnoldi.response(source, timing, AnalysisType::kMax, TransType::kRise, .1).get_is_valid(),
          "incomplete network entered the distributed driver solver");
  require(!dmp.response(source, timing, AnalysisType::kMax, TransType::kRise, .1, .241454).get_is_valid(), "incomplete network entered the pi driver solver");
  Arc arc;
  arc.set_type(ArcType::kNet);
  arc.set_owner_name(net_name);
  arc.set_source_pin(source);
  // In ord_000026 a missing output-port annotation uses a balanced tree for
  // every receiver, retaining its SDC capacitance.
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      require(std::abs(load.pinLoad(source, mode, edge) - .241454) < 1e-12, "missing receiver lost its SDC load at the driver");
      for (const auto& sink : {"missing", "a"}) {
        arc.set_sink_pin(sink);
        double cap = std::string(sink) == "missing" ? .2 : 0.;
        double expected = .318515 / 3. * (cap + .041454 / 3.);
        require(std::abs(wire.calcNetArcDelay(arc, mode, edge, .1) - expected) < 1e-12, "incomplete RC did not use balanced total R/C for every load");
        require(wire.calcNetArcSlew(arc, mode, edge, .1) == .1, "incomplete RC manufactured slew degradation");
      }
    }
  // A named but electrically disconnected receiver is just as incomplete.
  parasitic.get_node_map()["missing"].set_capacitance(0.);
  rc.clear();
  require(rc.hasIncompleteNetwork(parasitic, source), "isolated receiver was treated as connected RC");
  ParasiticResistor repaired;
  repaired.set_source_node(source);
  repaired.set_sink_node("missing");
  repaired.set_resistance(1.);
  parasitic.get_resistor_list().push_back(repaired);
  rc.clear();
  require(!rc.hasIncompleteNetwork(parasitic, source), "RC invalidation retained an old connectivity failure");
}

int main()
{
  testLoadSensitivityKnot();
  using namespace ista;
  testDistributedDriverLibraryConsistency();
  testDistinctRcBranches();
  testIncompleteNetwork();
  testDisabledMultidriverFallback();
  testOrdinaryDriverCrossing();
  testNonlinearEffectiveLoadConvergence();
  testFlatDelayDriver();
  testOrdinaryDriverPolicy();
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

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
void testSmallRcResponse()
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
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      const auto& result = calculator.response(source, timing, mode, edge, .1);
      // PT2018 small-network model: 60 fF wire C, 15 fF pin C, 90 Ohm
      // total R, three receivers. The balanced time constant is 1.8 ps;
      // source correction and receiver degradation must use the same model.
      require(result.get_is_valid() && std::abs(result.get_effective_capacitance() - .075) < 1e-12,
              "small RC source did not retain its total capacitive load");
      require(std::abs(result.get_gate_delay() - .4246257005224976) < 1e-9
                  && std::abs(result.get_driver_slew() - .399084918347181) < 1e-9,
              "small RC source omitted the bounded delay/slew correction");
      for (const auto& sink : net.get_load_pin_list()) {
        require(std::abs(result.get_wire_delay_map().at(sink) - .0012476649250079015) < 1e-9,
                "small RC receiver used a distributed delay instead of the selected lumped model");
        require(std::abs(result.get_load_slew_map().at(sink) - .399096574) < 1e-8,
                "small RC receiver lost slew degradation");
      }
    }
  auto reset = [&]() {
    calculator.clear();
    models.clear();
    rc.clear();
  };
  cell.get_port_map()["A"].get_trans_capacitance_map()[AnalysisType::kMax][TransType::kFall] = .004;
  reset();
  const auto asymmetric = calculator.response(source, timing, AnalysisType::kMax, TransType::kFall, .1);
  require(std::abs(asymmetric.get_gate_delay() - .4156444154963727) < 1e-9
              && std::abs(asymmetric.get_driver_slew() - .38708491831078184) < 1e-9
              && std::abs(asymmetric.get_wire_delay_map().at("a:A") - .0011852816787575064) < 1e-9
              && std::abs(asymmetric.get_load_slew_map().at("a:A") - .38709576462297646) < 1e-9,
          "small RC fall response lost its edge load or rise-based source-slew correction");
  cell.get_port_map()["A"].get_trans_capacitance_map().clear();
  for (auto& resistor : parasitic.get_resistor_list())
    resistor.set_resistance(resistor.get_resistance() * 1.2);
  reset();
  require(!models.arnoldiModel(parasitic, source, AnalysisType::kMax, TransType::kRise).smallNetTimeConstants(),
          "network above the 2 ps bound entered the small RC approximation");
  const auto distributed = calculator.response(source, timing, AnalysisType::kMax, TransType::kRise, .1);
  require(std::abs(distributed.get_wire_delay_map().at("a:A") - distributed.get_wire_delay_map().at("c:A")) > .0001,
          "large RC network lost its separate receiver waveforms");
  for (auto& resistor : parasitic.get_resistor_list())
    resistor.set_resistance(resistor.get_resistance() / 1.2);
  cell.get_port_map()["A"].get_trans_capacitance_map()[AnalysisType::kMax][TransType::kRise] = .02;
  reset();
  require(!models.arnoldiModel(parasitic, source, AnalysisType::kMax, TransType::kFall).smallNetTimeConstants()
              && models.arnoldiModel(parasitic, source, AnalysisType::kMin, TransType::kFall).smallNetTimeConstants().has_value(),
          "small RC selection ignored the other edge or mixed min/max capacitances");
  cell.get_port_map()["A"].get_trans_capacitance_map().clear();
  cell.set_input_threshold_pct_rise(.6);
  reset();
  require(std::abs(calculator.response(source, timing, AnalysisType::kMax, TransType::kRise, .1).get_gate_delay() - .4246257005224976) > 1e-5,
          "different receiver delay thresholds entered the small RC approximation");
  cell.set_input_threshold_pct_rise(.5);
  // A resistor-only node must not hide a disconnected capacitor by making
  // the reachable node count equal to the capacitor-map size.
  parasitic.get_resistor_list().front().set_sink_node("junction");
  ParasiticResistor extra;
  extra.set_source_node("junction");
  extra.set_sink_node("a:A");
  extra.set_resistance(1.);
  parasitic.get_resistor_list().push_back(extra);
  parasitic.get_node_map()["disconnected"].set_capacitance(.001);
  reset();
  require(!models.arnoldiModel(parasitic, source, AnalysisType::kMax, TransType::kRise).smallNetTimeConstants(),
          "disconnected parasitics were folded into a valid small RC response");
  parasitic.get_node_map().erase("disconnected");
  parasitic.get_resistor_list().pop_back();
  parasitic.get_resistor_list().front().set_sink_node("a:A");
  for (int receiver = 3; receiver < 20; ++receiver) {
    const std::string instance = "load" + std::to_string(receiver), pin_name = instance + ":A";
    auto& pin = db.network().pins()[pin_name];
    pin.set_full_name(pin_name);
    pin.set_net_name(name);
    pin.set_instance_name(instance);
    pin.set_pin_name("A");
    db.network().instances()[instance].set_cell_name("cell");
    net.get_load_pin_list().push_back(pin_name);
    net.get_pin_name_list().push_back(pin_name);
    parasitic.get_node_map()[pin_name].set_capacitance(0.);
    extra.set_source_node(source);
    extra.set_sink_node(pin_name);
    extra.set_resistance(110. / 17.);
    parasitic.get_resistor_list().push_back(extra);
    if (receiver == 4 || receiver == 5) {
      reset();
      require(models.arnoldiModel(parasitic, source, AnalysisType::kMax, TransType::kRise).smallNetTimeConstants().has_value()
                  == (receiver == 4),
              "balanced small RC selection must include five receivers and exclude six");
    }
  }
  reset();
  // Twenty loads would reduce the balanced estimate to 1.57 ps, but that
  // reduction is unavailable above five receivers: total RC is 32 ps.
  require(!models.arnoldiModel(parasitic, source, AnalysisType::kMax, TransType::kRise).smallNetTimeConstants(),
          "high fanout bypassed the ordinary total-RC bound");
  for (auto& resistor : parasitic.get_resistor_list())
    resistor.set_resistance(resistor.get_resistance() * .05);
  reset();
  const auto& high_fanout = models.arnoldiModel(parasitic, source, AnalysisType::kMax, TransType::kRise);
  require(high_fanout.smallNetTimeConstants() && std::abs(high_fanout.smallNetTimeConstants()->at(0) - .0016) < 1e-12,
          "ordinary low-RC network incorrectly used the limited-fanout balanced reduction");
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      const auto& result = calculator.response(source, timing, mode, edge, .1);
      // Ordinary low RC retains the table source response at 160 fF. Only
      // the receivers get the 1.6 ps RC degradation; the balanced source
      // correction belongs to the separate, limited-fanout model.
      require(result.get_is_valid() && std::abs(result.get_gate_delay() - .68) < 1e-9
                  && std::abs(result.get_driver_slew() - .74) < 1e-9,
              "ordinary low RC incorrectly corrected the table source response");
      for (const auto& sink : net.get_load_pin_list()) {
        require(std::abs(result.get_wire_delay_map().at(sink) - .0011090354888959125) < 1e-9
                    && std::abs(result.get_load_slew_map().at(sink) - .7400049671697627) < 1e-9,
                "ordinary low RC lost its receiver delay or slew degradation");
      }
    }
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

  // A shared distributed network also feeds a reachable core receiver. PT2018
  // rejects the receiver solve for this whole driver/edge when another receiver
  // requires a voltage outside the source swing; all loads use its explicit
  // nonbounding fallback. A control with both receivers reachable must recover.
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
      require(mixed.get_is_valid() && mixed.loadFailed(sink) && mixed.loadFailed(peer)
                  && !mixed.get_wire_delay_map().contains(peer) && !mixed.loadFailed(source)
                  && mixed.get_wire_delay_map().contains(source),
              "failed shared receiver solve published a sibling's RC delay");
      Arc peer_arc;
      peer_arc.set_type(ArcType::kNet);
      peer_arc.set_owner_name(name);
      peer_arc.set_source_pin(source);
      peer_arc.set_sink_pin(peer);
      require(wire.calcNetArcDelay(peer_arc, mode, edge, mixed.get_driver_slew()) == 0.
                  && wire.calcNetArcSlew(peer_arc, mode, edge, mixed.get_driver_slew()) == mixed.get_driver_slew()
                  && peer_arc.rcFailed(mode, edge),
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
    require(fall.loadFailed(peer) && !rise.loadFailed(peer) && !rise.loadFailed(sink)
                && rise.get_wire_delay_map().at(peer) > 0.,
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
    const auto adjusted = ArnoldiDelayCalculator{}.calculate(arc, edge, .1, model, true);
    require(adjusted && !adjusted->resistance_adjusted, "slow ramp incorrectly triggered the network-limited driver rule");
  }
  db.network().pins()[source].set_net_name("shielded");
  db.parasitics().nets()["shielded"] = net;
  ReceiverThresholdCalculator thresholds(db);
  ArnoldiResponseCalculator response(db, config, rc, models, thresholds);
  const auto physical_minimum = response.response(source, arc, AnalysisType::kMin, TransType::kRise, .1);
  require(physical_minimum.get_load_slew_map().at(sink) > physical_minimum.get_driver_slew(),
          "default minimum analysis replaced the physical receiver waveform with the driver slew");
  config.min_slew_degradation = 0;  // This slow ramp does not trigger resistance adjustment.
  response.clear();
  models.clear();
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
void testDriverFitNearRampEnd()
{
  using namespace ista;
  // A capacitive source isolates the two-load driver solve from RC reduction.
  // The 50% crossing is close to the end of the source ramp. PT O-2018.06-SP1
  // yields R=3.11792628705 kOhm and ramp=1.83916621609 ns for these samples.
  // A separately solved ramp/voltage residual reproduces those parameters.
  constexpr double capacitance = .36961336272691;
  ParasiticArnoldiModel model;
  model.set_is_valid(true);
  model.set_order(1);
  model.set_total_capacitance(capacitance);
  model.set_sqrt_total_capacitance(std::sqrt(capacitance));
  model.set_diagonal_list({0.});
  model.set_projection_list({{1. / std::sqrt(capacitance)}});
  model.set_term_node_list({"source"});
  TimingArc arc;
  arc.set_slew_derate(.5);
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    auto& delay = arc.get_delay_table_map()[edge];
    delay.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
    delay.set_axis_list({{capacitance, 1.01 * capacitance}});
    arc.get_slew_table_map()[edge].set_value_list({2. * 1.0761749744415});
    for (double offset : {0., -2.}) {
      delay.set_value_list({1.2116066217422 + offset, 1.2232328653336 + offset});
      const auto result = ArnoldiDelayCalculator{}.calculate(arc, edge, .104165, model, false);
      require(result && !result->resistance_adjusted, "capacitive driver fit failed near the ramp end");
      require(std::abs(result->drive_resistance - 3.11792628705) < 1e-6
                  && std::abs(result->driver_ramp - 1.83916621609) < 1e-6,
              "driver fit near ramp end selected a different waveform");
    }
  }
}

void testOrdinaryDriverCrossing()
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
    for (double offset : {.2, -1.2}) {
      delay.set_value_list({offset, offset + 2.5});
      for (bool adjust : {false, true}) {
        const auto result = ArnoldiDelayCalculator{}.calculate(arc, edge, .1, model, adjust);
        require(result && !result->resistance_adjusted, "ordinary source spuriously adjusted its resistance");
        // Independent two-node conductance eigensolve, with a ramp fitted to
        // D(C)=offset+2.5*C and physical slew=.15+2.8*C. The first midpoint
        // update meets the delay residual tolerance; charge matching does not
        // represent the same driver waveform. Translation preserves the RC.
        require(std::abs(result->effective_capacitance - .280115399739051) < 1e-6
                    && std::abs(result->drive_resistance - 3.55357132113458) < 1e-5,
                "ordinary driver did not match the distributed source crossing");
        require(std::abs(result->delays[0] - (.698935842717261 + offset)) < 1e-6
                    && std::abs(result->delays[1] - (.82772070301579 + offset)) < 1e-6
                    && std::abs(result->slews[0] - 2.08341278546534) < 1e-6
                    && std::abs(result->slews[1] - 2.10529621563007) < 1e-6,
                "ordinary source and receiver do not share the accepted waveform");
      }
    }
  }
}

void testAdjustedDriver()
{
  using namespace ista;
  Database db;
  Config config;
  PinCapacitanceCalculator caps(db);
  RcTreeAnalyzer rc(db, caps);
  RcModelBuilder models(db, config, rc);
  std::string source = "source", sink = "sink";
  db.network().pins()[source].set_is_port(true);
  db.network().pins()[sink].set_instance_name("receiver");
  db.network().pins()[sink].set_pin_name("A");
  db.network().instances()["receiver"].set_cell_name("receiver_cell");
  db.liberty().get_cell_map()["receiver_cell"].set_slew_derate_from_library(.5);
  db.liberty().get_cell_map()["receiver_cell"].get_port_map()["A"].set_capacitance(0.);
  ParasiticNet net;
  net.set_net_name("limited");
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
    const auto& model = models.arnoldiModel(net, source, AnalysisType::kMin, edge);
    const auto adjusted = ArnoldiDelayCalculator{}.calculate(arc, edge, .1, model, true);
    require(adjusted && adjusted->resistance_adjusted, "network-limited source did not adjust its driver");
    // Independent two-node C*dV/dt + G*V solve. Match the distributed source
    // midpoint and the lumped table slew; stop at the first delay residual
    // below 1% (0.25 ps floor). The fixed R follows the upper crossing.
    require(std::abs(adjusted->drive_resistance - .7473402774234486) < 1e-10
                && std::abs(adjusted->effective_capacitance - .2671456181245039) < 1e-7,
            "adjusted driver ignored the delay residual stopping criterion");
    require(std::abs(adjusted->delays[0] - .2621325282500496) < 1e-6
                && std::abs(adjusted->delays[1] - .9058311525795634) < 1e-6
                && std::abs(adjusted->slews[0] - .877862966512199) < 1e-6
                && std::abs(adjusted->slews[1] - 1.8626295004595543) < 1e-6,
            "adjusted cell and receiver do not share the fitted waveform");
    // The same nodal network with translated delay tables exercises a negative
    // time origin and the absolute residual floor near zero cell delay.
    for (const auto& offset : {std::array{-.95, .2741674270510762, -.7341138634450196, -.09263023789357172},
                               std::array{-.2089, .2617105149494928, .00030879162795094395, .6457456922693947}}) {
      TimingArc translated = arc;
      translated.get_delay_table_map()[edge].set_value_list({offset[0], offset[0] + .8});
      const auto shifted = ArnoldiDelayCalculator{}.calculate(translated, edge, .1, model, true);
      require(shifted && shifted->resistance_adjusted && std::abs(shifted->effective_capacitance - offset[1]) < 1e-7
                  && std::abs(shifted->delays[0] - offset[2]) < 1e-6 && std::abs(shifted->delays[1] - offset[3]) < 1e-6,
              "negative or near-zero driver delay used an invalid residual tolerance");
    }
    // The upper-crossing resistance fit is only defined here for symmetric
    // slew windows around a 50% delay crossing. In particular, a 10--60%
    // window would make its exponential slew exceed the LUT slew by 60%.
    for (bool asymmetric : {true, false}) {
      TimingArc unsupported = arc;
      unsupported.set_slew_lower_threshold_pct_rise(asymmetric ? .1 : .3);
      unsupported.set_slew_upper_threshold_pct_rise(asymmetric ? .6 : .7);
      unsupported.set_slew_lower_threshold_pct_fall(asymmetric ? .1 : .3);
      unsupported.set_slew_upper_threshold_pct_fall(asymmetric ? .6 : .7);
      unsupported.set_output_threshold_pct_rise(asymmetric ? .5 : .4);
      unsupported.set_output_threshold_pct_fall(asymmetric ? .5 : .4);
      const auto ordinary = ArnoldiDelayCalculator{}.calculate(unsupported, edge, .1, model);
      const auto selected = ArnoldiDelayCalculator{}.calculate(unsupported, edge, .1, model, true);
      require(ordinary && selected && !selected->resistance_adjusted,
              "driver adjustment accepted an unsupported threshold convention");
      require(selected->delays == ordinary->delays && selected->slews == ordinary->slews,
              "unsupported driver adjustment changed the ordinary waveform");
    }
  }
  db.network().pins()[source].set_net_name("limited");
  db.parasitics().nets()["limited"] = net;
  ReceiverThresholdCalculator thresholds(db);
  ArnoldiResponseCalculator response(db, config, rc, models, thresholds);
  config.min_slew_degradation = 0;
  for (int adjust : {0, 1}) {
    config.adjust_driver_resistance = adjust;
    response.clear();
    const auto early = response.response(source, arc, AnalysisType::kMin, TransType::kRise, .1);
    const auto late = response.response(source, arc, AnalysisType::kMax, TransType::kRise, .1);
    require(early.get_is_valid() && late.get_is_valid(), "driver policy lost the physical waveform");
    require(std::abs(early.get_wire_delay_map().at(sink) - late.get_wire_delay_map().at(sink)) < 1e-10,
            "minimum slew policy changed physical wire delay");
    require(adjust ? std::abs(early.get_load_slew_map().at(sink) - early.get_driver_slew()) < 1e-10
                   : early.get_load_slew_map().at(sink) > early.get_driver_slew(),
            "minimum slew rule did not follow the driver fit state");
    config.min_slew_degradation = 1;
    response.clear();
    const auto physical = response.response(source, arc, AnalysisType::kMin, TransType::kRise, .1);
    require(std::abs(physical.get_load_slew_map().at(sink) - late.get_load_slew_map().at(sink)) < 1e-10,
            "explicit minimum degradation did not use the physical receiver waveform");
    config.min_slew_degradation = 0;
  }
  // Different libraries can encode the same physical slew with different
  // derates. The minimum-slew policy must convert that value, not keep the
  // degraded receiver waveform merely because the scale differs.
  arc.set_library_name("driver_library");
  auto& receiver = db.liberty().get_cell_map().at("receiver_cell");
  receiver.set_library_name("receiver_library");
  config.adjust_driver_resistance = 1;
  for (double derate : {.25, 1.}) {
    receiver.set_slew_derate_from_library(derate);
    response.clear();
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      const auto early = response.response(source, arc, AnalysisType::kMin, edge, .1);
      const auto late = response.response(source, arc, AnalysisType::kMax, edge, .1);
      require(early.get_is_valid() && late.get_is_valid(), "mixed-derate receiver lost its waveform");
      require(std::abs(early.get_load_slew_map().at(sink) * derate - early.get_driver_slew() * .5) < 1e-10,
              "minimum receiver slew failed to convert the adjusted driver's physical transition");
      require(std::abs(early.get_wire_delay_map().at(sink) - late.get_wire_delay_map().at(sink)) < 1e-10,
              "mixed-derate minimum slew replacement changed physical delay");
    }
  }
  arc.set_nom_voltage(3.3);
  receiver.set_nom_voltage(1.2);
  for (double window : {.4, .8})
    for (double derate : {.25, .5, 1.}) {
      receiver.set_slew_lower_threshold_pct_rise((1. - window) / 2.);
      receiver.set_slew_upper_threshold_pct_rise((1. + window) / 2.);
      receiver.set_slew_lower_threshold_pct_fall((1. - window) / 2.);
      receiver.set_slew_upper_threshold_pct_fall((1. + window) / 2.);
      receiver.set_slew_derate_from_library(derate);
      response.clear();
      for (auto edge : {TransType::kRise, TransType::kFall}) {
        const auto early = response.response(source, arc, AnalysisType::kMin, edge, .1);
        const auto late = response.response(source, arc, AnalysisType::kMax, edge, .1);
        require(early.get_is_valid() && late.get_is_valid() && !early.loadFailed(sink), "reachable mixed-voltage receiver failed");
        require(std::abs(early.get_load_slew_map().at(sink) * derate - early.get_driver_slew() * .5 * (1.2 / 3.3) * window / .4) < 1e-10,
                "minimum receiver slew omitted source-to-receiver voltage conversion");
        require(std::abs(early.get_wire_delay_map().at(sink) - late.get_wire_delay_map().at(sink)) < 1e-10,
                "mixed-voltage minimum slew replacement changed physical delay");
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
  const double capacitance = .014547192195095704;
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
  delay.set_axis_list({{capacitance, 1.01 * capacitance}});
  delay.set_value_list({.0039669275283813477, .0040066242218017578});
  auto& slew = arc.get_slew_table_map()[TransType::kRise];
  slew.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
  slew.set_axis_list({{capacitance, 1.01 * capacitance}});
  slew.set_value_list({1.7700380086898804, 1.7701241970062256});
  const auto& model = models.arnoldiModel(net, source, AnalysisType::kMax, TransType::kRise);
  const auto result = ArnoldiDelayCalculator{}.calculate(arc, TransType::kRise, 0., model);
  require(result.has_value(), "flat delay / long slew driver failed to fit");
  // PT2018 single-load fit: R=44.527882 kOhm, ramp=1.232865 ns.
  // Independently solving the alternating RC equations gives 44.718081 and
  // 1.222281. A delay-slope-only fit gives .272889 and the wrong waveform.
  require(std::abs(result->drive_resistance - 44.527882) < .5 && std::abs(result->driver_ramp - 1.232865) < .03,
          "flat delay driver still fits resistance from delay slope alone");
  require(std::abs(result->slews[0] - 1.770038) < .02,
          "joint driver fit lost the physical library transition");
  delay.set_value_list({.004, .004});
  const auto constant_delay = ArnoldiDelayCalculator{}.calculate(arc, TransType::kRise, 0., model);
  require(constant_delay && constant_delay->drive_resistance > 40. && std::isfinite(constant_delay->delays[0]),
          "zero delay sensitivity discarded the finite-slew waveform");

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
  require(shielded && shielded->effective_capacitance < .2,
          "joint fit rejected a shielded load instead of retaining the charge solution");

  // The network-limited decision belongs to each trial waveform, before
  // Ceff converges. This model crosses the decision boundary during shielding.
  net.get_node_map()[source].set_capacitance(.01);
  net.get_node_map()[sink].set_capacitance(.564);
  net.get_resistor_list().front().set_resistance(2000.);
  delay.set_axis_list({{0., 1.}});
  delay.set_value_list({.48, 3.88});
  slew.set_value_list({.72, 1.786});
  arc.set_slew_derate(.5);
  models.clear();
  rc.clear();
  const auto& trial_model = models.arnoldiModel(net, source, AnalysisType::kMax, TransType::kRise);
  const auto trial = ArnoldiDelayCalculator{}.calculate(arc, TransType::kRise, 0., trial_model, true);
  require(trial && trial->resistance_adjusted, "late adjustment test missed a network-limited trial waveform");
  const auto unadjusted = ArnoldiDelayCalculator{}.calculate(arc, TransType::kRise, 0., trial_model, false);
  require(unadjusted && !unadjusted->resistance_adjusted, "disabled adjustment changed the ordinary driver policy");
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
  testSmallRcResponse();
  testDisabledMultidriverFallback();
  testDriverFitNearRampEnd();
  testOrdinaryDriverCrossing();
  testFlatDelayDriver();
  testAdjustedDriver();
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

// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <stdexcept>
#include <string>
#include <Eigen/Eigenvalues>
#include "ArnoldiResponseCalculator.hpp"
#include "ArnoldiWaveformSolver.hpp"
#include "PinCapacitanceCalculator.hpp"

namespace {
void require(bool condition, const char* message)
{
  if (!condition) throw std::runtime_error(message);
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
        const auto& delays = result.get_wire_delay_map();
        require(delays.at("a:A") < delays.at("b:A") && delays.at("b:A") < delays.at("c:A"),
                "unequal RC branches collapsed to the same receiver delay");
        for (const auto& sink : net.get_load_pin_list())
          require(std::isfinite(result.get_load_slew_map().at(sink)) && result.get_load_slew_map().at(sink) > 0.,
                  "passive RC receiver lost its finite transition");
      }
  }

}
void testIdealShortInLoop()
{
  using namespace ista;
  Database db;
  Config config;
  PinCapacitanceCalculator caps(db);
  RcTreeAnalyzer rc(db, caps);
  RcModelBuilder models(db, config, rc);
  std::string source = "source", tap = "tap", sink = "sink";
  ParasiticNet net;
  net.set_net_name("short_loop");
  for (const auto& node : {source, tap, sink})
    db.network().pins()[node].set_is_port(true);
  net.get_node_map()[source].set_capacitance(.1);
  net.get_node_map()[tap].set_capacitance(.2);
  net.get_node_map()[sink].set_capacitance(.4);
  const auto add = [&](const std::string& a, const std::string& b, double resistance) {
    ParasiticResistor resistor;
    resistor.set_source_node(a);
    resistor.set_sink_node(b);
    resistor.set_resistance(resistance);
    net.get_resistor_list().push_back(resistor);
  };
  add(source, tap, 0.);
  add(tap, sink, 100.);
  add(source, sink, 200.);
  const auto& model = models.arnoldiModel(net, source, AnalysisType::kMax, TransType::kRise);
  const auto waveform = ArnoldiWaveformSolver::poleResidue(model, 1.);
  require(waveform.get_is_valid(), "ideal short made the passive loop invalid");
  const auto moment = [&](const std::string& node) {
    double value = 0.;
    const auto terminal = model.get_term_index_map().at(node);
    for (std::size_t i = 0; i < waveform.get_pole_list().size(); ++i)
      value += waveform.get_residue_list()[terminal][i] / waveform.get_pole_list()[i];
    return value;
  };
  // Source and tap are the same voltage; the other two branches are parallel.
  require(std::abs(moment(source) - .7) < 1e-10 && std::abs(moment(tap) - .7) < 1e-10,
          "ideal short acquired artificial source delay");
  require(std::abs(moment(sink) - .7 - (.1 * .2 / (.1 + .2)) * .4) < 1e-10,
          "short contraction discarded a parallel conductive branch");
}

void testScaledLadderAgainstNodalSolution()
{
  using namespace ista;
  // Four dynamic nodes fit within the reduced dimension. Solve their complete
  // nodal system independently; uniformly scaling resistance must only scale
  // time, including when all time constants are small.
  const Eigen::Vector4d capacitances(.1, .2, .3, .4);
  const double driver_resistance = .35, ramp = .3;
  const double branches[] = {.12, .4, .9};
  Eigen::Matrix4d conductance = Eigen::Matrix4d::Zero();
  conductance(0, 0) = 1. / driver_resistance;
  for (int node = 1; node < 4; ++node) {
    const double g = 1. / branches[node - 1];
    conductance(node - 1, node - 1) += g;
    conductance(node, node) += g;
    conductance(node - 1, node) -= g;
    conductance(node, node - 1) -= g;
  }
  const Eigen::Vector4d roots = capacitances.cwiseSqrt();
  const Eigen::Matrix4d inverse_roots = roots.cwiseInverse().asDiagonal();
  Eigen::SelfAdjointEigenSolver<Eigen::Matrix4d> full(inverse_roots * conductance * inverse_roots);
  require(full.info() == Eigen::Success, "full nodal reference did not solve");
  const Eigen::Vector4d weights = full.eigenvectors().transpose() * roots;
  const auto exact_crossing = [&](int node, double threshold) {
    long double low = 0., high = ramp + 100. / full.eigenvalues()[0];
    for (int iteration = 0; iteration < 160; ++iteration) {
      const long double t = (low + high) / 2.;
      long double remaining = 0.;
      for (int mode = 0; mode < 4; ++mode) {
        const long double pole = full.eigenvalues()[mode];
        const long double decay = t < ramp ? 1.L - t / ramp - std::expm1(-pole * t) / (pole * ramp)
            : -std::expm1(-pole * ramp) / (pole * ramp) * std::exp(-pole * (t - ramp));
        remaining += full.eigenvectors()(node, mode) * weights[mode] / roots[node] * decay;
      }
      if (remaining > threshold) low = t;
      else high = t;
    }
    return static_cast<double>((low + high) / 2.);
  };
  for (double scale : {1e-6, 1., 1e6}) {
    Database db;
    Config config;
    PinCapacitanceCalculator caps(db);
    RcTreeAnalyzer rc(db, caps);
    RcModelBuilder models(db, config, rc);
    ParasiticNet net;
    net.set_net_name("scaled_ladder");
    std::string source = "n0";
    for (int node = 0; node < 4; ++node) {
      const auto name = "n" + std::to_string(node);
      db.network().pins()[name].set_is_port(true);
      net.get_node_map()[name].set_capacitance(capacitances[node]);
      if (node > 0) {
        ParasiticResistor resistor;
        resistor.set_source_node("n" + std::to_string(node - 1));
        resistor.set_sink_node(name);
        resistor.set_resistance(branches[node - 1] * 1000. * scale);
        net.get_resistor_list().push_back(resistor);
      }
    }
    const auto& model = models.arnoldiModel(net, source, AnalysisType::kMax, TransType::kRise);
    const auto waveform = ArnoldiWaveformSolver::poleResidue(model, driver_resistance * scale);
    require(waveform.get_is_valid(), "scaling a passive ladder invalidated its response");
    for (int node = 0; node < 4; ++node) {
      const auto terminal = model.get_term_index_map().at("n" + std::to_string(node));
      for (double threshold : {.1, .5, .9}) {
        double crossing = 0.;
        ArnoldiWaveformSolver::crossingTime(ramp * scale, waveform, terminal, threshold, crossing);
        require(std::abs(crossing / scale - exact_crossing(node, threshold)) < 1e-8,
                "scaled RC ladder disagrees with its full nodal crossing");
      }
    }
  }
}
}  // namespace

int main()
{
  testDistinctRcBranches();
  testIdealShortInLoop();
  testScaledLadderAgainstNodalSolution();
}

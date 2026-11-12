// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <stdexcept>
#include <vector>

#include "ArnoldiDelayCalculator.hpp"
#include "ArnoldiWaveformSolver.hpp"
#include "DelayResult.hpp"
#include "DmpDelayCalculator.hpp"

namespace {
void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}
bool near(double actual, double expected)
{
  return std::abs(actual - expected) < 1e-10;
}
ista::TimingArc makeArc(double delay, double slew)
{
  ista::TimingArc arc;
  arc.set_time_unit_scale(2.0);
  arc.set_cap_unit_scale(3.0);
  arc.get_delay_table_map()[ista::TransType::kRise].set_value_list({delay});
  arc.get_slew_table_map()[ista::TransType::kRise].set_value_list({slew});
  return arc;
}

void testInputPortResponse()
{
  using namespace ista;
  ParasiticArnoldiModel model;
  model.set_is_valid(true);
  model.set_order(1);
  model.set_sqrt_total_capacitance(1.0);
  model.set_diagonal_list({2.0});
  model.set_term_node_list({"source", "load"});
  model.set_projection_list({{1.0, 1.0}});
  const WaveformThresholds source{0.5, 0.1, 0.9, 1.0};
  std::vector<WaveformThresholds> loads{source, {0.25, 0.2, 0.8, 2.0}};
  // Analytic one-RC step: rise t=-tau*ln(1-v), fall t=-tau*ln(v).
  auto rise = ArnoldiWaveformSolver::inputPortResponse(model, TransType::kRise, 0.0, source, loads);
  auto fall = ArnoldiWaveformSolver::inputPortResponse(model, TransType::kFall, 0.0, source, loads);
  require(rise && fall && rise->delays.size() == 2 && fall->slews.size() == 2, "input-port terminal response missing");
  require(near(rise->delays[1], 0.5753641449035618) && near(fall->delays[1], 2.772588722239781), "input-port threshold polarity changed");
  require(near(rise->slews[1], 1.3862943611198906) && near(fall->slews[1], 1.3862943611198906), "input-port receiver slew derate changed");
  require(rise->delays[0] == 0.0 && rise->slews[0] == 0.0, "source terminal result was not preserved");

  model.set_diagonal_list({0.0});
  rise = ArnoldiWaveformSolver::inputPortResponse(model, TransType::kRise, 4.0, source, loads);
  fall = ArnoldiWaveformSolver::inputPortResponse(model, TransType::kFall, 4.0, source, loads);
  require(rise && fall && near(rise->delays[1], -1.25) && near(fall->delays[1], 1.25) && near(rise->slews[1], 1.5) && near(fall->slews[1], 1.5)
              && rise->slews[0] == 4.0,
          "zero-mode ideal ramp or source time reference changed");

  model.set_diagonal_list({-1.0});
  require(!ArnoldiWaveformSolver::inputPortResponse(model, TransType::kRise, 0.0, source, loads), "negative time constant accepted");
  model.set_diagonal_list({2.0});
  loads[1].derate = 0.0;
  require(!ArnoldiWaveformSolver::inputPortResponse(model, TransType::kRise, 0.0, source, loads), "invalid receiver published partial results");
  loads[1].derate = 2.0;
  model.set_projection_list({});
  require(!ArnoldiWaveformSolver::inputPortResponse(model, TransType::kRise, 0.0, source, loads), "missing projection accepted");

  // A tiny negative eigenvalue is numerical noise relative to the largest mode.
  model.set_order(2);
  model.set_diagonal_list({-5e-13, 1.0});
  model.set_off_diagonal_list({0.0});
  model.set_projection_list({{1.0, 1.0}, {0.0, 0.0}});
  rise = ArnoldiWaveformSolver::inputPortResponse(model, TransType::kRise, 4.0, source, loads);
  require(rise && near(rise->delays[1], -1.25), "relative zero-mode tolerance changed");
  loads.pop_back();
  require(!ArnoldiWaveformSolver::inputPortResponse(model, TransType::kRise, 4.0, source, loads), "missing receiver thresholds accepted");
}

void testSmallRcCharge()
{
  using namespace ista;
  // A single capacitor must have Q/dV=C at every resistance/time scale.
  // An absolute floor on a positive RC eigenvalue invents stored charge.
  ParasiticArnoldiModel model;
  model.set_is_valid(true);
  model.set_order(1);
  model.set_total_capacitance(1.);
  model.set_sqrt_total_capacitance(1.);
  model.set_diagonal_list({0.});
  model.set_projection_list({{1.}});
  model.set_term_node_list({"driver"});
  for (double resistance : {1e-3, 1e-6, 1e-9}) {
    const auto poles = ArnoldiWaveformSolver::poleResidue(model, resistance);
    require(poles.get_is_valid(), "positive single-capacitor model rejected");
    const double capacitance = ArnoldiWaveformSolver::effectiveCapacitance(10. * resistance, resistance, poles, 10. * resistance);
    require(std::abs(capacitance - 1.) < 1e-10, "fast RC pole changed a physical capacitor's charge");
  }
}
double loadCrossing(double ramp, double threshold)
{
  // Independent bisection of the integrated two-pole impulse response.
  // This RC chain has moments (-3, 8, -21), hence these poles/residues.
  auto voltage = [ramp](double time) {
    double value = std::min(time, ramp);
    for (auto [pole, residue] : {std::pair{8.0 / 21, 4.0 / 9}, std::pair{8.0 / 3, -4.0 / 9}}) {
      if (time < ramp)
        value += residue / (pole * pole) * std::expm1(-pole * time);
      else
        value -= residue / (pole * pole) * (std::exp(-pole * (time - ramp)) - std::exp(-pole * time));
    }
    return value / ramp;
  };
  double low = 0.0, high = ramp + 100.0;
  for (int i = 0; i < 100; ++i) {
    const double mid = (low + high) * 0.5;
    if (voltage(mid) < threshold)
      low = mid;
    else
      high = mid;
  }
  return (low + high) * 0.5;
}

void testDmpCrossing()
{
  using namespace ista;
  ParasiticDmpModel model;
  model.set_is_valid(true);
  model.set_driver_capacitance(1);
  model.set_load_capacitance(1);
  auto& load = model.get_load_model_map()["receiver"];
  load.set_is_valid(true);
  load.set_pole_list({8.0 / 21, 8.0 / 3});
  load.set_residue_list({4.0 / 9, -4.0 / 9});
  for (auto edge : {TransType::kRise, TransType::kFall})
    for (double ramp : {0.5, 2.0, 10.0}) {
      TimingArc arc;
      arc.get_delay_table_map()[edge].set_value_list({1.0});
      arc.get_slew_table_map()[edge].set_value_list({0.4 * ramp});
      auto result = DmpDelayCalculator{}.calculate(arc, edge, 0.1, model, 2.0);
      require(result.get_is_valid(), "DMP crossing fixture rejected");
      require(std::abs(result.get_wire_delay_map().at("receiver") - (loadCrossing(ramp, 0.5) - ramp * 0.5)) < 1e-7,
              "DMP delay does not cross its own two-pole waveform");
      require(std::abs(result.get_load_slew_map().at("receiver") - (loadCrossing(ramp, 0.7) - loadCrossing(ramp, 0.3))) < 1e-7,
              "DMP slew does not cross its own two-pole waveform");
    }
}
}  // namespace

int main()
{
  using namespace ista;
  testDmpCrossing();
  testInputPortResponse();
  testSmallRcCharge();
  // One RC step crosses 50% at tau * ln(2); a zero-pole ideal ramp at half its width.
  const std::vector<double> weights{1.0}, time_constants{2.0}, ideal{0.0};
  require(near(ArnoldiWaveformSolver::inputPortCrossing(time_constants, weights, 0.0, 0.5), 1.3862943611198906), "input-port step crossing changed");
  require(near(ArnoldiWaveformSolver::inputPortCrossing(ideal, weights, 4.0, 0.5), 2.0), "ideal ramp crossing changed");

  ParasiticArnoldiModel arnoldi;
  auto arc = makeArc(8, 6);
  ArnoldiDelayCalculator calculator;
  require(!calculator.calculate(arc, TransType::kRise, 0.4, arnoldi), "invalid Arnoldi model accepted");
  arnoldi.set_is_valid(true);
  arnoldi.set_order(1);
  arnoldi.set_total_capacitance(1.0);
  arnoldi.set_sqrt_total_capacitance(1.0);
  arnoldi.set_diagonal_list({0.2});
  arnoldi.set_term_node_list({"driver"});
  arnoldi.set_projection_list({{1.0}});
  auto poles = ArnoldiWaveformSolver::poleResidue(arnoldi, 0.3);
  require(poles.get_is_valid() && near(poles.get_pole_list()[0], 2.0) && near(poles.get_residue_list()[0][0], 1.0),
          "driver resistance was not added to the reduced RC response");
  auto first = calculator.calculate(arc, TransType::kRise, 0.4, arnoldi);
  require(first && first->delays.size() == 1 && first->slews.size() == 1 && std::isfinite(first->delays[0]), "standalone Arnoldi calculation failed");
  auto other_arc = makeArc(12, 2);
  require(bool(calculator.calculate(other_arc, TransType::kRise, 0.8, arnoldi)), "second Arnoldi calculation failed");
  auto repeated = calculator.calculate(arc, TransType::kRise, 0.4, arnoldi);
  require(repeated && repeated->delays == first->delays && repeated->slews == first->slews && repeated->effective_capacitance == first->effective_capacitance,
          "Arnoldi retained another call's state");
  arnoldi.set_term_node_list({});
  require(!calculator.calculate(arc, TransType::kRise, 0.4, arnoldi), "empty Arnoldi terminal set accepted");

  ParasiticDmpModel dmp;
  dmp.set_is_valid(true);
  dmp.set_driver_capacitance(2.0);
  dmp.set_load_capacitance(3.0);
  auto& load = dmp.get_load_model_map()["receiver"];
  load.set_is_valid(true);
  load.set_pole_list({2.0});
  load.set_residue_list({2.0});
  dmp.get_load_model_map()["invalid"] = ParasiticDmpLoadModel{};
  DmpDelayCalculator dmp_calculator;
  DriverResponse result = dmp_calculator.calculate(arc, TransType::kRise, 0.4, dmp, 5.0);
  require(result.get_is_valid() && result.get_gate_delay() == 4 && result.get_driver_slew() == 3 && result.get_effective_capacitance() == 5,
          "DMP capacitance or library-unit conversion changed");
  require(result.get_wire_delay_map().at("receiver") == 0.5 && result.get_load_slew_map().at("receiver") == 3, "single-pole DMP load fallback changed");
  require(!result.get_wire_delay_map().contains("invalid") && !result.get_load_slew_map().contains("invalid"), "invalid load was published");
  require(!dmp_calculator.calculate(arc, TransType::kFall, 0.4, dmp, 5.0).get_is_valid(), "missing DMP edge tables accepted");
  auto second = dmp_calculator.calculate(other_arc, TransType::kRise, 0.8, dmp, 5.0);
  require(second.get_is_valid() && second.get_gate_delay() == 6 && second.get_driver_slew() == 1, "DMP retained another call's table or failure state");
  dmp.set_is_valid(false);
  require(!dmp_calculator.calculate(arc, TransType::kRise, 0.4, dmp, 5.0).get_is_valid(), "invalid DMP model accepted");
}

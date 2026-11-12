// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <stdexcept>

#include "DmpCeffCalculator.hpp"

namespace {
void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
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

ista::TimingArc makeLoadDependentArc()
{
  using namespace ista;
  TimingArc arc;
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    auto& delay = arc.get_delay_table_map()[edge];
    auto& slew = arc.get_slew_table_map()[edge];
    for (auto* table : {&delay, &slew}) {
      table->set_variable_type1(TimingTableVariableType::kInputTransition);
      table->set_variable_type2(TimingTableVariableType::kOutputCapacitance);
      table->set_axis_list({{0.001, 20.0}, {0.001, 20.0}});
    }
    for (double input_slew : {0.001, 20.0}) {
      for (double load : {0.001, 20.0}) {
        delay.get_value_list().push_back(0.05 + 0.1 * input_slew + 0.5 * load);
        slew.get_value_list().push_back(0.1 + 0.5 * input_slew + 0.25 * load);
      }
    }
  }
  return arc;
}

bool near(double actual, double expected)
{
  return std::abs(actual - expected) < 1e-10;
}
}  // namespace

int main()
{
  using namespace ista;
  auto arc = makeArc(8.0, 6.0);
  ParasiticDmpModel model;
  model.set_is_valid(true);
  model.set_driver_capacitance(2.0);
  model.set_load_capacitance(3.0);
  model.set_pi_resistance(0.0);
  DmpCeffCalculator first, second;
  double delay = -1, slew = -1, ceff = -1;
  require(first.calculate(arc, TransType::kRise, 0.4, model, delay, slew, ceff), "capacitive solve failed");
  require(delay == 4.0 && slew == 3.0 && ceff == 5.0, "capacitance or library-unit conversion changed");
  auto other_arc = makeArc(12.0, 2.0);
  require(second.calculate(other_arc, TransType::kRise, 0.8, model, delay, slew, ceff), "second solver failed");
  require(delay == 6.0 && slew == 1.0, "solver instances share table state");
  require(!first.calculate(arc, TransType::kFall, 0.4, model, delay, slew, ceff), "missing edge tables accepted");
  model.set_driver_capacitance(-1.0);
  require(!first.calculate(arc, TransType::kRise, 0.4, model, delay, slew, ceff), "negative capacitance accepted");
  model.set_driver_capacitance(2.0);
  require(first.calculate(arc, TransType::kRise, 0.4, model, delay, slew, ceff), "reuse after failed solve changed");
  require(delay == 4.0 && slew == 3.0 && ceff == 5.0, "failed solve polluted later calculation");
  model.set_is_valid(false);
  require(!first.calculate(arc, TransType::kRise, 0.4, model, delay, slew, ceff), "invalid model accepted");

  // Golden values from the pre-extraction solver. These tables have nonzero
  // load dependence, so both cases exercise waveform solving, not calcCap.
  arc = makeLoadDependentArc();
  model.set_is_valid(true);
  model.set_load_capacitance(1.0);
  model.set_pi_resistance(1.0);
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    model.set_driver_capacitance(0.1);
    require(first.calculate(arc, edge, 0.1, model, delay, slew, ceff), "pi solve failed");
    require(near(delay, 0.20581157892843541) && near(slew, 0.26642228270528345) && near(ceff, 0.29162315785687087),
            "pi waveform or effective capacitance changed");
    model.set_driver_capacitance(1.0);
    require(first.calculate(arc, edge, 0.1, model, delay, slew, ceff), "balanced pi solve failed");
    // The post-ramp capacitive derivative must agree with the waveform's
    // finite difference (see the numerical audit); it changes this Newton root.
    require(near(delay, 0.70889981568388138) && near(slew, 0.55719227959643503) && near(ceff, 1.2977996313677627),
            "balanced pi solve used the inconsistent capacitive derivative");
    model.set_driver_capacitance(0.0);
    require(first.calculate(arc, edge, 0.1, model, delay, slew, ceff), "zero-C2 solve failed");
    require(near(delay, 0.36934647040519247) && near(slew, 0.40214509531658238) && near(ceff, 1.0), "zero-C2 waveform changed");
  }

  // These Pi equations have valid roots even when the approximate analytic
  // Jacobian diverges after the post-ramp derivative correction. References
  // were solved from the unchanged equations with residuals below 1e-8.
  arc = makeLoadDependentArc();
  arc.set_slew_lower_threshold_pct_rise(0.1);
  arc.set_slew_upper_threshold_pct_rise(0.9);
  arc.set_slew_lower_threshold_pct_fall(0.1);
  arc.set_slew_upper_threshold_pct_fall(0.9);
  for (double& value : arc.get_delay_table_map()[TransType::kFall].get_value_list())
    value *= 1.3;
  for (double& value : arc.get_slew_table_map()[TransType::kFall].get_value_list())
    value *= 1.3;
  model.set_driver_capacitance(0.1);
  model.set_pi_resistance(0.2);
  const double inputs[] = {0.0, 0.01, 0.4};
  const double reference[][2]
      = {{0.34478200717247059, 0.39049423046776549}, {0.35561231433879059, 0.40298649452779667}, {0.63334962713416687, 0.69908959654733271}};
  for (int i = 0; i < 3; ++i) {
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      if (i == 0 && edge == TransType::kFall)
        continue;  // This case already converges with the analytic solver.
      require(first.calculate(arc, edge, inputs[i], model, delay, slew, ceff), "Pi recovery failed");
      require(std::abs(ceff - reference[i][edge == TransType::kFall]) < 1e-7, "solvable Pi model fell back to total capacitance");
      require(std::isfinite(delay) && std::isfinite(slew) && slew >= 0.0, "Pi recovery produced invalid timing");
    }
  }
}

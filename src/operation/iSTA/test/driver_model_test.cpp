// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "ArnoldiDelayCalculator.hpp"

namespace {
void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

// Independent integration of C dv/dt = (u-v)/R for a unit linear ramp.
// Long-double bisection creates the synthetic library, without using the solver.
double analyticCrossing(double capacitance, double fraction, long double resistance = 2.L, long double ramp = 3.L)
{
  const long double tau = resistance * capacitance;
  long double lo = 0.L, hi = ramp + 100.L * tau;
  for (int i = 0; i < 200; ++i) {
    const long double t = (lo + hi) / 2.L;
    const long double v = t <= ramp ? (t + tau * std::expm1(-t / tau)) / ramp
                                   : 1.L + tau / ramp * std::expm1(-ramp / tau) * std::exp(-(t - ramp) / tau);
    if (v < fraction)
      lo = t;
    else
      hi = t;
  }
  return static_cast<double>((lo + hi) / 2.L);
}

ista::ParasiticArnoldiModel capacitor(double capacitance)
{
  ista::ParasiticArnoldiModel model;
  model.set_is_valid(true);
  model.set_order(1);
  model.set_total_capacitance(capacitance);
  model.set_sqrt_total_capacitance(std::sqrt(capacitance));
  model.set_diagonal_list({0.});
  model.set_projection_list({{1. / std::sqrt(capacitance)}});
  model.set_term_node_list({"source"});
  return model;
}

void testLocalCharacterization()
{
  using namespace ista;
  TimingArc arc;
  auto& delay = arc.get_delay_table_map()[TransType::kRise];
  delay.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
  delay.set_axis_list({{.25, .5, .9, 1.1}});
  delay.set_value_list({.15, .2, .36, .44});
  arc.get_slew_table_map()[TransType::kRise].set_value_list({1.});
  const auto original = ArnoldiDelayCalculator{}.calculate(arc, TransType::kRise, .1, capacitor(1.));
  delay.set_value_list({.25, .3, .36, .44});
  const auto changed = ArnoldiDelayCalculator{}.calculate(arc, TransType::kRise, .1, capacitor(1.));
  require(original && changed, "local characterization did not converge");
  require(std::abs(original->drive_resistance - changed->drive_resistance) < 1e-10
              && std::abs(original->driver_ramp - changed->driver_ramp) < 1e-10,
          "remote load samples changed the local driver characterization");
  // The same physical table in alternate units and either axis order must
  // identify the same source, including first/last-segment extrapolation.
  for (bool transposed : {false, true}) {
    arc.set_time_unit_scale(1000.);
    arc.set_cap_unit_scale(100.);
    arc.get_slew_table_map()[TransType::kRise].set_value_list({1000.});
    delay.set_variable_type1(transposed ? TimingTableVariableType::kOutputCapacitance : TimingTableVariableType::kInputTransition);
    delay.set_variable_type2(transposed ? TimingTableVariableType::kInputTransition : TimingTableVariableType::kOutputCapacitance);
    delay.set_axis_list(transposed ? std::vector<std::vector<double>>{{90., 110.}, {50., 150.}}
                                   : std::vector<std::vector<double>>{{50., 150.}, {90., 110.}});
    delay.set_value_list(transposed ? std::vector<double>{350., 370., 430., 450.} : std::vector<double>{350., 430., 370., 450.});
    for (double load : {.8, 1., 1.2}) {
      const auto actual = ArnoldiDelayCalculator{}.calculate(arc, TransType::kRise, .1, capacitor(load));
      TimingArc control;
      auto& control_delay = control.get_delay_table_map()[TransType::kRise];
      control_delay.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
      control_delay.set_axis_list({{.9, 1.1}});
      control_delay.set_value_list({.36, .44});
      control.get_slew_table_map()[TransType::kRise].set_value_list({1.});
      const auto expected = ArnoldiDelayCalculator{}.calculate(control, TransType::kRise, .1, capacitor(load));
      require(actual && expected, "scaled or transposed library table did not converge");
      require(std::abs(actual->drive_resistance - expected->drive_resistance) < 1e-10
                  && std::abs(actual->driver_ramp - expected->driver_ramp) < 1e-10
                  && std::abs(actual->delays.front() - expected->delays.front()) < 1e-10,
              "axis order or library units changed local characterization");
    }
  }
}

void testSyntheticDriver()
{
  using namespace ista;
  for (const auto edge : {TransType::kRise, TransType::kFall}) {
    for (const double time_scale : {1e-5, 1., 1e5}) {
      for (const double cap_scale : {.01, 1., 100.}) {
        for (const double offset : {0., -10.}) {
          constexpr double low = .15, high = .8, threshold = .35, derate = .8;
          const bool falling = edge == TransType::kFall;
          const double fraction = falling ? 1. - threshold : threshold;
          // A symmetric perturbation of the independent, integrated response
          // supplies a tangent table without sharing the production derivative.
          const double sensitivity = (analyticCrossing(1.0001, fraction) - analyticCrossing(.9999, fraction)) / .0002;
          const double delay_at_load = offset + analyticCrossing(1., fraction);
          const double physical_slew = falling ? analyticCrossing(1., 1. - low) - analyticCrossing(1., 1. - high)
                                              : analyticCrossing(1., high) - analyticCrossing(1., low);
          TimingArc arc;
          arc.set_slew_derate(derate);
          arc.set_slew_lower_threshold_pct_rise(low);
          arc.set_slew_upper_threshold_pct_rise(high);
          arc.set_slew_lower_threshold_pct_fall(low);
          arc.set_slew_upper_threshold_pct_fall(high);
          arc.set_output_threshold_pct_rise(threshold);
          arc.set_output_threshold_pct_fall(threshold);
          auto& delay = arc.get_delay_table_map()[edge];
          delay.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
          delay.set_axis_list({{.5 * cap_scale, cap_scale}});
          delay.set_value_list({time_scale * (delay_at_load - .5 * sensitivity), time_scale * delay_at_load});
          arc.get_slew_table_map()[edge].set_value_list({time_scale * physical_slew / derate});
          const auto result = ArnoldiDelayCalculator{}.calculate(arc, edge, .1 * time_scale, capacitor(cap_scale));
          require(result.has_value(), "synthetic capacitive driver did not converge");
          const double resistance = result->drive_resistance * cap_scale / time_scale;
          const double ramp = result->driver_ramp / time_scale;
          // An early output crossing can admit two R/ramp pairs. Check the
          // actual observations in both cases; only the identifiable falling
          // case can require recovery of the generating parameters.
          const double actual_sensitivity
              = (analyticCrossing(1.0001, fraction, resistance, ramp) - analyticCrossing(.9999, fraction, resistance, ramp)) / .0002;
          require(std::abs(actual_sensitivity - sensitivity) < 2e-7, "driver does not reproduce local library load sensitivity");
          if (falling) {
            require(std::abs(resistance - 2.) < 2e-6, "identifiable source resistance was not recovered");
            require(std::abs(ramp - 3.) < 2e-6, "identifiable source ramp was not recovered");
          }
          require(std::abs(result->delays.at(0) / time_scale - offset - analyticCrossing(1., fraction)) < 2e-7,
                  "capacitive source delay disagrees with the library threshold");
          require(std::abs(result->slews.at(0) / time_scale - physical_slew / derate) < 2e-7,
                  "capacitive source slew disagrees with the physical library transition");
        }
      }
    }
  }
}

void testInvalidInputs()
{
  using namespace ista;
  TimingArc arc;
  const auto edge = TransType::kRise;
  arc.get_delay_table_map()[edge].set_value_list({1.});
  arc.get_slew_table_map()[edge].set_value_list({1.});
  const auto model = capacitor(1.);
  const auto calculate = [&](double slew) { return ArnoldiDelayCalculator{}.calculate(arc, edge, slew, model); };
  require(!calculate(-1.), "negative input slew accepted");
  require(!calculate(std::numeric_limits<double>::quiet_NaN()), "NaN input slew accepted");
  arc.set_slew_derate(0.);
  require(!calculate(.1), "invalid threshold convention silently replaced");
  arc.set_slew_derate(1.);
  arc.get_slew_table_map().clear();
  require(!calculate(.1), "missing library transition table accepted");
}
}  // namespace

int main(int argc, char** argv)
{
  try {
    if (argc > 1 && std::string(argv[1]) == "invalid")
      testInvalidInputs();
    else {
      testLocalCharacterization();
      testSyntheticDriver();
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

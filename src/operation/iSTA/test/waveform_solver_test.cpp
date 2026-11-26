// SPDX-License-Identifier: MulanPSL-2.0
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ArnoldiWaveformSolver.hpp"

namespace {
using ista::ArnoldiWaveformSolver;
using ista::ParasiticArnoldiPoleResidue;

void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

void expectNear(double actual, double expected, const char* message)
{
  if (!std::isfinite(actual) || std::abs(actual - expected) > 2e-12 * std::abs(expected)) {
    std::ostringstream error;
    error << std::setprecision(17) << message << ": actual=" << actual << ", expected=" << expected;
    throw std::runtime_error(error.str());
  }
}

ParasiticArnoldiPoleResidue model(std::vector<double> poles, std::vector<double> weights)
{
  ParasiticArnoldiPoleResidue result;
  result.set_is_valid(true);
  result.set_order(static_cast<int>(poles.size()));
  result.set_pole_list(poles);
  result.set_residue_list({weights});
  return result;
}

double crossing(const ParasiticArnoldiPoleResidue& poles, double ramp, double voltage)
{
  double time = 0.;
  ArnoldiWaveformSolver::crossingTime(ramp, poles, 0, voltage, time);
  return time;
}

void testSinglePoleCrossings()
{
  // Values obtained independently from the analytic RC response integrated
  // over a width-2 ramp, with 65-decimal-digit arithmetic (tau = 1).
  for (double scale : {1e-9, 1., 1e9}) {
    const auto poles = model({1. / scale}, {1.});
    expectNear(crossing(poles, 2. * scale, .9), .70676057622484655143 * scale, "early single-pole crossing");
    expectNear(crossing(poles, 2. * scale, .5), 1.84140566043696063785 * scale, "middle single-pole crossing");
    expectNear(crossing(poles, 2. * scale, .1), 3.46402445456524131763 * scale, "late single-pole crossing");
  }
}

void testNegligibleSlowMode()
{
  const auto poles = model({1., 1e-30}, {1., 1e-30});
  expectNear(crossing(poles, 2., .5), 1.84140566043696063785, "negligible slow mode widened the crossing interval");
}

void testRoundedRampBound()
{
  // Binary rounding makes y+1 round down when y is just above 2^n-1.
  // A mathematical inverse bracket must still enclose its evaluated root.
  expectNear(ArnoldiWaveformSolver::rampTime(1., std::nextafter(90., 100.), .3), 64., "rounded inverse upper bound");
  double ramp = 0.;
  ArnoldiWaveformSolver::solveRamp(32., 1.2, 1. - .7, .7, ramp);
  expectNear(ramp, 3., "slow-ramp span at an inverse rounding boundary");
}

void testLateCrossing()
{
  const auto poles = model({1.}, {1.});
  // After the ramp: v(t) = (1-exp(-1))*exp(-(t-1)).
  expectNear(crossing(poles, 1., 1e-30), 1. + std::log(-std::expm1(-1.) / 1e-30), "late crossing outside the initial interval");
}

void testFeedthroughAndSignedResidues()
{
  const auto ideal = model({std::numeric_limits<double>::infinity()}, {1.});
  expectNear(crossing(ideal, 2., .1), 1.8, "ideal feedthrough crossing");
  const auto mixed = model({std::numeric_limits<double>::infinity(), 1.}, {.25, .75});
  expectNear(crossing(mixed, 2., .3), 2.07773009344535069879, "mixed feedthrough crossing");
  // Cascade H(s)=2/((s+1)*(s+2)): step residues are 2 and -1.
  const auto chain = model({1., 2.}, {2., -1.});
  expectNear(crossing(chain, 2., .5), 2.31584812014671336130, "signed-residue RC chain crossing");
  double early = 0., middle = 0., late = 0.;
  ArnoldiWaveformSolver::crossingTime(2., chain, 0, .9, early, .5, middle, .1, late);
  expectNear(early, 1.07648902248131835576, "three-threshold early crossing");
  expectNear(middle, 2.31584812014671336130, "three-threshold middle crossing");
  expectNear(late, 4.12260031371539864602, "three-threshold late crossing");
}

void testInvalidCrossings()
{
  require(std::isnan(crossing(model({1.}, {1., 0.}), 2., .5)), "mismatched pole/residue dimensions accepted");
  require(std::isnan(crossing(model({0.}, {1.}), 2., .5)), "zero decay rate accepted");
  require(std::isnan(crossing(model({1.}, {1.}), 0., .5)), "zero driver ramp accepted");
  require(std::isnan(crossing(model({1.}, {1.}), 2., 1.)), "invalid voltage accepted");
  double time = 0.;
  ArnoldiWaveformSolver::crossingTime(2., model({1.}, {1.}), 1, .5, time);
  require(std::isnan(time), "missing terminal accepted");
}

void testRampResponseLimits()
{
  expectNear(ArnoldiWaveformSolver::rampTime(1., 0., .5), std::log(2.), "ideal step limit");
}

void testTinyRampResponse()
{
  expectNear(ArnoldiWaveformSolver::rampTime(1., 1e-20, .5), std::log(2.), "tiny ramp lost the RC response");
}

void testLongRampThreshold()
{
  expectNear(ArnoldiWaveformSolver::rampTime(1., 31., .99), .90576914774537883007, "long ramp near the initial voltage");
}

void testRampInversion()
{
  for (double scale : {1., 1e-9, 1e9}) {
    for (double seed : {1e-8, .1, 100., 0.}) {
      double ramp = seed * scale;
      ArnoldiWaveformSolver::solveRamp(1. / scale, 1.70274763586919145349 * scale, .3, .7, ramp);
      expectNear(ramp, 4. * scale, "ramp inversion retained or depended on its seed");
    }
  }
  double ramp = .01;
  ArnoldiWaveformSolver::solveRamp(1., 31.26541383375753186727, .01, .99, ramp);
  expectNear(ramp, 31., "ramp inversion at nonstandard thresholds");
}

void testRampFeasibility()
{
  const double step_span = std::log(.7 / .3);
  double ramp = 10.;
  ArnoldiWaveformSolver::solveRamp(1., step_span, .3, .7, ramp);
  require(ramp == 0., "step-response span must choose the minimum-width solution");
  ramp = 10.;
  ArnoldiWaveformSolver::solveRamp(1., .5 * step_span, .3, .7, ramp);
  require(std::isnan(ramp), "slew below the step-response limit accepted");
  ArnoldiWaveformSolver::solveRamp(-1., 2., .3, .7, ramp);
  require(std::isnan(ramp), "invalid RC pole accepted");
  ArnoldiWaveformSolver::solveRamp(1., 2., .7, .3, ramp);
  require(std::isnan(ramp), "reversed slew thresholds accepted");
}

void testLongRampInversion()
{
  // Both crossings occur well inside these ramps. The exponential terms
  // are below double precision, leaving span = (upper-lower)*width.
  double ramp = 900.;
  ArnoldiWaveformSolver::solveRamp(1., 400., .3, .7, ramp);
  expectNear(ramp, 1000., "rounded upper bracket rejected a long ramp");
  ramp = 270.;
  ArnoldiWaveformSolver::solveRamp(1., 210., .1, .8, ramp);
  expectNear(ramp, 300., "rounded upper bracket rejected asymmetric thresholds");
}

void testCapacitanceInversion()
{
  // Independent one-RC crossing values from testSinglePoleCrossings.
  const std::array<std::pair<double, double>, 3> samples{{
      {.9, .70676057622484655143}, {.5, 1.84140566043696063785}, {.1, 3.46402445456524131763}}};
  for (const double time_scale : {1e-9, 1., 1e9})
    for (const double cap_scale : {.01, 1., 100.})
      for (const auto& [voltage, time] : samples) {
        const auto capacitance = ArnoldiWaveformSolver::crossingCapacitance(
            2. * time_scale, time_scale / cap_scale, 4. * cap_scale, time * time_scale, voltage);
        require(capacitance.has_value(), "valid one-RC capacitance inversion failed");
        expectNear(*capacitance, cap_scale, "threshold inversion lost the physical capacitance");
      }
  require(!ArnoldiWaveformSolver::crossingCapacitance(2., 1., 4., .5, .5),
          "crossing before the ideal source accepted");
  require(!ArnoldiWaveformSolver::crossingCapacitance(2., -1., 4., 2., .5), "negative drive resistance accepted");
  for (const double scale : {1e-9, 1., 1e9}) {
    const auto boundary = ArnoldiWaveformSolver::crossingCapacitance(2. * scale, scale, 1.,
                                                                   1.84140566043696063785 * scale, .5);
    require(boundary.has_value(), "total-load boundary crossing was rejected");
    expectNear(*boundary, 1., "boundary crossing lost total capacitance");
    require(!ArnoldiWaveformSolver::crossingCapacitance(2. * scale, scale, 1., 3. * scale, .5),
            "unattainable crossing was clipped into a capacitance root");
  }
}
}  // namespace

int main()
{
  using Test = std::pair<const char*, void (*)()>;
  const std::array<Test, 13> tests{{
      {"single pole crossings", testSinglePoleCrossings}, {"negligible slow mode", testNegligibleSlowMode},
      {"late crossing", testLateCrossing}, {"feedthrough and signed residues", testFeedthroughAndSignedResidues},
      {"invalid crossings", testInvalidCrossings}, {"ramp response limits", testRampResponseLimits},
      {"tiny ramp response", testTinyRampResponse}, {"long ramp threshold", testLongRampThreshold},
      {"ramp inversion", testRampInversion}, {"ramp feasibility", testRampFeasibility},
      {"long ramp inversion", testLongRampInversion}, {"capacitance inversion", testCapacitanceInversion},
      {"rounded ramp bound", testRoundedRampBound}}};
  int failed = 0;
  for (const auto& [name, test] : tests) {
    try {
      test();
      std::cout << "PASS " << name << '\n';
    } catch (const std::exception& error) {
      std::cerr << "FAIL " << name << ": " << error.what() << '\n';
      ++failed;
    }
  }
  return failed == 0 ? 0 : 1;
}

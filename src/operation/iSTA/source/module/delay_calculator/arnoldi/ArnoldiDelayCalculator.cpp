// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "ArnoldiDelayCalculator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ArnoldiWaveformSolver.hpp"
#include "TimingTolerance.hpp"

namespace ista {
namespace {
struct DriverFit
{
  double resistance;
  double ramp;
  double start;
};

// Rising response of a ramp source driving one capacitance through R.
double driverVoltage(double time, double tau, double ramp)
{
  if (time <= 0.)
    return 0.;
  if (time <= ramp)
    return (time + tau * std::expm1(-time / tau)) / ramp;
  return 1. - tau / ramp * (-std::expm1(-ramp / tau)) * std::exp(-(time - ramp) / tau);
}

// Start time and resistance both have decreasing voltage residuals. Retain
// the bracket when a Newton step leaves the physical interval.
template <class Residual, class Derivative>
bool solveDriverParameter(double& value, double lower, double upper, Residual residual, Derivative derivative)
{
  if (!(lower < upper) || residual(lower) < 0. || residual(upper) > 0.)
    return false;
  value = std::clamp(value, lower, upper);
  for (int iteration = 0; iteration < 50; ++iteration) {
    const double error = residual(value);
    if (!std::isfinite(error))
      return false;
    if (std::abs(error) < .001)
      return true;
    if (error > 0.)
      lower = value;
    else
      upper = value;
    const double next = value - error / derivative(value);
    value = std::isfinite(next) && next > lower && next < upper ? next : lower + .5 * (upper - lower);
  }
  return false;
}

// Fit a ramp source and series resistance to two neighboring delay samples
// and one physical slew. A flat delay slope alone cannot identify the driver
// waveform. Use bounded alternating solves; reject inconsistent table data.
std::optional<DriverFit> fitDriver(double capacitance, double delay, double next_delay, double slew, double lower, double upper)
{
  const double voltage_log = std::log(upper / lower);
  const double bound = slew / (voltage_log * capacitance);
  const double next_capacitance = 1.01 * capacitance;
  if (!(capacitance > 0. && slew > 0. && bound > 0.) || !std::isfinite(bound) || !std::isfinite(delay) || !std::isfinite(next_delay) || next_delay < delay
      || next_delay - delay > .01 * capacitance * bound)
    return std::nullopt;
  double resistance = .9 * bound;
  const double span = slew / (upper - lower);
  double ramp = .5 * span;
  double start = delay - .5 * span;
  double tolerance = .010001;
  for (int iteration = 0; iteration < 50; ++iteration) {
    if (iteration > 0 && iteration % 10 == 0)
      tolerance *= 2.;
    if (resistance >= bound)
      resistance = .9 * bound;
    const double tau = resistance * capacitance;
    double ramp_lower = 0., ramp_upper = span;
    double ramp_tolerance = .001;
    bool ramp_converged = false;
    for (int trial = 0; trial < 50; ++trial) {
      const double actual = ArnoldiWaveformSolver::rampTime(1. / tau, ramp, lower) - ArnoldiWaveformSolver::rampTime(1. / tau, ramp, upper);
      const double error = actual / slew - 1.;
      if (std::abs(error) < ramp_tolerance) {
        ramp_converged = true;
        break;
      }
      if (error > 0.)
        ramp_upper = ramp;
      else
        ramp_lower = ramp;
      ramp = ramp_lower + .5 * (ramp_upper - ramp_lower);
      if ((trial + 1) % 10 == 0)
        ramp_tolerance *= 2.;
    }
    if (!ramp_converged || !(ramp > 0.) || !std::isfinite(ramp))
      return std::nullopt;
    // Select the derivative from the segment containing the target crossing.
    // Switching segments at each trial gives a different stopping point near
    // the ramp end, which can change the later network-limited decision.
    const bool crossing_during_ramp = driverVoltage(ramp, tau, ramp) >= .5;
    const auto start_residual = [&](double value) { return 2. * driverVoltage(delay - value, tau, ramp) - 1.; };
    const auto start_derivative = [&](double value) {
      const double time = delay - value;
      return crossing_during_ramp ? 2. * std::expm1(-time / tau) / ramp : 2. * (std::exp(-time / tau) - std::exp(-(time - ramp) / tau)) / ramp;
    };
    if (!solveDriverParameter(start, delay - span, delay, start_residual, start_derivative))
      return std::nullopt;
    const double target = next_delay - start;
    const auto resistance_residual = [&](double value) { return 2. * driverVoltage(target, value * next_capacitance, ramp) - 1.; };
    const auto resistance_derivative = [&](double value) {
      const double next_tau = value * next_capacitance;
      const double end = driverVoltage(ramp, next_tau, ramp) >= .5 ? 0. : target - ramp;
      return 2. * next_capacitance / ramp * ((1. + target / next_tau) * std::exp(-target / next_tau) - (1. + end / next_tau) * std::exp(-end / next_tau));
    };
    const double maximum_resistance = (delay - start) / (std::log(2.) * capacitance);
    if (!solveDriverParameter(resistance, 1e-5, maximum_resistance, resistance_residual, resistance_derivative))
      return std::nullopt;
    const double pole = 1. / (resistance * capacitance);
    const double actual_slew = ArnoldiWaveformSolver::rampTime(pole, ramp, lower) - ArnoldiWaveformSolver::rampTime(pole, ramp, upper);
    const double residual = std::abs(actual_slew / slew - 1.) + 2. * std::abs(driverVoltage(delay - start, resistance * capacitance, ramp) - .5)
                            + std::abs(resistance_residual(resistance));
    {
      static const char* fp2 = std::getenv("ISTA_DUMP_FITDRV");
      if (fp2 != nullptr) {
        if (FILE* f2 = std::fopen(fp2, "a"); f2 != nullptr) {
          std::fprintf(f2, "FD,%d,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g\n",
                       static_cast<int>(residual <= tolerance && resistance < bound),
                       capacitance, slew, delay, next_delay, start, ramp, resistance, tau, tolerance);
          std::fclose(f2);
        }
      }
    }
    if (residual <= tolerance && resistance < bound)
      return DriverFit{resistance, ramp, start};
  }
  return std::nullopt;
}
}  // namespace

thread_local const char* g_dump_out_pin = "-";

std::optional<ArnoldiDelayResult> ArnoldiDelayCalculator::calculate(TimingArc& timing_arc, TransType output_trans_type, double input_slew,
                                                                    const ParasiticArnoldiModel& arnoldi_model, bool adjust_drive_resistance) const
{
  if (!arnoldi_model.get_is_valid() || arnoldi_model.get_order() <= 0 || arnoldi_model.get_total_capacitance() <= 0.0) {
    return std::nullopt;
  }
  // A zero input slew means the arc has not been propagated yet. Fitting the
  // driver there produces a model whose ramp is far shorter than the one the
  // real slew would give (FINDINGS 324), and that stale fit then feeds the
  // waveform the SDF reports. ISTA_SKIP_ZERO_SLEW=1 refuses it instead.
  static const bool skip_zero_slew = [] {
    const char* e = std::getenv("ISTA_SKIP_ZERO_SLEW");
    return e != nullptr && *e != '0';
  }();
  if (skip_zero_slew && !(input_slew > 0.0)) {
    return std::nullopt;
  }
  double total_capacitance = arnoldi_model.get_total_capacitance();
  double delay_at_total_capacitance = tableDelay(timing_arc, output_trans_type, input_slew, total_capacitance);
  double delay_at_half_capacitance = tableDelay(timing_arc, output_trans_type, input_slew, 0.5 * total_capacitance);
  double delay_resistance = 0.0;
  if (total_capacitance > ista::kTimingTolerance) {
    delay_resistance = (delay_at_total_capacitance - delay_at_half_capacitance) / (0.5 * total_capacitance);
  }
  if (!(delay_resistance > 0.0)) {
    delay_resistance = 1.0;
  }

  double slew_derate = 1.0;
  double lower_threshold = 0.1;
  double upper_threshold = 0.9;
  double voltage_log = 0.0;
  double min_slew_factor = 0.0;
  double x1 = 0.0;
  double y1 = 0.0;
  ArnoldiWaveformSolver::thresholds(timing_arc, output_trans_type, slew_derate, lower_threshold, upper_threshold, voltage_log, min_slew_factor, x1, y1);

  double output_threshold = output_trans_type == TransType::kRise ? timing_arc.get_output_threshold_pct_rise() : timing_arc.get_output_threshold_pct_fall();
  if (output_threshold > 1.)
    output_threshold *= .01;
  const bool supported_thresholds
      = lower_threshold < .5 && upper_threshold > .5 && std::abs(lower_threshold + upper_threshold - 1.) < 1e-12 && std::abs(output_threshold - .5) < 1e-12;
  // reference builds the driver model once for the specified output load, that
  // is the total capacitance of the net. The effective capacitance only feeds
  // the library lookup used for the delay and slew targets, so refitting the
  // driver at each effective-capacitance trial moves the model away from the
  // one reference builds.
  static const bool fit_at_total_c = [] {
    const char* e = std::getenv("ISTA_FIT_AT_TOTAL_C");
    return e != nullptr && std::atoi(e) != 0;
  }();
  auto fit_table = [&](double capacitance) -> std::optional<DriverFit> {
    if (!supported_thresholds)
      return std::nullopt;
    if (fit_at_total_c) capacitance = total_capacitance;
    {
      {
        static const char* fc = std::getenv("ISTA_DUMP_FITCALL");
        if (fc != nullptr) {
          if (FILE* f3 = std::fopen(fc, "a"); f3 != nullptr) {
            std::fprintf(f3, "FC,%d,%.15g,%.15g,%.15g,%.15g\n", static_cast<int>(output_trans_type),
                         capacitance, total_capacitance, input_slew,
                         slew_derate * tableSlew(timing_arc, output_trans_type, input_slew, capacitance));
            std::fclose(f3);
          }
        }
      }
      static const char* fd = std::getenv("ISTA_DUMP_FIT");
      if (fd != nullptr) {
        if (FILE* fp = std::fopen(fd, "a"); fp != nullptr) {
          std::fprintf(fp, "FIT,%s,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%d,%.12g\n", g_dump_out_pin, capacitance,
                       tableDelay(timing_arc, output_trans_type, input_slew, capacitance),
                       tableDelay(timing_arc, output_trans_type, input_slew, 1.01 * capacitance),
                       slew_derate * tableSlew(timing_arc, output_trans_type, input_slew, capacitance),
                       slew_derate, lower_threshold, upper_threshold, static_cast<int>(output_trans_type), input_slew);
          std::fclose(fp);
        }
      }
    }
    auto fitted = fitDriver(capacitance, tableDelay(timing_arc, output_trans_type, input_slew, capacitance),
                            tableDelay(timing_arc, output_trans_type, input_slew, 1.01 * capacitance),
                            slew_derate * tableSlew(timing_arc, output_trans_type, input_slew, capacitance), lower_threshold, upper_threshold);
    // Diagnostic only: zero the ramp start so the Ceff loop has to absorb the
    // distributed-vs-lumped delay difference instead of the fit's free parameter.
    static const bool zero_start = [] {
      const char* e = std::getenv("ISTA_FIT_START_ZERO");
      return e != nullptr && *e != '0';
    }();
    if (zero_start && fitted)
      fitted->start = 0.;
    return fitted;
  };
  double drive_resistance = tableResistance(timing_arc, output_trans_type, input_slew, total_capacitance, voltage_log, slew_derate, delay_resistance);
  if (!(drive_resistance > 0.0 && drive_resistance < 100.0)) {
    drive_resistance = 1.0;
  }
  {
    static const char* rp = std::getenv("ISTA_DUMP_RD");
    if (rp != nullptr) {
      if (FILE* fp = std::fopen(rp, "a"); fp != nullptr) {
        std::fprintf(fp, "RD,%s,%d,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g\n", g_dump_out_pin,
                     static_cast<int>(output_trans_type), input_slew, total_capacitance,
                     delay_at_total_capacitance, delay_at_half_capacitance, delay_resistance,
                     drive_resistance, voltage_log, slew_derate);
        std::fclose(fp);
      }
    }
  }
  // Diagnostic only: pin the drive resistance so the surrounding model can be
  // compared against an external tool without the local fit moving underneath.
  static const double forced_resistance = [] {
    const char* e = std::getenv("ISTA_FORCE_DRIVE_RES");
    return e != nullptr ? std::atof(e) : 0.0;
  }();
  double driver_ramp = tableRamp(timing_arc, output_trans_type, input_slew, drive_resistance, total_capacitance, lower_threshold, upper_threshold, voltage_log,
                                 slew_derate, min_slew_factor);
  if (!(driver_ramp > 0.0 && driver_ramp < 100.0)) {
    driver_ramp = 0.5;
  }

  const double charge_resistance = drive_resistance;
  const double charge_ramp = driver_ramp;
  static const bool disable_driver_fit = [] {
    const char* env = std::getenv("ISTA_DISABLE_DRIVER_FIT");
    return env != nullptr && std::atoi(env) != 0;
  }();
  auto driver_fit = (disable_driver_fit || forced_resistance > 0.) ? std::nullopt : fit_table(total_capacitance);
  const bool joint_fit = driver_fit.has_value();
  if (driver_fit) {
    drive_resistance = driver_fit->resistance;
    driver_ramp = driver_fit->ramp;
  }
  // ISTA_FORCE_REF_PARAMS=<file> pins (rd, delta_t) to an externally supplied
  // driver model so the whole network solve (driver and every sink) runs with
  // the same parameters. t0 is intentionally ignored: a uniform time shift
  // leaves every net delay unchanged. File rows are
  // "pin,analysis,trans,rd,t0,delta_t".
  {
    static const char* ref_env = std::getenv("ISTA_FORCE_REF_PARAMS");
    static const std::unordered_map<std::string, std::array<double, 3>> ref_map = [] {
      std::unordered_map<std::string, std::array<double, 3>> result;
      if (ref_env == nullptr) return result;
      FILE* f = std::fopen(ref_env, "r");
      if (f == nullptr) return result;
      char line[512];
      while (std::fgets(line, sizeof(line), f) != nullptr) {
        std::string text(line);
        std::vector<std::string> part;
        std::size_t start = 0;
        while (true) {
          std::size_t comma = text.find(',', start);
          part.push_back(text.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
          if (comma == std::string::npos) break;
          start = comma + 1;
        }
        if (part.size() < 6) continue;
        result[part[0] + "," + part[1] + "," + part[2]] = {std::atof(part[3].c_str()), std::atof(part[4].c_str()), std::atof(part[5].c_str())};
      }
      std::fclose(f);
      return result;
    }();
    if (!ref_map.empty()) {
      // Max and min share the same three scalars in the exported model, so the
      // analysis dimension collapses; the dump pin is set by the caller.
      const std::string key = std::string(g_dump_out_pin) + ",1," + std::to_string(static_cast<int>(output_trans_type));
      auto it = ref_map.find(key);
      if (it != ref_map.end() && it->second[0] > 0. && it->second[2] > 0.) {
        drive_resistance = it->second[0];
        driver_ramp = it->second[2];
      }
    }
  }
  {
    static const char* dr_dump = std::getenv("ISTA_DUMP_DRIVER");
    if (dr_dump != nullptr) {
      if (FILE* dr_fp = std::fopen(dr_dump, "a"); dr_fp != nullptr) {
        std::fprintf(dr_fp, "DRV,%d,%.12g,%.12g,%.12g,%.12g,%d,%.12g\n", static_cast<int>(output_trans_type), total_capacitance, input_slew,
                     drive_resistance, driver_ramp, joint_fit ? 1 : 0, delay_resistance);
        std::fclose(dr_fp);
      }
    }
  }

  ParasiticArnoldiPoleResidue pole_residue = ArnoldiWaveformSolver::poleResidue(arnoldi_model, drive_resistance);
  if (!pole_residue.get_is_valid()) {
    return std::nullopt;
  }
  double waveform_resistance = drive_resistance;
  auto update_waveform = [&](bool force = false) {
    // Reuse the network response within 5% relative conductance change.
    // G = 1/R: compare to the last actual rebuild, not the last driver fit,
    // so successive small changes cannot accumulate beyond the tolerance.
    if (force || std::abs(waveform_resistance / drive_resistance - 1.) > .05) {
      auto updated = ArnoldiWaveformSolver::poleResidue(arnoldi_model, drive_resistance);
      if (!updated.get_is_valid())
        return false;
      pole_residue = std::move(updated);
      waveform_resistance = drive_resistance;
    }
    return true;
  };

  double effective_capacitance = total_capacitance;
  // The crossing reference must use the same capacitance the driver model was
  // fitted against, otherwise the fitted start no longer places the 50% point
  // at the library delay.
  const auto reference_capacitance = [&]() { return fit_at_total_c ? total_capacitance : effective_capacitance; };
  bool request_adjustment = false;
  auto network_limited = [&]() {
    if (!adjust_drive_resistance || !supported_thresholds)
      return false;
    double midpoint = 0.;
    ArnoldiWaveformSolver::crossingTime(driver_ramp, pole_residue, 0, .5, midpoint);
    double step_voltage = 0.;
    for (std::size_t pole = 0; pole < pole_residue.get_pole_list().size(); ++pole)
      step_voltage += pole_residue.get_residue_list().front()[pole] * std::exp(-pole_residue.get_pole_list()[pole] * midpoint);
    if (!(std::isfinite(midpoint) && std::isfinite(step_voltage) && 2. * std::abs(step_voltage - .5) < .45))
      return false;
    // REF only rescales the driver when "the drive resistor is much less than the
    // impedance of the network to ground" (RC-009). Without that guard the raw
    // step-voltage test fires on ordinary nets whose driver is far stronger than
    // the wire, and the rescaled driver then displaces a correct fit. Compare
    // against the dominant network impedance R_eff = tau_0 / C_total.
    static const double driver_ratio_limit = [] {
      const char* env = std::getenv("ISTA_NETWORK_LIMITED_RATIO");
      return env != nullptr ? std::atof(env) : 0.0;
    }();
    if (driver_ratio_limit > 0.0) {
      const auto& pole_list = pole_residue.get_pole_list();
      if (!pole_list.empty() && pole_list.front() > 0.0 && total_capacitance > 0.0) {
        const double network_impedance = 1.0 / (pole_list.front() * total_capacitance);
        if (std::isfinite(network_impedance) && network_impedance > 0.0
            && drive_resistance > driver_ratio_limit * network_impedance)
          return false;
      }
    }
    return true;
  };
  // REF2022 reverse-engineered convergence (binary strings `ceff_delay_max_error`
  // plus `report_app_var`: rc_ceff_delay_max_error_pct = 8,
  // rc_ceff_delay_min_diff_ps_donotuse = 0.25). reference compares the *same*
  // quantity across iterations; the default scalar check compares the fitted
  // waveform against the table at the current Ceff, which the fit itself
  // satisfies, so it can never move Ceff. ISTA_REF_CEFF_ITER=1 selects the
  // reference criterion instead.
  static const bool ref_ceff_iter_env = [] {
    const char* e = std::getenv("ISTA_REF_CEFF_ITER");
    return e != nullptr && *e != '0';
  }();
  // Gate the reference criterion on the total RC: the 8%/0.25 ps band is far too
  // wide for the picosecond-scale nets that dominate gcd, where the default path
  // is already closer to reference. ISTA_REF_CEFF_RC_MIN_PS sets the cutoff in ps.
  static const double ref_ceff_rc_min_ps = [] {
    const char* e = std::getenv("ISTA_REF_CEFF_RC_MIN_PS");
    return e != nullptr ? std::atof(e) : 0.0;
  }();
  const bool ref_ceff_iter = ref_ceff_iter_env && total_capacitance > 0.0
                            && [&] {
                                 const auto& poles = pole_residue.get_pole_list();
                                 if (poles.empty() || !(poles.front() > 0.0))
                                   return true;
                                 const double rc_ps = 1.0 / poles.front() * 1e3;
                                 return rc_ps >= ref_ceff_rc_min_ps;
                               }();
  auto fit_load = [&](bool match_crossing) {
    double lower_capacitance = 0.;
    double upper_capacitance = std::numeric_limits<double>::infinity();
    double previous_residual = std::numeric_limits<double>::infinity();
    bool converged = false;
    // Fit the distributed source crossing for both ordinary and adjusted
    // two-load drivers. Charge matching remains the fallback for table data
    // that cannot identify a consistent two-load driver waveform.
    for (int32_t iter = 0; iter < 64; ++iter) {
      // Adjustment can be needed at total C or during a later Ceff trial.
      // Testing only the final waveform can miss the branch altogether.
      static const bool keep_ceff_on_limited = [] {
        const char* env = std::getenv("ISTA_KEEP_CEFF_ON_LIMITED");
        return env != nullptr && std::atoi(env) != 0;
      }();
      if (!match_crossing && network_limited()) {
        request_adjustment = true;
        if (!keep_ceff_on_limited)
          return false;
      }
      double capacitance = 0.;
      if (match_crossing || driver_fit) {
        double source_time = 0.;
        ArnoldiWaveformSolver::crossingTime(driver_ramp, pole_residue, 0, .5, source_time);
        // reference compares the distributed source crossing against the same
        // network's dominant time constant. Using the lumped rd*Cref instead
        // leaves a systematic gap (2.68 ps on s13207 fanout_net_3 versus a
        // 1.48 ps acceptance band). ISTA_REF_FIRST_POLE=1 makes the reference
        // use the first Arnoldi pole so the two paths describe one response.
        static const bool ref_first_pole = [] {
          const char* e = std::getenv("ISTA_REF_FIRST_POLE");
          return e != nullptr && *e != '0';
        }();
        const auto& ref_poles = pole_residue.get_pole_list();
        const bool use_pole = ref_first_pole && !ref_poles.empty() && ref_poles.front() > 0.;
        const double reference = ArnoldiWaveformSolver::rampTime(
            use_pole ? ref_poles.front() : 1. / (drive_resistance * reference_capacitance()), driver_ramp, .5);
        const double nominal = tableDelay(timing_arc, output_trans_type, input_slew, effective_capacitance);
        const double actual = source_time + (driver_fit ? driver_fit->start : nominal - reference);
        // Accept the fitted model by its delay error before another Ceff update
        // can distort it. Keep this same waveform intact for every receiver.
        // Internal time is ns: 1% of 25 ps supplies a 0.25 ps absolute floor.
        {
          static const char* dbg = std::getenv("ISTA_DUMP_CEFF_DEBUG");
          if (dbg != nullptr) {
            if (FILE* f = std::fopen(dbg, "a"); f != nullptr) {
              std::fprintf(f, "DBG,%s,iter=%d,source=%.12g,nominal=%.12g,actual=%.12g,effC=%.12g,totC=%.12g,match=%d,"
                              "fitstart=%.12g,reference=%.12g,ramp=%.12g,rd=%.12g\n",
                           g_dump_out_pin, iter, source_time, nominal, actual, effective_capacitance, total_capacitance,
                           match_crossing ? 1 : 0, driver_fit ? driver_fit->start : 0., reference, driver_ramp,
                           drive_resistance);
              std::fclose(f);
            }
          }
        }
        // ISTA_CEFF_ALWAYS_TOTAL keeps the lookup load at the net total
        // capacitance, which is what reference reports: on s13207 fanout_net_3
        // its Total capacitance and Effective capacitance are both 0.078191 pF.
        // When the 1% acceptance band below rejects the iter=0 model, the loop
        // otherwise takes exactly one crossingCapacitance step (0.94-0.97 x
        // total) and stops, so effC never really iterates.
        static const bool always_total = [] {
          const char* e = std::getenv("ISTA_CEFF_ALWAYS_TOTAL");
          return e != nullptr && *e != '0';
        }();
        if (always_total) {
          converged = true;
          break;
        }
        // The default 1% acceptance band is far tighter than reference's own
        // rc_ceff_delay_max_error_pct (8), so the iter=0 model is rejected on
        // large nets where reference accepts it. ISTA_CEFF_ACCEPT_PCT sweeps the
        // band; the default 1.0 keeps every existing result unchanged.
        static const double accept_pct = [] {
          const char* e = std::getenv("ISTA_CEFF_ACCEPT_PCT");
          return e != nullptr ? std::atof(e) : 1.0;
        }();
        if (!ref_ceff_iter && accept_pct > 0.
            && std::abs(actual - nominal) < .01 * accept_pct * std::max(std::abs(actual), .025)) {
          converged = true;
          break;
        }
        // reference's `report_delay_calculation` prints TotalC in the "Effective
        // capacitance" fields, so the Ceff it actually uses can only be recovered
        // from `Cell delay` (see FINDINGS §266). For gcd `fixio_net_0` the delay
        // corresponds to 9.4996 fF against a 9.5255 fF total, i.e. a 0.27% reduction.
        // The charge-based definition below is the classical Ceff; the crossing fit
        // gives 0.916 x total and is far off, so allow selecting the charge model.
        static const bool charge_ceff = [] {
          const char* e = std::getenv("ISTA_USE_CHARGE_CEFF");
          return e != nullptr && *e != '0';
        }();
        const auto matched = ArnoldiWaveformSolver::crossingCapacitance(driver_ramp, drive_resistance, total_capacitance, source_time, .5);
        if (!matched)
          return false;
        capacitance = *matched;
        if (charge_ceff) {
          const double charge = ArnoldiWaveformSolver::effectiveCapacitance(driver_ramp, drive_resistance, pole_residue, driver_ramp);
          if (charge > 0.0 && std::isfinite(charge))
            capacitance = charge;
        }
      } else {
        capacitance = ArnoldiWaveformSolver::effectiveCapacitance(driver_ramp, drive_resistance, pole_residue, driver_ramp);
      }
      if (!std::isfinite(capacitance) || capacitance <= 0.0) {
        return false;
      }
      {
        // Diagnostic only: record the raw Ceff update so a clamped or one-shot
        // step is distinguishable from a converged iteration. ISTA_DUMP_CEFF_STEP
        // gates the log.
        static const char* cs = std::getenv("ISTA_DUMP_CEFF_STEP");
        if (cs != nullptr) {
          if (FILE* fp = std::fopen(cs, "a"); fp != nullptr) {
            std::fprintf(fp, "CS,%s,%d,%.12g,%.12g,%.12g,%d\n", g_dump_out_pin, iter, capacitance,
                         effective_capacitance, total_capacitance, match_crossing ? 1 : 0);
            std::fclose(fp);
          }
        }
      }
      const auto next_fit = !match_crossing && driver_fit ? fit_table(capacitance) : std::nullopt;
      if (!match_crossing && driver_fit && !next_fit)
        return false;
      const double ramp = next_fit ? next_fit->ramp
                                   : tableRamp(timing_arc, output_trans_type, input_slew, drive_resistance, capacitance, lower_threshold, upper_threshold,
                                               voltage_log, slew_derate, min_slew_factor);
      if (!std::isfinite(ramp) || ramp <= 0.0)
        return false;
      if (ref_ceff_iter && !match_crossing) {
        // reference stops when the delay stops moving: |delay_new - delay_old| <
        // max(8% * delay_new, 0.25 ps). The 8% band is wide, so the loop normally
        // ends after one Ceff update.
        const double delay_new = tableDelay(timing_arc, output_trans_type, input_slew, capacitance);
        const double delay_old = tableDelay(timing_arc, output_trans_type, input_slew, effective_capacitance);
        // `rc_ceff_delay_min_diff_ps_donotuse` carries a `donotuse` suffix, so the
        // absolute floor may be inert in reference. ISTA_REF_CEFF_NO_FLOOR=1 drops it.
        static const bool no_floor = [] {
          const char* e = std::getenv("ISTA_REF_CEFF_NO_FLOOR");
          return e != nullptr && *e != '0';
        }();
        const double tolerance = no_floor ? .08 * std::abs(delay_new) : std::max(.08 * std::abs(delay_new), .25e-3);
        converged = std::abs(delay_new - delay_old) < tolerance;
      } else {
        converged = !match_crossing && std::abs(capacitance - effective_capacitance) <= 1e-7 * std::max(capacitance, effective_capacitance)
                    && std::abs(ramp - driver_ramp) <= 1e-7 * std::max(ramp, driver_ramp);
      }
      {
        static const char* cv_dump = std::getenv("ISTA_DUMP_CEFF_ITER");
        if (cv_dump != nullptr) {
          if (FILE* cv_fp = std::fopen(cv_dump, "a"); cv_fp != nullptr) {
            std::fprintf(cv_fp, "CV,%s,%d,%d,%.12g,%.12g,%.12g\n", g_dump_out_pin, iter, converged ? 1 : 0, capacitance,
                         effective_capacitance, ramp);
            std::fclose(cv_fp);
          }
        }
      }
      if (converged) {
        effective_capacitance = capacitance;
        driver_ramp = ramp;
        if (next_fit) {
          driver_fit = next_fit;
          drive_resistance = next_fit->resistance;
          const double previous_resistance = waveform_resistance;
          if (!update_waveform())
            return false;
          if (waveform_resistance != previous_resistance) {
            // A changed response invalidates both the crossing and its bracket.
            lower_capacitance = 0.;
            upper_capacitance = previous_residual = std::numeric_limits<double>::infinity();
            converged = false;
            continue;
          }
        }
        break;
      }
      // Solve matched(C) - C = 0. Nonlinear tables can make plain fixed-point
      // iteration oscillate around a valid solution. Retain the residual-sign
      // bracket and switch to bisection when iteration stops making progress.
      // A joint fit may rebuild the response when C changes: keep contracting
      // steps instead of jumping to a different network model. Fixed-R charge
      // and adjusted fits retain periodic bisection, including slow contraction.
      if (capacitance > effective_capacitance)
        lower_capacitance = effective_capacitance;
      else
        upper_capacitance = effective_capacitance;
      const double residual = std::abs(capacitance - effective_capacitance);
      {
        static const char* it_dump = std::getenv("ISTA_DUMP_CEFF_ITER");
        if (it_dump != nullptr && iter >= 0) {
          if (FILE* it_fp = std::fopen(it_dump, "a"); it_fp != nullptr) {
            std::fprintf(it_fp, "IT,%s,%d,%.12g,%.12g,%.12g,%.12g,%.12g\n", g_dump_out_pin, iter, capacitance,
                         effective_capacitance, driver_ramp, total_capacitance, residual);
            std::fclose(it_fp);
          }
        }
      }
      if (std::isfinite(upper_capacitance)
          && ((iter >= 8 && (!driver_fit || residual >= previous_residual)) || capacitance <= lower_capacitance || capacitance >= upper_capacitance)) {
        effective_capacitance = lower_capacitance + .5 * (upper_capacitance - lower_capacitance);
      } else {
        effective_capacitance = capacitance;
      }
      previous_residual = residual;
      if (!match_crossing && driver_fit) {
        driver_fit = effective_capacitance == capacitance ? next_fit : fit_table(effective_capacitance);
        if (!driver_fit)
          return false;
        drive_resistance = driver_fit->resistance;
        driver_ramp = driver_fit->ramp;
        const double previous_resistance = waveform_resistance;
        if (!update_waveform())
          return false;
        if (waveform_resistance != previous_resistance) {
          lower_capacitance = 0.;
          upper_capacitance = previous_residual = std::numeric_limits<double>::infinity();
        }
      } else {
        driver_ramp = tableRamp(timing_arc, output_trans_type, input_slew, drive_resistance, effective_capacitance, lower_threshold, upper_threshold,
                                voltage_log, slew_derate, min_slew_factor);
        if (!(driver_ramp > 0.) || !std::isfinite(driver_ramp))
          return false;
      }
    }
    return converged;
  };
  bool converged = fit_load(false);
  if (!converged && joint_fit && !request_adjustment) {
    // Shielding may enter a locally inconsistent LUT segment even when the
    // initial two-load fit was feasible. Restart the charge model from its
    // original state; never reuse a partial joint fit as a valid waveform.
    driver_fit.reset();
    drive_resistance = charge_resistance;
    driver_ramp = charge_ramp;
    effective_capacitance = total_capacitance;
    if (!update_waveform(true))
      return std::nullopt;
    converged = fit_load(false);
  }
  bool resistance_adjusted = false;
  static const bool keep_ceff_on_limited2 = [] {
    const char* env = std::getenv("ISTA_KEEP_CEFF_ON_LIMITED");
    return env != nullptr && std::atoi(env) != 0;
  }();
  if (request_adjustment && !keep_ceff_on_limited2) {
    const double transition = slew_derate * tableSlew(timing_arc, output_trans_type, input_slew, total_capacitance);
    const double adjusted
        = (upper_threshold - .5) * transition / ((upper_threshold - lower_threshold) * std::log(.5 / (1. - upper_threshold)) * total_capacitance);
    if (!(adjusted > 0.) || !std::isfinite(adjusted))
      return std::nullopt;
    driver_fit.reset();
    drive_resistance = adjusted;
    effective_capacitance = total_capacitance;
    driver_ramp = tableRamp(timing_arc, output_trans_type, input_slew, drive_resistance, total_capacitance, lower_threshold, upper_threshold, voltage_log,
                            slew_derate, min_slew_factor);
    if (!update_waveform(true) || !(driver_ramp > 0.) || !std::isfinite(driver_ramp))
      return std::nullopt;
    // The adjusted driver matches its lumped table delay to the distributed
    // source crossing; receiver delay and slew use this same fitted waveform.
    converged = fit_load(true);
    resistance_adjusted = converged;
  }
  if (!converged)
    return std::nullopt;

  {
    static const char* fd = std::getenv("ISTA_DUMP_FINAL");
    if (fd != nullptr) {
      if (FILE* fp = std::fopen(fd, "a"); fp != nullptr) {
        std::fprintf(fp, "FIN,%s,%d,%.12g,%.12g,%.12g,%.12g,%.12g,%d,%.12g\n", g_dump_out_pin,
                     static_cast<int>(output_trans_type), drive_resistance, driver_ramp,
                     driver_fit ? driver_fit->start : 0., effective_capacitance, total_capacitance,
                     driver_fit ? 1 : 0, input_slew);
        std::fclose(fp);
      }
    }
  }
  double table_delay = tableDelay(timing_arc, output_trans_type, input_slew, effective_capacitance);
  double reference_time = ArnoldiWaveformSolver::rampTime(1.0 / (drive_resistance * reference_capacitance()), driver_ramp, 0.5);
  std::vector<double> delay_list;
  std::vector<double> slew_list;
  for (std::size_t term_idx = 0; term_idx < arnoldi_model.get_term_node_list().size(); term_idx++) {
    double upper_time = 0.0;
    double middle_time = 0.0;
    double lower_time = 0.0;
    static const double ramp_scale = [] {
      const char* e = std::getenv("ISTA_DRIVER_RAMP_SCALE");
      return e != nullptr ? std::atof(e) : 1.0;
    }();
    static const double ramp_scale_rise = [] {
      const char* e = std::getenv("ISTA_DRIVER_RAMP_SCALE_RISE");
      return e != nullptr ? std::atof(e) : 1.0;
    }();
    static const double ramp_scale_fall = [] {
      const char* e = std::getenv("ISTA_DRIVER_RAMP_SCALE_FALL");
      return e != nullptr ? std::atof(e) : 1.0;
    }();
    const double dir_scale = (output_trans_type == TransType::kRise) ? ramp_scale_rise : ramp_scale_fall;
    const double total_scale = ramp_scale * dir_scale;
    const double used_ramp = (std::abs(total_scale - 1.0) > 1e-12 && driver_ramp > 0.0) ? driver_ramp * total_scale : driver_ramp;
    ArnoldiWaveformSolver::crossingTime(used_ramp, pole_residue, term_idx, upper_threshold, upper_time, 0.5, middle_time, lower_threshold, lower_time);
    {
      static const char* tr = std::getenv("ISTA_DUMP_TERM_RESID");
      if (tr != nullptr) {
        if (FILE* rf = std::fopen(tr, "a"); rf != nullptr) {
          std::fprintf(rf, "R,%d,%zu,%zu,poles=", static_cast<int>(output_trans_type),
                       term_idx, pole_residue.get_pole_list().size());
          for (double pv : pole_residue.get_pole_list()) std::fprintf(rf, "%.9g,", pv);
          std::fprintf(rf, "resid=");
          for (double rv : pole_residue.get_residue_list()[term_idx]) std::fprintf(rf, "%.12g,", rv);
          std::fprintf(rf, "\n");
          std::fclose(rf);
        }
      }
    }
    {
      static const char* dd = std::getenv("ISTA_DUMP_DELAYCOMP");
      if (dd != nullptr) {
        if (FILE* dp = std::fopen(dd, "a"); dp != nullptr) {
          const auto& tpl = arnoldi_model.get_term_pin_list();
          const char* tpn = term_idx < tpl.size() ? tpl[term_idx].c_str() : "";
          std::fprintf(dp, "%s,%d,%zu,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g,%d,%.15g,%.15g\n", tpn, static_cast<int>(output_trans_type), term_idx,
                       middle_time, lower_time, upper_time,
                       driver_fit ? driver_fit->start : (table_delay - reference_time), table_delay, reference_time, driver_fit ? 1 : 0,
                       input_slew, effective_capacitance);
          std::fclose(dp);
        }
      }
    }
    static const bool waveform_only = [] {
      const char* env = std::getenv("ISTA_DELAY_WAVEFORM_ONLY");
      return env != nullptr && std::atoi(env) != 0;
    }();
    delay_list.push_back(waveform_only ? middle_time : middle_time + (driver_fit ? driver_fit->start : table_delay - reference_time));
    {
      static const char* wd = std::getenv("ISTA_DUMP_ARD_WAVE");
      if (wd != nullptr && term_idx == 0 && input_slew > 1e-6) {
        if (FILE* wf = std::fopen(wd, "a"); wf != nullptr) {
          const auto& pl = pole_residue.get_pole_list();
          const auto& rl = pole_residue.get_residue_list()[term_idx];
          auto vat = [&](double tt) {
            double vv = 0.0;
            for (std::size_t pi = 0; pi < pl.size(); ++pi) {
              const double pv = pl[pi], rv = pi < rl.size() ? rl[pi] : 0.0;
              if (std::isinf(pv)) { vv += rv * std::max(0., 1. - tt / driver_ramp); continue; }
              const double ref2 = pv * tt, pr2 = pv * driver_ramp;
              vv += rv * ((tt < driver_ramp) ? (1.0 - tt / driver_ramp - std::expm1(-ref2) / pr2)
                                             : (-std::exp(pr2 - ref2) * std::expm1(-pr2) / pr2));
            }
            return vv;
          };
          auto bisect = [&](double lvl) {
            double lo_t = 0.0, hi_t = driver_ramp;
            while (vat(hi_t) > lvl && hi_t < 1e3) hi_t *= 2.0;
            for (int it = 0; it < 200; ++it) {
              const double mt = 0.5 * (lo_t + hi_t);
              if (vat(mt) > lvl) lo_t = mt; else hi_t = mt;
            }
            return 0.5 * (lo_t + hi_t);
          };
          const double b70 = bisect(0.7), b50 = bisect(0.5), b30 = bisect(0.3);
          std::fprintf(wf, "ARD,%d,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g,%.15g,np=%zu\n",
                       static_cast<int>(output_trans_type), input_slew, driver_ramp, effective_capacitance,
                       table_delay, reference_time, upper_time, lower_time, b70, b30,
                       (lower_time - upper_time) / slew_derate, (b30 - b70) / slew_derate, pl.size());
          std::fclose(wf);
        }
      }
    }
    slew_list.push_back((lower_time - upper_time) / slew_derate);
  }
  if (delay_list.empty() || slew_list.empty()) {
    return std::nullopt;
  }

  {
    static const char* rr = std::getenv("ISTA_DUMP_RESISTANCE");
    if (rr != nullptr) {
      if (FILE* rf = std::fopen(rr, "a"); rf != nullptr) {
        std::fprintf(rf, "%d,%d,%.15g,%.15g,%.15g,%.15g,%.15g\n", static_cast<int>(output_trans_type),
                     resistance_adjusted ? 1 : 0, drive_resistance, effective_capacitance, total_capacitance, driver_ramp, table_delay);
        std::fclose(rf);
      }
    }
  }
  return ArnoldiDelayResult{effective_capacitance, drive_resistance,        std::move(delay_list), std::move(slew_list),
                            driver_ramp,           std::move(pole_residue), resistance_adjusted};
}

double ArnoldiDelayCalculator::sourceResistance(TimingArc& timing_arc, TransType output_trans_type, double input_slew,
                                                 double total_capacitance) const
{
  if (!(total_capacitance > 0.))
    return 0.;
  // Mirror the same two inputs calculate() derives before tableResistance so the
  // probe returns the identical rd for the identical capacitance.
  double slew_derate = 1.0;
  double lower_threshold = 0.1;
  double upper_threshold = 0.9;
  double voltage_log = 0.0;
  double min_slew_factor = 0.0;
  double x1 = 0.0;
  double y1 = 0.0;
  ArnoldiWaveformSolver::thresholds(timing_arc, output_trans_type, slew_derate, lower_threshold, upper_threshold, voltage_log, min_slew_factor, x1, y1);
  double delay_resistance = 0.0;
  if (total_capacitance > ista::kTimingTolerance) {
    const double delay_at_total_capacitance = tableDelay(timing_arc, output_trans_type, input_slew, total_capacitance);
    const double delay_at_half_capacitance = tableDelay(timing_arc, output_trans_type, input_slew, 0.5 * total_capacitance);
    delay_resistance = (delay_at_total_capacitance - delay_at_half_capacitance) / (0.5 * total_capacitance);
  }
  if (!(delay_resistance > 0.0))
    delay_resistance = 1.0;
  double drive_resistance = tableResistance(timing_arc, output_trans_type, input_slew, total_capacitance, voltage_log, slew_derate, delay_resistance);
  if (!(drive_resistance > 0.0 && drive_resistance < 100.0))
    drive_resistance = 1.0;
  return drive_resistance;
}

double ArnoldiDelayCalculator::tableResistance(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double total_capacitance,
                                               double voltage_log, double slew_derate, double delay_resistance)
{
  if (!(total_capacitance > 0.0 && voltage_log > 0.0)) {
    return 0.0;
  }
  double table_slew = tableSlew(timing_arc, output_trans_type, input_slew, total_capacitance);
  double transition_time = slew_derate * table_slew;
  double drive_resistance = transition_time / (voltage_log * total_capacitance);
  // reference does not clamp its drive resistance to the chord resistance taken
  // from the table delay at C and C/2; on gcd that chord is 1.3% below the
  // measured rise value and 6.8% above the measured fall value, so the clamp
  // pulls both quadrants onto the wrong number. ISTA_NO_RD_CLAMP=1 disables it.
  static const bool no_rd_clamp = [] {
    const char* e = std::getenv("ISTA_NO_RD_CLAMP");
    return e != nullptr && *e != '0';
  }();
  if (!no_rd_clamp && delay_resistance > 0.0 && drive_resistance > delay_resistance) {
    drive_resistance = delay_resistance;
  }
  return drive_resistance;
}

double ArnoldiDelayCalculator::tableRamp(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double drive_resistance, double capacitance,
                                         double lower_threshold, double upper_threshold, double voltage_log, double slew_derate, double min_slew_factor)
{
  if (!(drive_resistance > 0.0 && capacitance > 0.0 && voltage_log > 0.0)) {
    return 0.0;
  }
  double table_slew = tableSlew(timing_arc, output_trans_type, input_slew, capacitance);
  static const double ramp_derate = [] {
    const char* env = std::getenv("ISTA_RAMP_DERATE");
    return env != nullptr ? std::atof(env) : -1.0;
  }();
  double transition_time = (ramp_derate >= 0.0 ? ramp_derate : slew_derate) * table_slew;
  double min_slew = drive_resistance * capacitance * min_slew_factor;
  if (voltage_log * drive_resistance * capacitance >= transition_time) {
    return min_slew;
  }
  double ramp = min_slew + 0.3 * transition_time;
  ArnoldiWaveformSolver::solveRamp(1.0 / (drive_resistance * capacitance), transition_time, lower_threshold, upper_threshold, ramp);
  static const char* rr_dump = std::getenv("ISTA_DUMP_RAMP");
  if (rr_dump != nullptr) {
    if (FILE* rp = std::fopen(rr_dump, "a"); rp != nullptr) {
      std::fprintf(rp, "table_slew=%.15g transition=%.15g min_slew=%.15g ramp=%.15g R=%.15g C=%.15g early=%d\n", table_slew, transition_time,
                   min_slew, ramp, drive_resistance, capacitance,
                   voltage_log * drive_resistance * capacitance >= transition_time ? 1 : 0);
      std::fclose(rp);
    }
  }
  return ramp;
}

double ArnoldiDelayCalculator::tableDelay(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load)
{
  if (timing_arc.get_delay_table_map().count(output_trans_type) == 0) {
    return 0.0;
  }
  double converted_output_load = output_load * timing_arc.get_cap_unit_scale();
  double delay = timing_arc.get_delay_table_map()[output_trans_type].findValue(input_slew * timing_arc.get_time_unit_scale(), converted_output_load);
  return delay / timing_arc.get_time_unit_scale();
}

double ArnoldiDelayCalculator::tableSlew(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load)
{
  if (timing_arc.get_slew_table_map().count(output_trans_type) == 0) {
    return input_slew;
  }
  double converted_output_load = output_load * timing_arc.get_cap_unit_scale();
  double slew = timing_arc.get_slew_table_map()[output_trans_type].findValue(input_slew * timing_arc.get_time_unit_scale(), converted_output_load);
  return slew / timing_arc.get_time_unit_scale();
}

}  // namespace ista

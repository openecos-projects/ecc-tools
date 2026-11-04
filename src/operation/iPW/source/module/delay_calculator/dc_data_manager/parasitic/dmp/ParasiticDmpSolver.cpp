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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "ParasiticDmpSolver.hpp"

#include "Arc.hpp"
#include "DataManager.hpp"
#include "Database.hpp"
#include "DelayCache.hpp"
#include "Instance.hpp"
#include "Net.hpp"
#include "OutputLoadEstimator.hpp"
#include "ParasiticGraph.hpp"
#include "ParasiticNet.hpp"
#include "ParasiticNodeName.hpp"
#include "SharedDriverResultCache.hpp"
#include "ParasiticThresholds.hpp"
#include "Pin.hpp"
#include "PWHeader.hpp"
#include "RcTree.hpp"
#include "TimingArc.hpp"

namespace ipw {

ParasiticDelayResult& ParasiticDmpSolver::getTimingResult(
    std::string& output_pin,
    TimingArc& timing_arc,
    AnalysisType analysis_type,
    TransType output_trans_type,
    double input_slew,
    double output_load
)
{
  return PWDCACHE.dmpTimingResult({output_pin, &timing_arc, analysis_type, output_trans_type, input_slew}, [&] {
    return calcParasiticDmpTimingResult(
        output_pin,
        timing_arc,
        analysis_type,
        output_trans_type,
        input_slew,
        output_load
    );
  });
}

ParasiticDelayResult ParasiticDmpSolver::calcParasiticDmpTimingResult(
    std::string& output_pin,
    TimingArc& timing_arc,
    AnalysisType analysis_type,
    TransType output_trans_type,
    double input_slew,
    double output_load
)
{
  ParasiticDelayResult timing_result;
  Database& database = PWDM.getDatabase();
  if (database.get_pin_map().count(output_pin) == 0) {
    return timing_result;
  }
  Pin& output_pin_data = database.get_pin_map()[output_pin];
  if (output_pin_data.get_net_name().empty()
      || database.get_parasitic_net_map().count(output_pin_data.get_net_name()) == 0) {
    return timing_result;
  }

  ParasiticNet& parasitic_net = database.get_parasitic_net_map()[output_pin_data.get_net_name()];
  std::string source_node_name = getParasiticNodeName(parasitic_net, output_pin);
  if (source_node_name.empty()) {
    return timing_result;
  }

  ParasiticDmpModel& dmp_model
      = getParasiticDmpModel(parasitic_net, getParasiticGraph(parasitic_net).find(source_node_name),
                             analysis_type, output_trans_type);
  if (!dmp_model.get_is_valid()) {
    return timing_result;
  }

  double gate_delay = 0.0;
  double driver_slew = 0.0;
  double effective_capacitance = output_load;
  DmpSolverModel dmp_solver_model;
  if (!calcParasiticDmpCeff(
          dmp_solver_model,
          timing_arc,
          output_trans_type,
          input_slew,
          dmp_model,
          gate_delay,
          driver_slew,
          effective_capacitance
      )) {
    return timing_result;
  }

  timing_result.set_is_valid(true);
  timing_result.set_gate_delay(gate_delay);
  timing_result.set_driver_slew(driver_slew);
  timing_result.get_load_slew_map()[source_node_name] = driver_slew;
  timing_result.get_load_slew_map()[output_pin] = driver_slew;

  const ParasiticGraph& graph = getParasiticGraph(parasitic_net);
  for (std::pair<const std::string, ParasiticDmpLoadModel>& load_model_pair : dmp_model.get_load_model_map()) {
    std::string load_node_name = load_model_pair.first;
    const uint32_t load_node_id = graph.find(load_node_name);
    std::string load_pin_name = load_node_id == kInvalidParasiticNode ? load_node_name : graph.pin_name_of(load_node_id);
    double wire_delay = 0.0;
    double load_slew = driver_slew;
    if (!calcParasiticDmpLoadDelay(
            load_model_pair.second,
            timing_arc,
            output_trans_type,
            driver_slew,
            wire_delay,
            load_slew
        )) {
      continue;
    }
    adjustParasiticLoadThreshold(timing_arc, load_pin_name, output_trans_type, wire_delay, load_slew);
    timing_result.get_load_slew_map()[load_node_name] = load_slew;
    timing_result.get_load_slew_map()[load_pin_name] = load_slew;
  }
  cacheParasiticDmpDriverResult(output_pin, analysis_type, output_trans_type, driver_slew, timing_result);
  return timing_result;
}

bool ParasiticDmpSolver::calcParasiticDmpCeff(
    DmpSolverModel& dmp_solver_model,
    TimingArc& timing_arc,
    TransType trans_type,
    double input_slew,
    ParasiticDmpModel& dmp_model,
    double& gate_delay,
    double& driver_slew,
    double& effective_capacitance
)
{
  if (!initParasiticDmpCeff(dmp_solver_model, timing_arc, trans_type, input_slew, dmp_model)) {
    return false;
  }

  dmp_solver_model.get_driver_resistance() = calcGateResistance(dmp_solver_model);
  if (!std::isfinite(dmp_solver_model.get_driver_resistance()) || dmp_solver_model.get_driver_resistance() < 0.0) {
    return false;
  }
  if (dmp_solver_model.get_driver_resistance() < kCapacitiveDriverResistance
      || dmp_solver_model.get_pi_resistance() < dmp_solver_model.get_driver_resistance() * 1E-3
      || dmp_solver_model.get_load_capacitance() == 0.0
      || dmp_solver_model.get_load_capacitance() < dmp_solver_model.get_driver_capacitance() * 1E-3
      || dmp_solver_model.get_pi_resistance() == 0.0) {
    return calcCap(dmp_solver_model, gate_delay, driver_slew, effective_capacitance);
  }
  if (dmp_solver_model.get_driver_capacitance() < dmp_solver_model.get_load_capacitance() * 1E-3) {
    return calcZeroC2(dmp_solver_model, gate_delay, driver_slew, effective_capacitance);
  }
  return calcPi(dmp_solver_model, gate_delay, driver_slew, effective_capacitance);
}

bool ParasiticDmpSolver::initParasiticDmpCeff(
    DmpSolverModel& dmp_solver_model,
    TimingArc& timing_arc,
    TransType trans_type,
    double input_slew,
    ParasiticDmpModel& dmp_model
)
{
  if (!dmp_model.get_is_valid() || timing_arc.get_delay_table_map().count(trans_type) == 0
      || timing_arc.get_slew_table_map().count(trans_type) == 0) {
    return false;
  }

  dmp_solver_model.get_timing_arc() = &timing_arc;
  dmp_solver_model.get_trans_type() = trans_type;
  dmp_solver_model.get_input_slew() = input_slew;
  dmp_solver_model.get_driver_capacitance() = dmp_model.get_driver_capacitance();
  dmp_solver_model.get_pi_resistance() = dmp_model.get_pi_resistance();
  dmp_solver_model.get_load_capacitance() = dmp_model.get_load_capacitance();
  dmp_solver_model.get_threshold() = getNormalizedThreshold(
      trans_type == TransType::kFall ? timing_arc.get_output_threshold_pct_fall()
                                     : timing_arc.get_output_threshold_pct_rise()
  );
  dmp_solver_model.get_lower_threshold() = getNormalizedThreshold(
      trans_type == TransType::kFall ? timing_arc.get_slew_lower_threshold_pct_fall()
                                     : timing_arc.get_slew_lower_threshold_pct_rise()
  );
  dmp_solver_model.get_upper_threshold() = getNormalizedThreshold(
      trans_type == TransType::kFall ? timing_arc.get_slew_upper_threshold_pct_fall()
                                     : timing_arc.get_slew_upper_threshold_pct_rise()
  );
  dmp_solver_model.get_slew_derate() = timing_arc.get_slew_derate();
  return std::isfinite(dmp_solver_model.get_input_slew()) && dmp_solver_model.get_driver_capacitance() >= 0.0
         && dmp_solver_model.get_pi_resistance() >= 0.0 && dmp_solver_model.get_load_capacitance() >= 0.0
         && dmp_solver_model.get_threshold() > 0.0 && dmp_solver_model.get_threshold() < 1.0
         && dmp_solver_model.get_lower_threshold() >= 0.0 && dmp_solver_model.get_upper_threshold() <= 1.0
         && dmp_solver_model.get_upper_threshold() > dmp_solver_model.get_lower_threshold()
         && dmp_solver_model.get_slew_derate() > 0.0;
}

double ParasiticDmpSolver::calcGateResistance(DmpSolverModel& dmp_solver_model)
{
  double capacitance1 = dmp_solver_model.get_driver_capacitance() + dmp_solver_model.get_load_capacitance();
  double capacitance2 = capacitance1 + kGateResistanceCapacitanceStep;
  double delay1 = 0.0;
  double slew1 = 0.0;
  double delay2 = 0.0;
  double slew2 = 0.0;
  if (!getGateDelaySlew(dmp_solver_model, capacitance1, delay1, slew1)
      || !getGateDelaySlew(dmp_solver_model, capacitance2, delay2, slew2)) {
    return 0.0;
  }
  return -std::log(dmp_solver_model.get_threshold()) * std::abs(delay1 - delay2) / (capacitance2 - capacitance1);
}

bool ParasiticDmpSolver::getGateDelaySlew(
    DmpSolverModel& dmp_solver_model,
    double capacitance,
    double& gate_delay,
    double& gate_slew
)
{
  if (dmp_solver_model.get_timing_arc() == nullptr
      || dmp_solver_model.get_timing_arc()->get_delay_table_map().count(dmp_solver_model.get_trans_type()) == 0
      || dmp_solver_model.get_timing_arc()->get_slew_table_map().count(dmp_solver_model.get_trans_type()) == 0) {
    return false;
  }
  double converted_slew = dmp_solver_model.get_input_slew() * dmp_solver_model.get_timing_arc()->get_time_unit_scale();
  double converted_capacitance = capacitance * dmp_solver_model.get_timing_arc()->get_cap_unit_scale();
  gate_delay = dmp_solver_model.get_timing_arc()->get_delay_table_map()[dmp_solver_model.get_trans_type()].findValue(
                   converted_slew,
                   converted_capacitance
               )
               / dmp_solver_model.get_timing_arc()->get_time_unit_scale();
  gate_slew = dmp_solver_model.get_timing_arc()->get_slew_table_map()[dmp_solver_model.get_trans_type()].findValue(
                  converted_slew,
                  converted_capacitance
              )
              / dmp_solver_model.get_timing_arc()->get_time_unit_scale();
  return std::isfinite(gate_delay) && std::isfinite(gate_slew) && gate_slew >= 0.0;
}

bool ParasiticDmpSolver::calcCap(
    DmpSolverModel& dmp_solver_model,
    double& gate_delay,
    double& driver_slew,
    double& effective_capacitance
)
{
  effective_capacitance = dmp_solver_model.get_driver_capacitance() + dmp_solver_model.get_load_capacitance();
  return getGateDelaySlew(dmp_solver_model, effective_capacitance, gate_delay, driver_slew);
}

bool ParasiticDmpSolver::calcPi(
    DmpSolverModel& dmp_solver_model,
    double& gate_delay,
    double& driver_slew,
    double& effective_capacitance
)
{
  if (!initPi(dmp_solver_model)) {
    return calcCap(dmp_solver_model, gate_delay, driver_slew, effective_capacitance);
  }

  if (!findDriverParams(
          dmp_solver_model,
          dmp_solver_model.get_driver_capacitance() + dmp_solver_model.get_load_capacitance()
      )
      && !findDriverParams(dmp_solver_model, dmp_solver_model.get_driver_capacitance())) {
    return calcCap(dmp_solver_model, gate_delay, driver_slew, effective_capacitance);
  }

  effective_capacitance = dmp_solver_model.get_parameter_list()[kEffectiveCapacitanceIndex];
  double table_slew = 0.0;
  if (!getGateDelaySlew(dmp_solver_model, effective_capacitance, gate_delay, table_slew)) {
    return false;
  }
  double waveform_delay = 0.0;
  if (!findDriverDelaySlew(dmp_solver_model, waveform_delay, driver_slew)) {
    driver_slew = table_slew;
  }
  return std::isfinite(gate_delay) && std::isfinite(driver_slew);
}

bool ParasiticDmpSolver::initPi(DmpSolverModel& dmp_solver_model)
{
  dmp_solver_model.get_is_pi() = true;
  dmp_solver_model.get_is_zero_c2() = false;
  dmp_solver_model.get_newton_order() = 3;
  double denominator = dmp_solver_model.get_pi_resistance() * dmp_solver_model.get_driver_resistance()
                       * dmp_solver_model.get_load_capacitance() * dmp_solver_model.get_driver_capacitance();
  double coefficient = dmp_solver_model.get_driver_resistance()
                           * (dmp_solver_model.get_load_capacitance() + dmp_solver_model.get_driver_capacitance())
                       + dmp_solver_model.get_pi_resistance() * dmp_solver_model.get_load_capacitance();
  double discriminant = coefficient * coefficient - 4.0 * denominator;
  if (!(denominator > 0.0) || discriminant < 0.0) {
    return false;
  }

  dmp_solver_model.get_pi_zero()
      = 1.0 / (dmp_solver_model.get_pi_resistance() * dmp_solver_model.get_load_capacitance());
  dmp_solver_model.get_pi_scale()
      = 1.0 / (dmp_solver_model.get_driver_resistance() * dmp_solver_model.get_driver_capacitance());
  double root = std::sqrt(discriminant);
  dmp_solver_model.get_pi_pole1() = (coefficient + root) / (2.0 * denominator);
  dmp_solver_model.get_pi_pole2() = (coefficient - root) / (2.0 * denominator);
  double pole_product = dmp_solver_model.get_pi_pole1() * dmp_solver_model.get_pi_pole2();
  if (!(dmp_solver_model.get_pi_pole1() > 0.0) || !(dmp_solver_model.get_pi_pole2() > 0.0) || !(pole_product > 0.0)
      || std::abs(dmp_solver_model.get_pi_pole2() - dmp_solver_model.get_pi_pole1()) < kTinyNumber) {
    return false;
  }

  dmp_solver_model.get_pi_constant2() = dmp_solver_model.get_pi_zero() / pole_product;
  dmp_solver_model.get_pi_constant1()
      = (1.0
         - dmp_solver_model.get_pi_constant2() * (dmp_solver_model.get_pi_pole1() + dmp_solver_model.get_pi_pole2()))
        / pole_product;
  dmp_solver_model.get_pi_residue2()
      = (dmp_solver_model.get_pi_constant1() * dmp_solver_model.get_pi_pole1() + dmp_solver_model.get_pi_constant2())
        / (dmp_solver_model.get_pi_pole2() - dmp_solver_model.get_pi_pole1());
  dmp_solver_model.get_pi_residue1() = -dmp_solver_model.get_pi_constant1() - dmp_solver_model.get_pi_residue2();
  double current_zero = (dmp_solver_model.get_load_capacitance() + dmp_solver_model.get_driver_capacitance())
                        / (dmp_solver_model.get_pi_resistance() * dmp_solver_model.get_load_capacitance()
                           * dmp_solver_model.get_driver_capacitance());
  dmp_solver_model.get_pi_current_constant() = current_zero / pole_product;
  dmp_solver_model.get_pi_current_residue1()
      = (current_zero - dmp_solver_model.get_pi_pole1())
        / (dmp_solver_model.get_pi_pole1() * (dmp_solver_model.get_pi_pole1() - dmp_solver_model.get_pi_pole2()));
  dmp_solver_model.get_pi_current_residue2()
      = (current_zero - dmp_solver_model.get_pi_pole2())
        / (dmp_solver_model.get_pi_pole2() * (dmp_solver_model.get_pi_pole2() - dmp_solver_model.get_pi_pole1()));
  return std::isfinite(dmp_solver_model.get_pi_residue1()) && std::isfinite(dmp_solver_model.get_pi_residue2())
         && std::isfinite(dmp_solver_model.get_pi_current_constant())
         && std::isfinite(dmp_solver_model.get_pi_current_residue1())
         && std::isfinite(dmp_solver_model.get_pi_current_residue2());
}

bool ParasiticDmpSolver::calcZeroC2(
    DmpSolverModel& dmp_solver_model,
    double& gate_delay,
    double& driver_slew,
    double& effective_capacitance
)
{
  if (!initZeroC2(dmp_solver_model)) {
    return calcCap(dmp_solver_model, gate_delay, driver_slew, effective_capacitance);
  }

  effective_capacitance = dmp_solver_model.get_load_capacitance();
  if (findDriverParams(dmp_solver_model, effective_capacitance)
      && findDriverDelaySlew(dmp_solver_model, gate_delay, driver_slew)) {
    return true;
  }
  return getGateDelaySlew(dmp_solver_model, effective_capacitance, gate_delay, driver_slew);
}

bool ParasiticDmpSolver::initZeroC2(DmpSolverModel& dmp_solver_model)
{
  dmp_solver_model.get_is_pi() = false;
  dmp_solver_model.get_is_zero_c2() = true;
  dmp_solver_model.get_newton_order() = 2;
  if (!(dmp_solver_model.get_pi_resistance() > 0.0) || !(dmp_solver_model.get_load_capacitance() > 0.0)
      || !(dmp_solver_model.get_driver_resistance() > 0.0)) {
    return false;
  }

  dmp_solver_model.get_zero_zero()
      = 1.0 / (dmp_solver_model.get_pi_resistance() * dmp_solver_model.get_load_capacitance());
  dmp_solver_model.get_zero_pole()
      = 1.0
        / (dmp_solver_model.get_load_capacitance()
           * (dmp_solver_model.get_driver_resistance() + dmp_solver_model.get_pi_resistance()));
  dmp_solver_model.get_zero_scale() = dmp_solver_model.get_zero_pole() / dmp_solver_model.get_zero_zero();
  if (!(dmp_solver_model.get_zero_scale() > 0.0) || !(dmp_solver_model.get_zero_pole() > 0.0)) {
    return false;
  }
  dmp_solver_model.get_zero_constant2() = 1.0 / dmp_solver_model.get_zero_scale();
  dmp_solver_model.get_zero_constant1() = (dmp_solver_model.get_zero_pole() - dmp_solver_model.get_zero_zero())
                                          / (dmp_solver_model.get_zero_pole() * dmp_solver_model.get_zero_pole());
  dmp_solver_model.get_zero_residue() = -dmp_solver_model.get_zero_constant1();
  return std::isfinite(dmp_solver_model.get_zero_constant1()) && std::isfinite(dmp_solver_model.get_zero_constant2())
         && std::isfinite(dmp_solver_model.get_zero_residue());
}

bool ParasiticDmpSolver::findDriverParams(DmpSolverModel& dmp_solver_model, double effective_capacitance)
{
  if (dmp_solver_model.get_newton_order() == 3) {
    dmp_solver_model.get_parameter_list()[kEffectiveCapacitanceIndex] = effective_capacitance;
  }
  double threshold_delay = 0.0;
  double lower_delay = 0.0;
  double measured_slew = 0.0;
  if (!getGateDelays(dmp_solver_model, effective_capacitance, threshold_delay, lower_delay, measured_slew)) {
    return false;
  }

  double threshold_span = dmp_solver_model.get_upper_threshold() - dmp_solver_model.get_lower_threshold();
  double transition_time = measured_slew / threshold_span;
  double start_time = threshold_delay
                      + std::log(1.0 - dmp_solver_model.get_threshold()) * dmp_solver_model.get_driver_resistance()
                            * effective_capacitance
                      - dmp_solver_model.get_threshold() * transition_time;
  dmp_solver_model.get_parameter_list()[kTransitionTimeIndex] = transition_time;
  dmp_solver_model.get_parameter_list()[kStartTimeIndex] = start_time;
  if (!newtonRaphson(dmp_solver_model)) {
    return false;
  }
  dmp_solver_model.get_start_time() = dmp_solver_model.get_parameter_list()[kStartTimeIndex];
  dmp_solver_model.get_transition_time() = dmp_solver_model.get_parameter_list()[kTransitionTimeIndex];
  return std::isfinite(dmp_solver_model.get_start_time()) && std::isfinite(dmp_solver_model.get_transition_time())
         && dmp_solver_model.get_transition_time() > 0.0;
}

bool ParasiticDmpSolver::getGateDelays(
    DmpSolverModel& dmp_solver_model,
    double effective_capacitance,
    double& threshold_delay,
    double& lower_delay,
    double& measured_slew
)
{
  double table_slew = 0.0;
  if (!getGateDelaySlew(dmp_solver_model, effective_capacitance, threshold_delay, table_slew)) {
    return false;
  }
  measured_slew = table_slew * dmp_solver_model.get_slew_derate();
  double threshold_span = dmp_solver_model.get_upper_threshold() - dmp_solver_model.get_lower_threshold();
  if (!(threshold_span > 0.0)) {
    return false;
  }
  lower_delay
      = threshold_delay
        - measured_slew * (dmp_solver_model.get_threshold() - dmp_solver_model.get_lower_threshold()) / threshold_span;
  return measured_slew > 0.0;
}

bool ParasiticDmpSolver::newtonRaphson(DmpSolverModel& dmp_solver_model)
{
  for (int32_t iteration = 0; iteration < kMaxNewtonIteration; iteration++) {
    if (!evalDmpEqns(dmp_solver_model)) {
      return false;
    }
    for (int32_t index = 0; index < dmp_solver_model.get_newton_order(); index++) {
      dmp_solver_model.get_delta_list()[index] = -dmp_solver_model.get_function_list()[index];
    }
    if (!decomposeJacobian(dmp_solver_model)) {
      return false;
    }
    solveJacobian(dmp_solver_model);

    bool is_converged = true;
    for (int32_t index = 0; index < dmp_solver_model.get_newton_order(); index++) {
      if (!std::isfinite(dmp_solver_model.get_delta_list()[index])
          || std::abs(dmp_solver_model.get_delta_list()[index])
                 > std::abs(dmp_solver_model.get_parameter_list()[index]) * kDriverParameterTolerance) {
        is_converged = false;
      }
      dmp_solver_model.get_parameter_list()[index] += dmp_solver_model.get_delta_list()[index];
    }
    if (is_converged) {
      return evalDmpEqns(dmp_solver_model);
    }
  }
  return false;
}

bool ParasiticDmpSolver::evalDmpEqns(DmpSolverModel& dmp_solver_model)
{
  if (dmp_solver_model.get_is_pi()) {
    return evalPiEqns(dmp_solver_model);
  }
  if (dmp_solver_model.get_is_zero_c2()) {
    return evalOnePoleEqns(dmp_solver_model);
  }
  return false;
}

bool ParasiticDmpSolver::evalPiEqns(DmpSolverModel& dmp_solver_model)
{
  double start_time = dmp_solver_model.get_parameter_list()[kStartTimeIndex];
  double transition_time = dmp_solver_model.get_parameter_list()[kTransitionTimeIndex];
  double effective_capacitance = dmp_solver_model.get_parameter_list()[kEffectiveCapacitanceIndex];
  double total_capacitance = dmp_solver_model.get_load_capacitance() + dmp_solver_model.get_driver_capacitance();
  if (!(effective_capacitance > 0.0) || effective_capacitance > total_capacitance || !(transition_time > 0.0)) {
    return false;
  }

  double threshold_delay = 0.0;
  double lower_delay = 0.0;
  double measured_slew = 0.0;
  if (!getGateDelays(dmp_solver_model, effective_capacitance, threshold_delay, lower_delay, measured_slew)) {
    return false;
  }
  double effective_capacitance_time
      = measured_slew / (dmp_solver_model.get_upper_threshold() - dmp_solver_model.get_lower_threshold());
  effective_capacitance_time = std::min(effective_capacitance_time, 1.4 * transition_time);

  double threshold_voltage = 0.0;
  double lower_voltage = 0.0;
  calcCapacitiveWaveform(
      dmp_solver_model,
      threshold_delay,
      start_time,
      transition_time,
      effective_capacitance,
      threshold_voltage
  );
  calcCapacitiveWaveform(
      dmp_solver_model,
      lower_delay,
      start_time,
      transition_time,
      effective_capacitance,
      lower_voltage
  );
  dmp_solver_model.get_function_list()[kCurrentIndex]
      = calcPiCurrentDifference(dmp_solver_model, transition_time, effective_capacitance_time, effective_capacitance);
  dmp_solver_model.get_function_list()[kThresholdVoltageIndex] = threshold_voltage - dmp_solver_model.get_threshold();
  dmp_solver_model.get_function_list()[kLowerVoltageIndex] = lower_voltage - dmp_solver_model.get_lower_threshold();

  double exp_pole1 = calcDmpExp(-dmp_solver_model.get_pi_pole1() * transition_time);
  double exp_pole2 = calcDmpExp(-dmp_solver_model.get_pi_pole2() * transition_time);
  double exp_effective
      = calcDmpExp(-transition_time / (dmp_solver_model.get_driver_resistance() * effective_capacitance));
  dmp_solver_model.get_jacobian()[kCurrentIndex][kStartTimeIndex] = 0.0;
  dmp_solver_model.get_jacobian()[kCurrentIndex][kTransitionTimeIndex]
      = (-dmp_solver_model.get_pi_current_constant() * transition_time
         + dmp_solver_model.get_pi_current_residue1() * transition_time * exp_pole1
         - (2.0 * dmp_solver_model.get_pi_current_residue1() / dmp_solver_model.get_pi_pole1()) * (1.0 - exp_pole1)
         + dmp_solver_model.get_pi_current_residue2() * transition_time * exp_pole2
         - (2.0 * dmp_solver_model.get_pi_current_residue2() / dmp_solver_model.get_pi_pole2()) * (1.0 - exp_pole2)
         + dmp_solver_model.get_driver_resistance() * effective_capacitance
               * (transition_time + transition_time * exp_effective
                  - 2.0 * dmp_solver_model.get_driver_resistance() * effective_capacitance * (1.0 - exp_effective)))
        / (dmp_solver_model.get_driver_resistance() * transition_time * transition_time * transition_time);
  dmp_solver_model.get_jacobian()[kCurrentIndex][kEffectiveCapacitanceIndex]
      = (2.0 * dmp_solver_model.get_driver_resistance() * effective_capacitance - transition_time
         - (2.0 * dmp_solver_model.get_driver_resistance() * effective_capacitance + transition_time) * exp_effective)
        / (transition_time * transition_time);

  calcCapacitiveWaveformDerivative(
      dmp_solver_model,
      lower_delay,
      start_time,
      transition_time,
      effective_capacitance,
      dmp_solver_model.get_jacobian()[kLowerVoltageIndex][kStartTimeIndex],
      dmp_solver_model.get_jacobian()[kLowerVoltageIndex][kTransitionTimeIndex],
      dmp_solver_model.get_jacobian()[kLowerVoltageIndex][kEffectiveCapacitanceIndex]
  );
  calcCapacitiveWaveformDerivative(
      dmp_solver_model,
      threshold_delay,
      start_time,
      transition_time,
      effective_capacitance,
      dmp_solver_model.get_jacobian()[kThresholdVoltageIndex][kStartTimeIndex],
      dmp_solver_model.get_jacobian()[kThresholdVoltageIndex][kTransitionTimeIndex],
      dmp_solver_model.get_jacobian()[kThresholdVoltageIndex][kEffectiveCapacitanceIndex]
  );
  return std::isfinite(dmp_solver_model.get_function_list()[kCurrentIndex])
         && std::isfinite(dmp_solver_model.get_function_list()[kThresholdVoltageIndex])
         && std::isfinite(dmp_solver_model.get_function_list()[kLowerVoltageIndex]);
}

double ParasiticDmpSolver::calcPiCurrentDifference(
    DmpSolverModel& dmp_solver_model,
    double transition_time,
    double effective_capacitance_time,
    double effective_capacitance
)
{
  double exp_pole1 = calcDmpExp(-dmp_solver_model.get_pi_pole1() * effective_capacitance_time);
  double exp_pole2 = calcDmpExp(-dmp_solver_model.get_pi_pole2() * effective_capacitance_time);
  double exp_effective
      = calcDmpExp(-effective_capacitance_time / (dmp_solver_model.get_driver_resistance() * effective_capacitance));
  double pi_current
      = (dmp_solver_model.get_pi_current_constant() * effective_capacitance_time
         + (dmp_solver_model.get_pi_current_residue1() / dmp_solver_model.get_pi_pole1()) * (1.0 - exp_pole1)
         + (dmp_solver_model.get_pi_current_residue2() / dmp_solver_model.get_pi_pole2()) * (1.0 - exp_pole2))
        / (dmp_solver_model.get_driver_resistance() * effective_capacitance_time * transition_time);
  double effective_current
      = (dmp_solver_model.get_driver_resistance() * effective_capacitance * effective_capacitance_time
         - std::pow(dmp_solver_model.get_driver_resistance() * effective_capacitance, 2) * (1.0 - exp_effective))
        / (dmp_solver_model.get_driver_resistance() * effective_capacitance_time * transition_time);
  return pi_current - effective_current;
}

bool ParasiticDmpSolver::evalOnePoleEqns(DmpSolverModel& dmp_solver_model)
{
  double start_time = dmp_solver_model.get_parameter_list()[kStartTimeIndex];
  double transition_time = dmp_solver_model.get_parameter_list()[kTransitionTimeIndex];
  double threshold_delay = 0.0;
  double lower_delay = 0.0;
  double measured_slew = 0.0;
  if (!getGateDelays(
          dmp_solver_model,
          dmp_solver_model.get_load_capacitance(),
          threshold_delay,
          lower_delay,
          measured_slew
      )
      || !(transition_time > 0.0)) {
    return false;
  }

  double threshold_voltage = 0.0;
  double lower_voltage = 0.0;
  calcCapacitiveWaveform(
      dmp_solver_model,
      threshold_delay,
      start_time,
      transition_time,
      dmp_solver_model.get_load_capacitance(),
      threshold_voltage
  );
  calcCapacitiveWaveform(
      dmp_solver_model,
      lower_delay,
      start_time,
      transition_time,
      dmp_solver_model.get_load_capacitance(),
      lower_voltage
  );
  dmp_solver_model.get_function_list()[kThresholdVoltageIndex] = threshold_voltage - dmp_solver_model.get_threshold();
  dmp_solver_model.get_function_list()[kLowerVoltageIndex] = lower_voltage - dmp_solver_model.get_lower_threshold();

  double ignored_capacitance_derivative = 0.0;
  calcCapacitiveWaveformDerivative(
      dmp_solver_model,
      lower_delay,
      start_time,
      transition_time,
      dmp_solver_model.get_load_capacitance(),
      dmp_solver_model.get_jacobian()[kLowerVoltageIndex][kStartTimeIndex],
      dmp_solver_model.get_jacobian()[kLowerVoltageIndex][kTransitionTimeIndex],
      ignored_capacitance_derivative
  );
  calcCapacitiveWaveformDerivative(
      dmp_solver_model,
      threshold_delay,
      start_time,
      transition_time,
      dmp_solver_model.get_load_capacitance(),
      dmp_solver_model.get_jacobian()[kThresholdVoltageIndex][kStartTimeIndex],
      dmp_solver_model.get_jacobian()[kThresholdVoltageIndex][kTransitionTimeIndex],
      ignored_capacitance_derivative
  );
  return std::isfinite(dmp_solver_model.get_function_list()[kThresholdVoltageIndex])
         && std::isfinite(dmp_solver_model.get_function_list()[kLowerVoltageIndex]);
}

void ParasiticDmpSolver::calcCapacitiveWaveform(
    DmpSolverModel& dmp_solver_model,
    double time,
    double start_time,
    double transition_time,
    double capacitance,
    double& voltage
)
{
  double shifted_time = time - start_time;
  if (shifted_time <= 0.0) {
    voltage = 0.0;
  } else if (shifted_time <= transition_time) {
    voltage = calcCapacitiveUnitRamp(dmp_solver_model, shifted_time, capacitance) / transition_time;
  } else {
    voltage = (calcCapacitiveUnitRamp(dmp_solver_model, shifted_time, capacitance)
               - calcCapacitiveUnitRamp(dmp_solver_model, shifted_time - transition_time, capacitance))
              / transition_time;
  }
}

double ParasiticDmpSolver::calcCapacitiveUnitRamp(DmpSolverModel& dmp_solver_model, double time, double capacitance)
{
  double time_constant = dmp_solver_model.get_driver_resistance() * capacitance;
  if (!(time_constant > 0.0)) {
    return time;
  }
  return time - time_constant * (1.0 - calcDmpExp(-time / time_constant));
}

void ParasiticDmpSolver::calcCapacitiveWaveformDerivative(
    DmpSolverModel& dmp_solver_model,
    double time,
    double start_time,
    double transition_time,
    double capacitance,
    double& start_derivative,
    double& transition_derivative,
    double& capacitance_derivative
)
{
  double shifted_time = time - start_time;
  if (shifted_time <= 0.0) {
    start_derivative = 0.0;
    transition_derivative = 0.0;
    capacitance_derivative = 0.0;
  } else if (shifted_time <= transition_time) {
    start_derivative
        = -calcCapacitiveUnitRampTimeDerivative(dmp_solver_model, shifted_time, capacitance) / transition_time;
    transition_derivative
        = -calcCapacitiveUnitRamp(dmp_solver_model, shifted_time, capacitance) / (transition_time * transition_time);
    capacitance_derivative
        = calcCapacitiveUnitRampCapDerivative(dmp_solver_model, shifted_time, capacitance) / transition_time;
  } else {
    start_derivative
        = -(calcCapacitiveUnitRampTimeDerivative(dmp_solver_model, shifted_time, capacitance)
            - calcCapacitiveUnitRampTimeDerivative(dmp_solver_model, shifted_time - transition_time, capacitance))
          / transition_time;
    transition_derivative
        = -(calcCapacitiveUnitRamp(dmp_solver_model, shifted_time, capacitance)
            + calcCapacitiveUnitRamp(dmp_solver_model, shifted_time - transition_time, capacitance))
              / (transition_time * transition_time)
          + calcCapacitiveUnitRampTimeDerivative(dmp_solver_model, shifted_time - transition_time, capacitance)
                / transition_time;
    capacitance_derivative
        = (calcCapacitiveUnitRampCapDerivative(dmp_solver_model, shifted_time, capacitance)
           - calcCapacitiveUnitRampCapDerivative(dmp_solver_model, shifted_time - transition_time, capacitance))
          / transition_time;
  }
}

double ParasiticDmpSolver::calcCapacitiveUnitRampTimeDerivative(
    DmpSolverModel& dmp_solver_model,
    double time,
    double capacitance
)
{
  double time_constant = dmp_solver_model.get_driver_resistance() * capacitance;
  if (!(time_constant > 0.0)) {
    return 1.0;
  }
  return 1.0 - calcDmpExp(-time / time_constant);
}

double ParasiticDmpSolver::calcCapacitiveUnitRampCapDerivative(
    DmpSolverModel& dmp_solver_model,
    double time,
    double capacitance
)
{
  double time_constant = dmp_solver_model.get_driver_resistance() * capacitance;
  if (!(time_constant > 0.0)) {
    return 0.0;
  }
  return dmp_solver_model.get_driver_resistance()
         * ((1.0 + time / time_constant) * calcDmpExp(-time / time_constant) - 1.0);
}

bool ParasiticDmpSolver::decomposeJacobian(DmpSolverModel& dmp_solver_model)
{
  for (int32_t row = 0; row < dmp_solver_model.get_newton_order(); row++) {
    double largest_value = 0.0;
    for (int32_t column = 0; column < dmp_solver_model.get_newton_order(); column++) {
      largest_value = std::max(largest_value, std::abs(dmp_solver_model.get_jacobian()[row][column]));
    }
    if (largest_value == 0.0 || !std::isfinite(largest_value)) {
      return false;
    }
    dmp_solver_model.get_scale_list()[row] = 1.0 / largest_value;
  }

  int32_t last_index = dmp_solver_model.get_newton_order() - 1;
  for (int32_t column = 0; column < dmp_solver_model.get_newton_order(); column++) {
    for (int32_t row = 0; row < column; row++) {
      double value = dmp_solver_model.get_jacobian()[row][column];
      for (int32_t index = 0; index < row; index++) {
        value -= dmp_solver_model.get_jacobian()[row][index] * dmp_solver_model.get_jacobian()[index][column];
      }
      dmp_solver_model.get_jacobian()[row][column] = value;
    }

    double largest_value = 0.0;
    int32_t pivot_row = column;
    for (int32_t row = column; row < dmp_solver_model.get_newton_order(); row++) {
      double value = dmp_solver_model.get_jacobian()[row][column];
      for (int32_t index = 0; index < column; index++) {
        value -= dmp_solver_model.get_jacobian()[row][index] * dmp_solver_model.get_jacobian()[index][column];
      }
      dmp_solver_model.get_jacobian()[row][column] = value;
      double scaled_value = dmp_solver_model.get_scale_list()[row] * std::abs(value);
      if (scaled_value >= largest_value) {
        largest_value = scaled_value;
        pivot_row = row;
      }
    }
    if (column != pivot_row) {
      std::swap(dmp_solver_model.get_jacobian()[pivot_row], dmp_solver_model.get_jacobian()[column]);
      dmp_solver_model.get_scale_list()[pivot_row] = dmp_solver_model.get_scale_list()[column];
    }
    dmp_solver_model.get_index_list()[column] = pivot_row;
    if (dmp_solver_model.get_jacobian()[column][column] == 0.0) {
      dmp_solver_model.get_jacobian()[column][column] = kTinyNumber;
    }
    if (column != last_index) {
      double inverse_pivot = 1.0 / dmp_solver_model.get_jacobian()[column][column];
      for (int32_t row = column + 1; row < dmp_solver_model.get_newton_order(); row++) {
        dmp_solver_model.get_jacobian()[row][column] *= inverse_pivot;
      }
    }
  }
  return true;
}

void ParasiticDmpSolver::solveJacobian(DmpSolverModel& dmp_solver_model)
{
  int32_t first_nonzero = -1;
  for (int32_t row = 0; row < dmp_solver_model.get_newton_order(); row++) {
    int32_t pivot_row = dmp_solver_model.get_index_list()[row];
    double value = dmp_solver_model.get_delta_list()[pivot_row];
    dmp_solver_model.get_delta_list()[pivot_row] = dmp_solver_model.get_delta_list()[row];
    if (first_nonzero != -1) {
      for (int32_t column = first_nonzero; column < row; column++) {
        value -= dmp_solver_model.get_jacobian()[row][column] * dmp_solver_model.get_delta_list()[column];
      }
    } else if (value != 0.0) {
      first_nonzero = row;
    }
    dmp_solver_model.get_delta_list()[row] = value;
  }

  for (int32_t row = dmp_solver_model.get_newton_order() - 1; row >= 0; row--) {
    double value = dmp_solver_model.get_delta_list()[row];
    for (int32_t column = row + 1; column < dmp_solver_model.get_newton_order(); column++) {
      value -= dmp_solver_model.get_jacobian()[row][column] * dmp_solver_model.get_delta_list()[column];
    }
    dmp_solver_model.get_delta_list()[row] = value / dmp_solver_model.get_jacobian()[row][row];
  }
}

bool ParasiticDmpSolver::findDriverDelaySlew(
    DmpSolverModel& dmp_solver_model,
    double& driver_delay,
    double& driver_slew
)
{
  double upper_bound = getOutputCrossingUpperBound(dmp_solver_model);
  double lower_crossing = 0.0;
  double upper_crossing = 0.0;
  if (!(upper_bound > dmp_solver_model.get_start_time())
      || !findOutputCrossing(
          dmp_solver_model,
          dmp_solver_model.get_threshold(),
          dmp_solver_model.get_start_time(),
          upper_bound,
          driver_delay
      )
      || !findOutputCrossing(
          dmp_solver_model,
          dmp_solver_model.get_lower_threshold(),
          dmp_solver_model.get_start_time(),
          driver_delay,
          lower_crossing
      )
      || !findOutputCrossing(
          dmp_solver_model,
          dmp_solver_model.get_upper_threshold(),
          driver_delay,
          upper_bound,
          upper_crossing
      )) {
    return false;
  }
  driver_slew = (upper_crossing - lower_crossing) / dmp_solver_model.get_slew_derate();
  return std::isfinite(driver_delay) && std::isfinite(driver_slew) && driver_slew >= 0.0;
}

bool ParasiticDmpSolver::findOutputCrossing(
    DmpSolverModel& dmp_solver_model,
    double threshold,
    double lower_time,
    double upper_time,
    double& crossing_time
)
{
  std::function<void(double, double&, double&)> waveform_function
      = [this, &dmp_solver_model, threshold](double time, double& value, double& derivative) {
          calcOutputWaveform(dmp_solver_model, time, value, derivative);
          value -= threshold;
        };
  return findRoot(waveform_function, lower_time, upper_time, crossing_time);
}

bool ParasiticDmpSolver::findRoot(
    std::function<void(double, double&, double&)>& function,
    double lower_time,
    double upper_time,
    double& root
)
{
  double lower_value = 0.0;
  double upper_value = 0.0;
  double ignored_derivative = 0.0;
  function(lower_time, lower_value, ignored_derivative);
  function(upper_time, upper_value, ignored_derivative);
  if (!std::isfinite(lower_value) || !std::isfinite(upper_value) || lower_value * upper_value > 0.0) {
    return false;
  }
  if (lower_value == 0.0) {
    root = lower_time;
    return true;
  }
  if (upper_value == 0.0) {
    root = upper_time;
    return true;
  }
  if (lower_value > 0.0) {
    std::swap(lower_time, upper_time);
  }

  root = 0.5 * (lower_time + upper_time);
  double previous_delta = std::abs(upper_time - lower_time);
  double delta = previous_delta;
  double value = 0.0;
  double derivative = 0.0;
  function(root, value, derivative);
  for (int32_t iteration = 0; iteration < kMaxRootIteration; iteration++) {
    bool use_bisection
        = derivative == 0.0
          || (((root - upper_time) * derivative - value) * ((root - lower_time) * derivative - value) > 0.0)
          || std::abs(2.0 * value) > std::abs(previous_delta * derivative);
    if (use_bisection) {
      previous_delta = delta;
      delta = 0.5 * (upper_time - lower_time);
      root = lower_time + delta;
    } else {
      previous_delta = delta;
      delta = value / derivative;
      root -= delta;
    }
    if (std::abs(delta) <= kThresholdTimeTolerance * std::abs(root)) {
      return true;
    }

    function(root, value, derivative);
    if (!std::isfinite(value) || !std::isfinite(derivative)) {
      return false;
    }
    if (value < 0.0) {
      lower_time = root;
    } else {
      upper_time = root;
    }
  }
  return false;
}

void ParasiticDmpSolver::calcOutputWaveform(
    DmpSolverModel& dmp_solver_model,
    double time,
    double& voltage,
    double& derivative
)
{
  double shifted_time = time - dmp_solver_model.get_start_time();
  if (shifted_time <= 0.0) {
    voltage = 0.0;
    derivative = 0.0;
    return;
  }

  double unit_voltage = 0.0;
  double unit_derivative = 0.0;
  if (shifted_time <= dmp_solver_model.get_transition_time()) {
    if (dmp_solver_model.get_is_pi()) {
      calcPiUnitRamp(dmp_solver_model, shifted_time, unit_voltage, unit_derivative);
    } else {
      calcZeroC2UnitRamp(dmp_solver_model, shifted_time, unit_voltage, unit_derivative);
    }
    voltage = unit_voltage / dmp_solver_model.get_transition_time();
    derivative = unit_derivative / dmp_solver_model.get_transition_time();
    return;
  }

  double delayed_voltage = 0.0;
  double delayed_derivative = 0.0;
  if (dmp_solver_model.get_is_pi()) {
    calcPiUnitRamp(dmp_solver_model, shifted_time, unit_voltage, unit_derivative);
    calcPiUnitRamp(
        dmp_solver_model,
        shifted_time - dmp_solver_model.get_transition_time(),
        delayed_voltage,
        delayed_derivative
    );
  } else {
    calcZeroC2UnitRamp(dmp_solver_model, shifted_time, unit_voltage, unit_derivative);
    calcZeroC2UnitRamp(
        dmp_solver_model,
        shifted_time - dmp_solver_model.get_transition_time(),
        delayed_voltage,
        delayed_derivative
    );
  }
  voltage = (unit_voltage - delayed_voltage) / dmp_solver_model.get_transition_time();
  derivative = (unit_derivative - delayed_derivative) / dmp_solver_model.get_transition_time();
}

void ParasiticDmpSolver::calcPiUnitRamp(
    DmpSolverModel& dmp_solver_model,
    double time,
    double& voltage,
    double& derivative
)
{
  double exp_pole1 = calcDmpExp(-dmp_solver_model.get_pi_pole1() * time);
  double exp_pole2 = calcDmpExp(-dmp_solver_model.get_pi_pole2() * time);
  voltage = dmp_solver_model.get_pi_scale()
            * (dmp_solver_model.get_pi_constant1() + dmp_solver_model.get_pi_constant2() * time
               + dmp_solver_model.get_pi_residue1() * exp_pole1 + dmp_solver_model.get_pi_residue2() * exp_pole2);
  derivative = dmp_solver_model.get_pi_scale()
               * (dmp_solver_model.get_pi_constant2()
                  - dmp_solver_model.get_pi_residue1() * dmp_solver_model.get_pi_pole1() * exp_pole1
                  - dmp_solver_model.get_pi_residue2() * dmp_solver_model.get_pi_pole2() * exp_pole2);
}

void ParasiticDmpSolver::calcZeroC2UnitRamp(
    DmpSolverModel& dmp_solver_model,
    double time,
    double& voltage,
    double& derivative
)
{
  double exp_pole = calcDmpExp(-dmp_solver_model.get_zero_pole() * time);
  voltage = dmp_solver_model.get_zero_scale()
            * (dmp_solver_model.get_zero_constant1() + dmp_solver_model.get_zero_constant2() * time
               + dmp_solver_model.get_zero_residue() * exp_pole);
  derivative = dmp_solver_model.get_zero_scale()
               * (dmp_solver_model.get_zero_constant2()
                  - dmp_solver_model.get_zero_residue() * dmp_solver_model.get_zero_pole() * exp_pole);
}

double ParasiticDmpSolver::getOutputCrossingUpperBound(DmpSolverModel& dmp_solver_model)
{
  if (dmp_solver_model.get_is_pi()) {
    return dmp_solver_model.get_start_time() + dmp_solver_model.get_transition_time()
           + (dmp_solver_model.get_load_capacitance() + dmp_solver_model.get_driver_capacitance())
                 * (dmp_solver_model.get_driver_resistance() + dmp_solver_model.get_pi_resistance()) * 2.0;
  }
  if (dmp_solver_model.get_is_zero_c2()) {
    return dmp_solver_model.get_start_time() + dmp_solver_model.get_transition_time()
           + dmp_solver_model.get_load_capacitance()
                 * (dmp_solver_model.get_driver_resistance() + dmp_solver_model.get_pi_resistance()) * 2.0;
  }
  return dmp_solver_model.get_start_time() + dmp_solver_model.get_transition_time();
}

double ParasiticDmpSolver::calcDmpExp(double value)
{
  if (value < -12.0) {
    return 0.0;
  }
  double result = 1.0 + value / 4096.0;
  for (int32_t iteration = 0; iteration < 12; iteration++) {
    result *= result;
  }
  return result;
}

void ParasiticDmpSolver::cacheParasiticDmpDriverResult(
    std::string& output_pin,
    AnalysisType analysis_type,
    TransType output_trans_type,
    double driver_slew,
    ParasiticDelayResult& timing_result
)
{
  SharedDriverResultCache::getInst().insert(
      SharedDriverResultCache::Kind::kDmp,
      {output_pin, analysis_type, output_trans_type, driver_slew},
      timing_result
  );
}

std::optional<double> ParasiticDmpSolver::getCachedLoadSlew(
    Arc& arc,
    AnalysisType analysis_type,
    TransType trans_type,
    double input_slew
)
{
  if (!std::isfinite(input_slew)) {
    return std::nullopt;
  }
  ParasiticDelayResult* timing_result = SharedDriverResultCache::getInst().find(
      SharedDriverResultCache::Kind::kDmp,
      {arc.get_source_pin(), analysis_type, trans_type, input_slew}
  );
  if (timing_result == nullptr) {
    return std::nullopt;
  }
  std::map<std::string, double>& load_slew_map = timing_result->get_load_slew_map();
  auto load_slew = load_slew_map.find(arc.get_sink_pin());
  if (load_slew == load_slew_map.end()) {
    return std::nullopt;
  }
  return load_slew->second;
}

ParasiticDmpModel& ParasiticDmpSolver::getParasiticDmpModel(
    ParasiticNet& parasitic_net,
    uint32_t source_node_id,
    AnalysisType analysis_type,
    TransType trans_type
)
{
  ParasiticModelKey key{&parasitic_net.get_net_name(), source_node_id, analysis_type, trans_type};
  return PWDCACHE.dmpModel(key, [&] {
    return buildParasiticDmpModel(parasitic_net, source_node_id, analysis_type, trans_type);
  });
}

ParasiticDmpModel ParasiticDmpSolver::buildParasiticDmpModel(
    ParasiticNet& parasitic_net,
    uint32_t source_node_id,
    AnalysisType analysis_type,
    TransType trans_type
)
{
  ParasiticDmpModel dmp_model;
  const ParasiticGraph& graph = getParasiticGraph(parasitic_net);
  if (source_node_id >= graph.size()) {
    return dmp_model;
  }
  std::vector<uint32_t> node_id_list;
  std::vector<int32_t> parent_idx_list;
  std::vector<double> resistance_list;
  std::vector<double> capacitance_list;
  flattenRcTree(
      parasitic_net,
      source_node_id,
      analysis_type,
      trans_type,
      node_id_list,
      parent_idx_list,
      resistance_list,
      capacitance_list
  );
  if (node_id_list.empty()) {
    return dmp_model;
  }

  buildPiModel(dmp_model, parent_idx_list, resistance_list, capacitance_list);
  if (!dmp_model.get_is_valid()) {
    return dmp_model;
  }
  buildParasiticDmpLoadModelList(
      dmp_model,
      graph,
      node_id_list,
      parent_idx_list,
      resistance_list,
      capacitance_list,
      source_node_id
  );
  return dmp_model;
}

void ParasiticDmpSolver::buildParasiticDmpLoadModelList(
    ParasiticDmpModel& dmp_model,
    const ParasiticGraph& graph,
    std::vector<uint32_t>& node_id_list,
    std::vector<int32_t>& parent_idx_list,
    std::vector<double>& resistance_list,
    std::vector<double>& capacitance_list,
    uint32_t source_node_id
)
{
  std::vector<std::vector<double>> moment_list(4, std::vector<double>(node_id_list.size(), 0.0));
  for (int32_t moment_idx = 1; moment_idx < 4; moment_idx++) {
    std::vector<double> branch_current_list(node_id_list.size(), 0.0);
    for (std::size_t node_idx = 0; node_idx < node_id_list.size(); node_idx++) {
      double previous_moment = moment_idx == 1 ? 1.0 : moment_list[moment_idx - 1][node_idx];
      branch_current_list[node_idx] = capacitance_list[node_idx] * previous_moment;
    }
    for (std::size_t node_idx = node_id_list.size() - 1; node_idx > 0; node_idx--) {
      int32_t parent_idx = parent_idx_list[node_idx];
      if (parent_idx < 0) {
        return;
      }
      branch_current_list[parent_idx] += branch_current_list[node_idx];
    }
    moment_list[moment_idx].front() = 0.0;
    for (std::size_t node_idx = 1; node_idx < node_id_list.size(); node_idx++) {
      int32_t parent_idx = parent_idx_list[node_idx];
      moment_list[moment_idx][node_idx]
          = moment_list[moment_idx][parent_idx] - resistance_list[node_idx] * branch_current_list[node_idx];
    }
  }

  for (std::size_t node_idx = 0; node_idx < node_id_list.size(); node_idx++) {
    const uint32_t node_id = node_id_list[node_idx];
    if (node_id == source_node_id) {
      continue;
    }
    if (!graph.is_pin(node_id)) {
      continue;
    }
    ParasiticDmpLoadModel load_model
        = buildParasiticDmpLoadModel(moment_list[1][node_idx], moment_list[2][node_idx], moment_list[3][node_idx]);
    if (load_model.get_is_valid()) {
      dmp_model.get_load_model_map()[*graph.node_name[node_id]] = load_model;
    }
  }
}

ParasiticDmpLoadModel ParasiticDmpSolver::buildParasiticDmpLoadModel(double moment1, double moment2, double moment3)
{
  ParasiticDmpLoadModel load_model;
  if (!std::isfinite(moment1) || !std::isfinite(moment2) || !std::isfinite(moment3) || moment1 == 0.0) {
    return load_model;
  }

  double pole1 = moment3 == 0.0 ? 0.0 : -moment2 / moment3;
  double ratio_denominator = moment2 == 0.0 || moment3 == 0.0 ? 0.0 : moment1 / moment2 - moment2 / moment3;
  double pole2 = 0.0;
  if (pole1 > 0.0 && ratio_denominator != 0.0 && moment2 != 0.0) {
    pole2 = pole1 * (1.0 / moment1 - moment1 / moment2) / ratio_denominator;
  }
  if (!(pole1 > 0.0) || !(pole2 > 0.0) || pole1 == pole2 || ratio_denominator == 0.0 || !std::isfinite(pole1)
      || !std::isfinite(pole2)) {
    pole1 = -1.0 / moment1;
    if (!(pole1 > 0.0) || !std::isfinite(pole1)) {
      return load_model;
    }
    load_model.set_is_valid(true);
    load_model.get_pole_list().push_back(pole1);
    load_model.get_residue_list().push_back(1.0);
    return load_model;
  }

  double residue1 = pole1 * pole1 * (1.0 + moment1 * pole2) / (pole1 - pole2);
  double residue2 = -pole2 * pole2 * (1.0 + moment1 * pole1) / (pole1 - pole2);
  if (!std::isfinite(residue1) || !std::isfinite(residue2)) {
    return load_model;
  }
  if (residue1 < 0.0 && residue2 > 0.0) {
    std::swap(pole1, pole2);
    std::swap(residue1, residue2);
  }
  load_model.set_is_valid(true);
  load_model.get_pole_list().push_back(pole1);
  load_model.get_pole_list().push_back(pole2);
  load_model.get_residue_list().push_back(residue1);
  load_model.get_residue_list().push_back(residue2);
  return load_model;
}

bool ParasiticDmpSolver::calcParasiticDmpLoadDelay(
    ParasiticDmpLoadModel& load_model,
    TimingArc& timing_arc,
    TransType trans_type,
    double driver_slew,
    double& wire_delay,
    double& load_slew
)
{
  if (!load_model.get_is_valid() || load_model.get_pole_list().empty() || load_model.get_residue_list().empty()) {
    return false;
  }
  double pole1 = load_model.get_pole_list().front();
  if (!(pole1 > 0.0)) {
    return false;
  }
  wire_delay = 1.0 / pole1;
  load_slew = driver_slew;
  if (load_model.get_pole_list().size() < 2 || load_model.get_residue_list().size() < 2
      || std::abs(driver_slew) <= PW_ERROR) {
    return true;
  }

  double pole2 = load_model.get_pole_list()[1];
  double residue1 = load_model.get_residue_list()[0];
  double residue2 = load_model.get_residue_list()[1];
  if (!(pole2 > 0.0)) {
    return true;
  }
  double threshold = getNormalizedThreshold(timing_arc.get_output_threshold_pct_rise());
  double lower_threshold = getNormalizedThreshold(timing_arc.get_slew_lower_threshold_pct_rise());
  double upper_threshold = getNormalizedThreshold(timing_arc.get_slew_upper_threshold_pct_rise());
  if (trans_type == TransType::kFall) {
    threshold = getNormalizedThreshold(timing_arc.get_output_threshold_pct_fall());
    lower_threshold = getNormalizedThreshold(timing_arc.get_slew_lower_threshold_pct_fall());
    upper_threshold = getNormalizedThreshold(timing_arc.get_slew_upper_threshold_pct_fall());
  }
  double slew_derate = timing_arc.get_slew_derate();
  if (!(upper_threshold > lower_threshold) || !(slew_derate > 0.0)) {
    return true;
  }

  double residue_pole1 = residue1 / (pole1 * pole1);
  double residue_pole2 = residue2 / (pole2 * pole2);
  double constant = residue_pole1 + residue_pole2;
  double transition_time = driver_slew * slew_derate / (upper_threshold - lower_threshold);
  if (!(transition_time > 0.0)) {
    return true;
  }
  double transition_voltage = (transition_time - constant + residue_pole1 * std::exp(-pole1 * transition_time)
                               + residue_pole2 * std::exp(-pole2 * transition_time))
                              / transition_time;
  double threshold_time = calcParasiticDmpLoadTime(
      threshold,
      pole1,
      pole2,
      residue1,
      residue2,
      constant,
      residue_pole1,
      residue_pole2,
      transition_time,
      transition_voltage
  );
  double lower_time = calcParasiticDmpLoadTime(
      lower_threshold,
      pole1,
      pole2,
      residue1,
      residue2,
      constant,
      residue_pole1,
      residue_pole2,
      transition_time,
      transition_voltage
  );
  double upper_time = calcParasiticDmpLoadTime(
      upper_threshold,
      pole1,
      pole2,
      residue1,
      residue2,
      constant,
      residue_pole1,
      residue_pole2,
      transition_time,
      transition_voltage
  );
  double dmp_wire_delay = threshold_time - transition_time * threshold;
  double dmp_load_slew = (upper_time - lower_time) / slew_derate;
  if (std::isfinite(dmp_wire_delay) && std::isfinite(dmp_load_slew) && dmp_load_slew >= 0.0) {
    wire_delay = dmp_wire_delay;
    load_slew = dmp_load_slew;
  }
  return true;
}

double ParasiticDmpSolver::calcParasiticDmpLoadTime(
    double threshold,
    double pole1,
    double pole2,
    double residue1,
    double residue2,
    double constant,
    double residue_pole1,
    double residue_pole2,
    double transition_time,
    double transition_voltage
)
{
  if (transition_voltage < threshold) {
    double logarithm_argument
        = residue1 * (std::exp(pole1 * transition_time) - 1.0) / ((1.0 - threshold) * pole1 * pole1 * transition_time);
    if (!(logarithm_argument > 0.0)) {
      return transition_time * threshold;
    }
    double time = std::log(logarithm_argument) / pole1;
    double exp_pole1_time = std::exp(-pole1 * time);
    double exp_pole2_time = std::exp(-pole2 * time);
    double exp_pole1_delta = std::exp(-pole1 * (time - transition_time));
    double exp_pole2_delta = std::exp(-pole2 * (time - transition_time));
    double voltage = (transition_time - residue_pole1 * (exp_pole1_delta - exp_pole1_time)
                      - residue_pole2 * (exp_pole2_delta - exp_pole2_time))
                     / transition_time;
    double derivative = (residue1 / pole1 * (exp_pole1_delta - exp_pole1_time)
                         - residue2 / pole2 * (exp_pole2_delta - exp_pole2_time))
                        / transition_time;
    if (std::abs(derivative) <= PW_ERROR) {
      return time;
    }
    return time - (voltage - threshold) / derivative;
  }

  double time = threshold * transition_time / transition_voltage;
  double exp_pole1_time = std::exp(-pole1 * time);
  double exp_pole2_time = std::exp(-pole2 * time);
  double voltage
      = (time - constant + residue_pole1 * exp_pole1_time + residue_pole2 * exp_pole1_time) / transition_time;
  double derivative = (1.0 - residue1 / pole1 * exp_pole1_time - residue2 / pole2 * exp_pole2_time) / transition_time;
  if (std::abs(derivative) <= PW_ERROR) {
    return time;
  }
  return time - (voltage - threshold) / derivative;
}

}  // namespace ipw

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
#include "RcModelBuilder.hpp"

#include <cstdio>
#include <cstdlib>

#include <Eigen/Eigenvalues>
#include <Eigen/IterativeLinearSolvers>
#include <Eigen/SparseCore>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "DmpDelayCalculator.hpp"
#include "TimingTolerance.hpp"

namespace ista {
namespace {
// ISTA_DRIVE_RES_SERIES=1 enables the physically exact source model: the driver
// resistance becomes a real branch between a new root node and the original
// source, and ArnoldiWaveformSolver::poleResidue drops its rank-1 fold of
// rd*C_total into the reduced [0][0]. Default off keeps every existing result
// bit-identical.
constexpr const char* kSeriesSourceNodeName = "__ista_source_r__";

bool driveResSeriesEnabled()
{
  static const bool enabled = [] {
    const char* e = std::getenv("ISTA_DRIVE_RES_SERIES");
    return e != nullptr && *e != '0';
  }();
  return enabled;
}

// Total-RC time constant below which the lumped small-net response (R*C*ln2)
// replaces the full network solve. Override with the environment variable
// ISTA_SMALL_NET_CUTOFF_PS (ps) for calibration experiments.
double smallNetCutoff()
{
  static const double value = [] {
    const char* text = std::getenv("ISTA_SMALL_NET_CUTOFF_PS");
    if (text == nullptr || *text == '\0')
      return .002;
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    return (end != text && parsed >= 0.) ? parsed * 1e-3 : .002;
  }();
  return value;
}

// reference's reduced small-net capacitance divider. Enabled by default;
// ISTA_REF_BALANCED=0 restores the plain total-RC response for A/B runs.
bool ptBalancedEnabled()
{
  static const bool value = [] {
    const char* text = std::getenv("ISTA_REF_BALANCED");
    return text == nullptr || *text != '0';
  }();
  return value;
}
// reference also applies the reduced small-net response to networks with
// parallel edges. Enabled by default; ISTA_LOOPED_SMALLNET=0 restores the
// tree-only guard.
bool loopedSmallNetEnabled()
{
  static const bool value = [] {
    const char* text = std::getenv("ISTA_LOOPED_SMALLNET");
    return text == nullptr || *text != '0';
  }();
  return value;
}
}  // namespace

std::string RcModelBuilder::getParasiticDmpModelKey(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                                    TransType trans_type)
{
  std::stringstream key_stream;
  key_stream << parasitic_net.get_net_name() << "|" << source_node_name << "|" << static_cast<int32_t>(analysis_type) << "|"
             << static_cast<int32_t>(trans_type);
  return key_stream.str();
}

ParasiticDmpModel& RcModelBuilder::dmpModel(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type, TransType trans_type)
{
  std::string dmp_model_key = getParasiticDmpModelKey(parasitic_net, source_node_name, analysis_type, trans_type);
  if (_parasitic_dmp_model_cache.count(dmp_model_key) == 0) {
    _parasitic_dmp_model_cache[dmp_model_key] = buildParasiticDmpModel(parasitic_net, source_node_name, analysis_type, trans_type);
  }
  return _parasitic_dmp_model_cache[dmp_model_key];
}

ParasiticDmpModel RcModelBuilder::buildParasiticDmpModel(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                                         TransType trans_type)
{
  ParasiticDmpModel dmp_model;
  std::vector<std::string> node_name_list;
  std::vector<int32_t> parent_idx_list;
  std::vector<double> resistance_list;
  std::vector<double> capacitance_list;
  _rc.buildTree(parasitic_net, source_node_name, analysis_type, trans_type, node_name_list, parent_idx_list, resistance_list, capacitance_list);
  if (node_name_list.empty()) {
    return dmp_model;
  }

  buildParasiticDmpPiModel(dmp_model, parent_idx_list, resistance_list, capacitance_list);
  if (!dmp_model.get_is_valid()) {
    return dmp_model;
  }
  buildParasiticDmpLoadModelList(dmp_model, node_name_list, parent_idx_list, resistance_list, capacitance_list, source_node_name);
  return dmp_model;
}

void RcModelBuilder::buildParasiticDmpPiModel(ParasiticDmpModel& dmp_model, std::vector<int32_t>& parent_idx_list, std::vector<double>& resistance_list,
                                              std::vector<double>& capacitance_list)
{
  if (parent_idx_list.size() != resistance_list.size() || resistance_list.size() != capacitance_list.size() || capacitance_list.empty()) {
    return;
  }

  std::vector<double> admittance1_list = capacitance_list;
  std::vector<double> admittance2_list(capacitance_list.size(), 0.0);
  std::vector<double> admittance3_list(capacitance_list.size(), 0.0);
  for (std::size_t node_idx = capacitance_list.size() - 1; node_idx > 0; node_idx--) {
    int32_t parent_idx = parent_idx_list[node_idx];
    if (parent_idx < 0) {
      return;
    }
    double resistance = resistance_list[node_idx];
    double admittance1 = admittance1_list[node_idx];
    double admittance2 = admittance2_list[node_idx];
    double admittance3 = admittance3_list[node_idx];
    admittance1_list[parent_idx] += admittance1;
    admittance2_list[parent_idx] += admittance2 - resistance * admittance1 * admittance1;
    admittance3_list[parent_idx]
        += admittance3 - 2.0 * resistance * admittance1 * admittance2 + resistance * resistance * admittance1 * admittance1 * admittance1;
  }

  double admittance1 = admittance1_list.front();
  double admittance2 = admittance2_list.front();
  double admittance3 = admittance3_list.front();
  double driver_capacitance = 0.0;
  double pi_resistance = 0.0;
  double load_capacitance = 0.0;
  if (admittance2 == 0.0 && admittance3 == 0.0) {
    load_capacitance = admittance1;
  } else {
    if (admittance3 == 0.0 || admittance2 == 0.0) {
      return;
    }
    load_capacitance = admittance2 * admittance2 / admittance3;
    driver_capacitance = admittance1 - load_capacitance;
    pi_resistance = -admittance3 * admittance3 / (admittance2 * admittance2 * admittance2);
  }
  if (!std::isfinite(driver_capacitance) || !std::isfinite(pi_resistance) || !std::isfinite(load_capacitance) || driver_capacitance < 0.0 || pi_resistance < 0.0
      || load_capacitance < 0.0 || driver_capacitance + load_capacitance <= 0.0) {
    return;
  }

  dmp_model.set_is_valid(true);
  dmp_model.set_driver_capacitance(driver_capacitance);
  dmp_model.set_pi_resistance(pi_resistance);
  dmp_model.set_load_capacitance(load_capacitance);
}

void RcModelBuilder::buildParasiticDmpLoadModelList(ParasiticDmpModel& dmp_model, std::vector<std::string>& node_name_list,
                                                    std::vector<int32_t>& parent_idx_list, std::vector<double>& resistance_list,
                                                    std::vector<double>& capacitance_list, std::string& source_node_name)
{
  Database& database = _database;
  std::vector<std::vector<double>> moment_list(4, std::vector<double>(node_name_list.size(), 0.0));
  for (int32_t moment_idx = 1; moment_idx < 4; moment_idx++) {
    std::vector<double> branch_current_list(node_name_list.size(), 0.0);
    for (std::size_t node_idx = 0; node_idx < node_name_list.size(); node_idx++) {
      double previous_moment = moment_idx == 1 ? 1.0 : moment_list[moment_idx - 1][node_idx];
      branch_current_list[node_idx] = capacitance_list[node_idx] * previous_moment;
    }
    for (std::size_t node_idx = node_name_list.size() - 1; node_idx > 0; node_idx--) {
      int32_t parent_idx = parent_idx_list[node_idx];
      if (parent_idx < 0) {
        return;
      }
      branch_current_list[parent_idx] += branch_current_list[node_idx];
    }
    moment_list[moment_idx].front() = 0.0;
    for (std::size_t node_idx = 1; node_idx < node_name_list.size(); node_idx++) {
      int32_t parent_idx = parent_idx_list[node_idx];
      moment_list[moment_idx][node_idx] = moment_list[moment_idx][parent_idx] - resistance_list[node_idx] * branch_current_list[node_idx];
    }
  }

  for (std::size_t node_idx = 0; node_idx < node_name_list.size(); node_idx++) {
    std::string& node_name = node_name_list[node_idx];
    if (node_name == source_node_name) {
      continue;
    }
    std::string pin_name = _rc.pinName(node_name);
    if (database.network().pins().count(pin_name) == 0) {
      continue;
    }
    ParasiticDmpLoadModel load_model = buildParasiticDmpLoadModel(moment_list[1][node_idx], moment_list[2][node_idx], moment_list[3][node_idx]);
    if (load_model.get_is_valid()) {
      dmp_model.get_load_model_map()[node_name] = load_model;
    }
  }
}

ParasiticDmpLoadModel RcModelBuilder::buildParasiticDmpLoadModel(double moment1, double moment2, double moment3)
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
  if (!(pole1 > 0.0) || !(pole2 > 0.0) || pole1 == pole2 || ratio_denominator == 0.0 || !std::isfinite(pole1) || !std::isfinite(pole2)) {
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

ParasiticArnoldiModel& RcModelBuilder::arnoldiModel(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                                    TransType trans_type, double drive_resistance)
{
  ArnoldiModelKey arnoldi_model_key = getParasiticArnoldiModelKey(parasitic_net, source_node_name, analysis_type, trans_type, drive_resistance);
  if (_parasitic_arnoldi_model_cache.count(arnoldi_model_key) == 0) {
    _parasitic_arnoldi_model_cache[arnoldi_model_key]
        = buildParasiticArnoldiModel(parasitic_net, source_node_name, analysis_type, trans_type, drive_resistance);
  }
  return _parasitic_arnoldi_model_cache[arnoldi_model_key];
}

RcModelBuilder::ArnoldiModelKey RcModelBuilder::getParasiticArnoldiModelKey(ParasiticNet& parasitic_net, std::string& source_node_name,
                                                                            AnalysisType analysis_type, TransType trans_type, double drive_resistance)
{
  // ISTA_DRIVE_RES_SERIES makes the source resistance a real branch of the
  // original tree, so two arcs that differ only in rd need two distinct models.
  // Quantize rd to 1 mOhm to keep the bucket count bounded.
  int64_t rd_bucket = 0;
  if (driveResSeriesEnabled() && drive_resistance > 0. && std::isfinite(drive_resistance))
    rd_bucket = static_cast<int64_t>(std::llround(drive_resistance * 1E6));
  return std::make_tuple(parasitic_net.get_net_name(), source_node_name, analysis_type, trans_type, rd_bucket);
}

ParasiticArnoldiModel RcModelBuilder::buildParasiticArnoldiModel(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                                                 TransType trans_type, double drive_resistance)
{
  ParasiticArnoldiModel arnoldi_model;
  std::vector<std::string> node_name_list;
  std::vector<int32_t> parent_idx_list;
  std::vector<double> resistance_list;
  std::vector<double> capacitance_list;
  _rc.buildTree(parasitic_net, source_node_name, analysis_type, trans_type, node_name_list, parent_idx_list, resistance_list, capacitance_list);
  if (node_name_list.empty()) {
    return arnoldi_model;
  }

  initParasiticArnoldiTerm(arnoldi_model, node_name_list, source_node_name);
  if (arnoldi_model.get_term_node_list().empty()) {
    return arnoldi_model;
  }

  {
    // Diagnostic only: compare the tree model's summed capacitance against the
    // SPEF net so a mismatch is attributable to buildTree rather than to the
    // downstream solver. ISTA_DUMP_MODEL_CAP gates the log.
    static const char* mc = std::getenv("ISTA_DUMP_MODEL_CAP");
    if (mc != nullptr) {
      double sum_c = 0.;
      for (double cv : capacitance_list) sum_c += cv;
      double net_c = 0.;
      for (auto& kv : parasitic_net.get_node_map()) net_c += kv.second.get_capacitance();
      if (FILE* fp = std::fopen(mc, "a"); fp != nullptr) {
        std::fprintf(fp, "MC,%s,%s,%zu,%zu,%.12g,%.12g,%zu\n", parasitic_net.get_net_name().c_str(),
                     source_node_name.c_str(), node_name_list.size(),
                     parasitic_net.get_node_map().size(), sum_c, net_c,
                     parasitic_net.get_resistor_list().size());
        std::fclose(fp);
      }
    }
  }
  if (driveResSeriesEnabled() && drive_resistance > 0. && std::isfinite(drive_resistance) && node_name_list.size() > 1
      && node_name_list[0] != kSeriesSourceNodeName) {
    // Insert the driver resistance as a real branch above the original source.
    // buildTree emits the root at index 0 with parent -1, and every node's
    // parent index is smaller than its own, so the new root must occupy index 0
    // and shift everything by one to preserve that ordering invariant.
    // Capacitance of the new node is 0: total load is unchanged, which keeps
    // this purely a source-impedance change. resistance_list is in kOhm.
    node_name_list.insert(node_name_list.begin(), kSeriesSourceNodeName);
    capacitance_list.insert(capacitance_list.begin(), 0.);
    resistance_list.insert(resistance_list.begin(), drive_resistance);
    parent_idx_list.insert(parent_idx_list.begin(), -1);
    for (std::size_t node_idx = 1; node_idx < parent_idx_list.size(); ++node_idx) {
      if (parent_idx_list[node_idx] >= 0)
        parent_idx_list[node_idx] += 1;
      else
        parent_idx_list[node_idx] = 0;  // the old root now hangs from the new one
    }
    // The old root had resistance 0 (nothing above it); its branch is now rd.
    resistance_list[1] = drive_resistance;
  }

  std::vector<std::size_t> term_point_idx_list;
  for (std::string& term_node_name : arnoldi_model.get_term_node_list()) {
    for (std::size_t node_idx = 0; node_idx < node_name_list.size(); node_idx++) {
      if (node_name_list[node_idx] == term_node_name) {
        term_point_idx_list.push_back(node_idx);
        break;
      }
    }
  }
  if (term_point_idx_list.size() != arnoldi_model.get_term_node_list().size()) {
    return arnoldi_model;
  }

  updateParasiticArnoldiModel(arnoldi_model, parasitic_net, node_name_list, parent_idx_list, resistance_list, capacitance_list, term_point_idx_list);
  // Summed resistance is a tree approximation, so parallel edges and loops keep
  // the conductance-aware model built above. reference still applies its
  // reduced small-net response to those networks, so the lumped branch below
  // is allowed for them as well; ISTA_LOOPED_SMALLNET=0 restores the old guard.
  const bool tree_like = parasitic_net.get_resistor_list().size() + 1 == node_name_list.size();
  if (!tree_like && !loopedSmallNetEnabled())
    return arnoldi_model;
  const auto net = _database.network().nets().find(parasitic_net.get_net_name());
  if (!arnoldi_model.get_is_valid() || net == _database.network().nets().end() || net->second.get_driver_pin_list().size() != 1
      || net->second.get_driver_pin_list().front() != _rc.pinName(source_node_name))
    return arnoldi_model;
  const auto& loads = net->second.get_load_pin_list();
  if (loads.empty() || loads.size() + 1 != arnoldi_model.get_term_pin_list().size())
    return arnoldi_model;
  for (const auto& pin : loads)
    if (pin == _rc.pinName(source_node_name) || !arnoldi_model.get_term_index_map().contains(pin))
      return arnoldi_model;
  double wire_capacitance = 0.;
  for (const auto& [name, node] : parasitic_net.get_node_map())
    wire_capacitance += node.get_capacitance();
  const double resistance = _rc.totalResistance(parasitic_net) * 1e-3;
  // reference folds the driver resistance into the network with a floor tied to
  // the net resistance: the per-sink merge routine (ref_shell_exec 0x11900bba)
  // computes rd_eff = rd + min(rd, threshold * rnet) without a branch, where
  // threshold is the rc_rd_less_than_rnet_threshold app var (default 0.45,
  // rodata 0xe81480 = 0x3fdccccccccccccd). Off by default: the only consumer of
  // drive_resistance is the series-insertion path, which needs
  // ISTA_DRIVE_RES_SERIES and measurably degrades the net delay on its own
  // (picorv32a >0.1ps 2.89% -> 5.40%). See FINDINGS 591.
  {
    static const double rd_floor = [] {
      const char* e = std::getenv("ISTA_RD_LESS_THAN_RNET");
      return e != nullptr ? std::atof(e) : 0.0;
    }();
    if (rd_floor > 0. && std::isfinite(resistance) && resistance > 0. && std::isfinite(drive_resistance) && drive_resistance > 0.) {
      double rd = drive_resistance;
      if (rd < rd_floor * resistance)
        rd += rd;
      else
        rd += rd_floor * resistance;
      drive_resistance = rd;
    }
  }
  // Fanout ceiling for the small-net reduction branch. reference does not
  // publish the limit; it is exposed so the modelled fanout range can be swept
  // against reference instead of being fixed at the historical value.
  static const std::size_t fanout_limit = [] {
    const char* e = std::getenv("ISTA_FANOUT_LIMIT");
    return e != nullptr ? static_cast<std::size_t>(std::atoi(e)) : std::size_t{5};
  }();
  const double count = static_cast<double>(loads.size());
  PinCapacitanceCalculator pin_capacitance(_database);
  std::array<double, 2> constants, reduced_constants, total_constants;
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    double pin_load = pin_capacitance.driverCapacitance(_rc.pinName(source_node_name), analysis_type, edge);
    for (auto pin : loads)
      pin_load += pin_capacitance.pinCapacitance(pin, analysis_type, edge);
    if (!std::isfinite(pin_load) || pin_load < 0.)
      return arnoldi_model;
    // The enhanced balanced range is limited to five receivers. Other
    // networks use total RC for the ordinary low-RC cutoff and response.
    // The enhancement also bounds the reduction to 10% of total RC.
    const double total_load = wire_capacitance + pin_load;
    const double reduced_load = std::max(wire_capacitance / (count + 1.) + pin_load / count, .1 * total_load);
    // Ordinary total RC keeps priority: when the network already fits below the
    // small-net cutoff the plain total-RC value is the reference and the
    // enhanced fanout reduction only extends that range (see reduced_load).
    // Round 101 measured REF2022 directly: a small net's reported delay is
    // ln2 * R * (Cw/(n+1) + Cp/n) even when the total RC is already below the
    // 2 ps cutoff, so the spread form is the response value for n <= 5.
    // The plain total-RC constant is kept separately because the balanced flag
    // (source-correction policy) must not be decided by the smaller value.
    // reference's "Balanced case tree" response, which report_delay_calculation
    // prints verbatim for the constant-driven fanout cone:
    //   RC delay: (r_wire/load_count) * (c_pin + c_wire/load_count)
    // Algebraically R * (n*c_pin + c_wire) / n^2 == R * totalC / n^2.  Both
    // r_wire and c_wire are byte-identical to this builder's resistance and
    // wire_capacitance, and n*c_pin equals pin_load, so the printed form
    // collapses onto the total RC divided by n^2.  ArnoldiResponseCalculator
    // multiplies the constant by ln2, hence the divisor below.
    static const bool balanced_tree = [] {
      const char* e = std::getenv("ISTA_REF_BALANCED_TREE");
      return e == nullptr ? true : std::atoi(e) != 0;
    }();
    // Only the constant-driven cone takes this branch: reference announces
    // "RC delay calculation is being skipped because the driver from-pin is
    // constant" for exactly that population and still emits a number, while
    // every other net keeps the full RC solve.
    bool constant_driver = false;
    if (balanced_tree) {
      const auto& sim = _database.graph().simValues();
      for (bool as_driver : {true, false}) {
        const auto logic = sim.value(net->second.get_driver_pin_list().front(), as_driver);
        if (logic.has_value()
            && (*logic == TimingCaseValue::kZero || *logic == TimingCaseValue::kOne || *logic == TimingCaseValue::kStatic))
          constant_driver = true;
      }
    }
    double response_load = total_load;
    if (ptBalancedEnabled() && loads.size() <= fanout_limit)
      response_load = wire_capacitance / (count + 1.) + pin_load / count;
    else if (constant_driver && count > 0.)
      response_load = total_load / (count * count * std::log(2.));
    constants[edge == TransType::kRise ? 0 : 1] = resistance * response_load;
    reduced_constants[edge == TransType::kRise ? 0 : 1] = resistance * reduced_load;
    total_constants[edge == TransType::kRise ? 0 : 1] = resistance * total_load;
    arnoldi_model.setTotalTimeConstants(total_constants);
    {
      static const bool ref_lumped = [] {
        const char* e = std::getenv("ISTA_REF_LUMPED_LN2");
        return e == nullptr ? true : std::atoi(e) != 0;
      }();
      // reference switches to the plain single-pole form as soon as the
      // receiver count passes the reduced-fanout range: measured alpha =
      // REF/(TotalR*TotalC) is 0.6935 +- 0.002 for every fanout >= 6 (1,839
      // corners) and the reduced form is exact for fanout <= 5. See FINDINGS 589.
      if (ref_lumped && !constant_driver && loads.size() > fanout_limit)
        arnoldi_model.setPtLumpedMode(1);
    }
    {
      static const char* rc_dump = std::getenv("ISTA_DUMP_NET_RC");
      if (rc_dump != nullptr) {
        if (FILE* rf = std::fopen(rc_dump, "a"); rf != nullptr) {
          std::fprintf(rf, "%s,%d,%d,%zu,%.12g,%.12g,%.12g,%.12g\n", parasitic_net.get_net_name().c_str(),
                       static_cast<int>(analysis_type), static_cast<int>(edge), loads.size(), resistance,
                       total_load, resistance * total_load, wire_capacitance);
          std::fclose(rf);
        }
      }
    }
  }
  // Ordinary total RC has priority. The enhanced model extends the small-net
  // range; applying it inside the ordinary range understates every wire delay.
  const bool balanced
      = _config.small_rc_fanout_reduction != 0 && std::max(total_constants[0], total_constants[1]) >= smallNetCutoff() && loads.size() <= fanout_limit;
  if (balanced)
    constants = reduced_constants;
  // Both low-RC models use a 2 ps cutoff. Select both edges together;
  // never replace a partially annotated
  // or multi-driver network with this approximation.
  if (std::isfinite(resistance) && resistance > 0. && constants[0] >= 0. && constants[1] >= 0. && std::max(constants[0], constants[1]) < smallNetCutoff()) {
    const std::unordered_set<std::string> connected(node_name_list.begin(), node_name_list.end());
    for (const auto& [name, node] : parasitic_net.get_node_map())
      if (!connected.contains(name) || !std::isfinite(node.get_capacitance()) || node.get_capacitance() < 0.)
        return arnoldi_model;
    for (auto& resistor : parasitic_net.get_resistor_list())
      if (!connected.contains(resistor.get_source_node()) || !connected.contains(resistor.get_sink_node()) || !std::isfinite(resistor.get_resistance())
          || resistor.get_resistance() < 0.)
        return arnoldi_model;
    static const int force_corr = [] {
      const char* e = std::getenv("ISTA_FORCE_SMALLNET_CORR");
      return e != nullptr ? std::atoi(e) : 2;
    }();
    // reference applies the small-net source correction (K = 0.314922/(N+1))
    // up to tau ~ 5 ps on cx55/prince; the former 0.3 ps cutoff left every
    // multi-load net uncorrected. Measured 19-case <0.1ps 92.1434% -> 94.0238%
    // (FINDINGS 524).
    static const double corr_rc_max = [] {
      const char* e = std::getenv("ISTA_SMALLNET_CORR_RC_MAX_PS");
      return e != nullptr ? std::atof(e) * 1e-3 : 5.0e-3;
    }();
    // 1 = always, 2 = only when tau <= corr_rc_max
    bool apply_corr = balanced;
    if (force_corr == 1) {
      apply_corr = true;
    } else if (force_corr == 2) {
      const double tau_max = std::max(constants[0], constants[1]);
      apply_corr = (corr_rc_max >= 0.0) ? (tau_max <= corr_rc_max) : true;
    }
    // reference's own report_delay_calculation fixes this boundary: for the
    // single-pole regime outside the reduced-fanout range it prints
    //   To_pin transition time == hypot(From_pin transition time, tau*slew_factor)
    // with From_pin transition time equal to the library output transition and
    // no source correction at all (measured exact to 0.0000 ps on
    // _23271_/Y->_24468_/B). Inside the reduced range (fanout <= fanout_limit)
    // the correction is required. Gating at fanout_limit measured 19-case
    // slews: >0.1ps 10.30% -> 8.23% (FINDINGS 590).
    static const int corr_fanout = [] {
      const char* e = std::getenv("ISTA_SMALLNET_CORR_FANOUT");
      return e != nullptr ? std::atoi(e) : static_cast<int>(fanout_limit);
    }();
    if (corr_fanout >= 0 && static_cast<int>(loads.size()) > corr_fanout)
      apply_corr = false;
    arnoldi_model.setSmallNetTimeConstants(constants, apply_corr);
  }
  return arnoldi_model;
}

void RcModelBuilder::initParasiticArnoldiTerm(ParasiticArnoldiModel& arnoldi_model, std::vector<std::string>& node_name_list, std::string& source_node_name)
{
  Database& database = _database;
  arnoldi_model.get_term_node_list().push_back(source_node_name);
  arnoldi_model.get_term_pin_list().push_back(_rc.pinName(source_node_name));
  arnoldi_model.get_term_index_map()[source_node_name] = 0;
  arnoldi_model.get_term_index_map()[arnoldi_model.get_term_pin_list().front()] = 0;
  for (std::string& node_name : node_name_list) {
    if (node_name == source_node_name) {
      continue;
    }
    std::string pin_name = _rc.pinName(node_name);
    if (database.network().pins().count(pin_name) == 0) {
      continue;
    }
    std::size_t term_idx = arnoldi_model.get_term_node_list().size();
    arnoldi_model.get_term_node_list().push_back(node_name);
    arnoldi_model.get_term_pin_list().push_back(pin_name);
    arnoldi_model.get_term_index_map()[node_name] = term_idx;
    arnoldi_model.get_term_index_map()[pin_name] = term_idx;
  }
}

void RcModelBuilder::updateParasiticArnoldiModel(ParasiticArnoldiModel& arnoldi_model, ParasiticNet& parasitic_net, std::vector<std::string>& node_name_list,
                                                 std::vector<int32_t>& parent_idx_list, std::vector<double>& resistance_list,
                                                 std::vector<double>& capacitance_list, std::vector<std::size_t>& term_point_idx_list)
{
  double total_capacitance = 0.0;
  for (double capacitance : capacitance_list) {
    total_capacitance += capacitance;
  }
  if (total_capacitance <= 0.0) {
    return;
  }

  std::size_t node_num = capacitance_list.size();
  // The reduced response must be able to separate the receiver terminals of a
  // multi-fanout net; a fixed small order cannot, and the per-terminal slew
  // then collapses onto the driver value.
  static const int32_t max_order = [] {
    const char* e = std::getenv("ISTA_ARNOLDI_MAX_ORDER");
    return e != nullptr ? static_cast<int32_t>(std::atoi(e)) : int32_t{5};
  }();
  int32_t order = std::min(static_cast<int32_t>(node_num), max_order);
  double sqrt_total_capacitance = std::sqrt(total_capacitance);
  std::vector<double> current_basis_list(node_num, 1.0 / sqrt_total_capacitance);
  std::vector<double> previous_basis_list(node_num, 0.0);
  std::vector<double> diagonal_list(order, 0.0);
  std::vector<double> off_diagonal_list(order, 0.0);
  std::vector<std::vector<double>> projection_list(order, std::vector<double>(term_point_idx_list.size(), 0.0));
  int32_t final_order = order;
  std::vector<bool> zero_capacitance_terms;
  for (auto node : term_point_idx_list)
    zero_capacitance_terms.push_back(capacitance_list[node] == 0.);
  std::vector<std::vector<double>> response_projection(order, std::vector<double>(term_point_idx_list.size()));

  std::map<std::string, std::size_t> node_idx_map;
  for (std::size_t node_idx = 0; node_idx < node_name_list.size(); node_idx++) {
    node_idx_map[node_name_list[node_idx]] = node_idx;
  }
  std::size_t network_resistance_num = 0;
  for (ParasiticResistor& parasitic_resistor : parasitic_net.get_resistor_list()) {
    if (node_idx_map.count(parasitic_resistor.get_source_node()) == 0 || node_idx_map.count(parasitic_resistor.get_sink_node()) == 0
        || parasitic_resistor.get_source_node() == parasitic_resistor.get_sink_node()) {
      continue;
    }
    network_resistance_num++;
  }

  bool has_resistance_loop = network_resistance_num + 1 > node_num;
  Eigen::SparseMatrix<double> conductance_matrix;
  Eigen::ConjugateGradient<Eigen::SparseMatrix<double>, Eigen::Lower | Eigen::Upper, Eigen::IncompleteCholesky<double>> conductance_solver;
  if (has_resistance_loop) {
    std::vector<Eigen::Triplet<double>> conductance_triplet_list;
    for (ParasiticResistor& parasitic_resistor : parasitic_net.get_resistor_list()) {
      if (node_idx_map.count(parasitic_resistor.get_source_node()) == 0 || node_idx_map.count(parasitic_resistor.get_sink_node()) == 0) {
        continue;
      }
      std::size_t source_idx = node_idx_map[parasitic_resistor.get_source_node()];
      std::size_t sink_idx = node_idx_map[parasitic_resistor.get_sink_node()];
      if (source_idx == sink_idx) {
        continue;
      }
      double conductance = 1.0 / (parasitic_resistor.get_resistance() * 1E-3);
      if (source_idx > 0) {
        conductance_triplet_list.emplace_back(source_idx - 1, source_idx - 1, conductance);
      }
      if (sink_idx > 0) {
        conductance_triplet_list.emplace_back(sink_idx - 1, sink_idx - 1, conductance);
      }
      if (source_idx > 0 && sink_idx > 0) {
        conductance_triplet_list.emplace_back(source_idx - 1, sink_idx - 1, -conductance);
        conductance_triplet_list.emplace_back(sink_idx - 1, source_idx - 1, -conductance);
      }
    }
    conductance_matrix.resize(node_num - 1, node_num - 1);
    conductance_matrix.setFromTriplets(conductance_triplet_list.begin(), conductance_triplet_list.end());
    conductance_solver.compute(conductance_matrix);
    has_resistance_loop = conductance_solver.info() == Eigen::Success;
  }

  for (int32_t order_idx = 0; order_idx < order; order_idx++) {
    updateParasiticArnoldiProjection(arnoldi_model, current_basis_list, term_point_idx_list, order_idx);
    if (arnoldi_model.get_projection_list().empty()) {
      arnoldi_model.set_projection_list(projection_list);
    } else {
      projection_list = arnoldi_model.get_projection_list();
    }

    std::vector<double> response_list(node_num, 0.0);
    if (has_resistance_loop) {
      Eigen::VectorXd current_load_vector(node_num - 1);
      for (std::size_t node_idx = 1; node_idx < node_num; node_idx++) {
        current_load_vector[node_idx - 1] = capacitance_list[node_idx] * current_basis_list[node_idx];
      }
      Eigen::VectorXd response_vector = conductance_solver.solve(current_load_vector);
      for (std::size_t node_idx = 1; node_idx < node_num; node_idx++) {
        response_list[node_idx] = response_vector[node_idx - 1];
      }
    } else {
      std::vector<double> current_load_list(node_num, 0.0);
      for (int32_t node_idx = static_cast<int32_t>(node_num) - 1; node_idx > 0; node_idx--) {
        current_load_list[node_idx] += capacitance_list[node_idx] * current_basis_list[node_idx];
        current_load_list[parent_idx_list[node_idx]] += current_load_list[node_idx];
      }
      current_load_list[0] += capacitance_list[0] * current_basis_list[0];
      for (std::size_t node_idx = 1; node_idx < node_num; node_idx++) {
        response_list[node_idx] = response_list[parent_idx_list[node_idx]] + resistance_list[node_idx] * current_load_list[node_idx];
      }
    }

    for (size_t term = 0; term < term_point_idx_list.size(); ++term)
      response_projection[order_idx][term] = response_list[term_point_idx_list[term]];

    double diagonal = 0.0;
    for (std::size_t node_idx = 1; node_idx < node_num; node_idx++) {
      diagonal += current_basis_list[node_idx] * capacitance_list[node_idx] * response_list[node_idx];
    }
    diagonal_list[order_idx] = diagonal;
    if (order_idx == order - 1) {
      break;
    }
    if (diagonal < ista::kTimingTolerance) {
      final_order = order_idx + 1;
      break;
    }

    for (std::size_t node_idx = 0; node_idx < node_num; node_idx++) {
      response_list[node_idx] -= diagonal * current_basis_list[node_idx];
      if (order_idx > 0) {
        response_list[node_idx] -= off_diagonal_list[order_idx - 1] * previous_basis_list[node_idx];
      }
    }

    double off_diagonal = 0.0;
    for (std::size_t node_idx = 0; node_idx < node_num; node_idx++) {
      off_diagonal += capacitance_list[node_idx] * response_list[node_idx] * response_list[node_idx];
    }
    if (off_diagonal < ista::kTimingTolerance * ista::kTimingTolerance) {
      final_order = order_idx + 1;
      break;
    }
    off_diagonal = std::sqrt(off_diagonal);
    off_diagonal_list[order_idx] = off_diagonal;

    previous_basis_list = current_basis_list;
    for (std::size_t node_idx = 0; node_idx < node_num; node_idx++) {
      current_basis_list[node_idx] = response_list[node_idx] / off_diagonal;
    }
  }

  diagonal_list.resize(final_order);
  if (final_order > 1) {
    off_diagonal_list.resize(final_order - 1);
  } else {
    off_diagonal_list.clear();
  }
  projection_list.resize(final_order);
  response_projection.resize(final_order);
  arnoldi_model.setZeroCapacitanceTerms(std::move(zero_capacitance_terms));
  arnoldi_model.setResponseProjection(std::move(response_projection));
  arnoldi_model.set_is_valid(final_order > 0);
  {
    static const char* order_dump = std::getenv("ISTA_DUMP_ARNOLDI_ORDER");
    if (order_dump != nullptr) {
      if (FILE* order_fp = std::fopen(order_dump, "a"); order_fp != nullptr) {
        std::fprintf(order_fp, "%zu,%d,%d\n", node_num, max_order, final_order);
        std::fclose(order_fp);
      }
    }
  }
  arnoldi_model.set_order(final_order);
  arnoldi_model.set_total_capacitance(total_capacitance);
  arnoldi_model.set_sqrt_total_capacitance(sqrt_total_capacitance);
  arnoldi_model.set_diagonal_list(diagonal_list);
  arnoldi_model.set_off_diagonal_list(off_diagonal_list);
  arnoldi_model.set_projection_list(projection_list);
}

void RcModelBuilder::updateParasiticArnoldiProjection(ParasiticArnoldiModel& arnoldi_model, std::vector<double>& basis_list,
                                                      std::vector<std::size_t>& term_point_idx_list, std::size_t order_idx)
{
  if (arnoldi_model.get_projection_list().size() <= order_idx) {
    arnoldi_model.get_projection_list().resize(order_idx + 1);
  }
  arnoldi_model.get_projection_list()[order_idx].clear();
  for (std::size_t term_point_idx : term_point_idx_list) {
    arnoldi_model.get_projection_list()[order_idx].push_back(basis_list[term_point_idx]);
  }
}

}  // namespace ista

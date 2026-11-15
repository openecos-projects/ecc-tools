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
#include "CellDelayCalculator.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <ranges>
#include <unordered_map>
#include <string>
#include <vector>

#include "TimingTolerance.hpp"

namespace ista {
extern thread_local const char* g_dump_source_pin_cstr;

namespace
{
// Diagnostic only: the CEFF dump needs the driving (source) pin so multi-input
// cells can be paired arc by arc. TimingArc carries no pin name, so the caller
// that still holds the Arc publishes it here for the dump below.
thread_local const std::string* g_dump_source_pin = nullptr;

// Diagnostic: reference's converged slew per (pin, edge). Loaded once.
const std::unordered_map<std::string, double>& forcedRefSlew()
{
  static const auto values = [] {
    std::unordered_map<std::string, double> map;
    const char* path = std::getenv("ISTA_FORCE_REF_SLEW");
    if (path != nullptr) {
      if (FILE* f = std::fopen(path, "r"); f != nullptr) {
        char pin[512];
        int trans = 0;
        double slew = 0.;
        while (std::fscanf(f, "%511[^,],%d,%lf\n", pin, &trans, &slew) == 3)
          map[std::string(pin) + "," + std::to_string(trans)] = slew;
        std::fclose(f);
      }
    }
    return map;
  }();
  return values;
}

const std::unordered_map<std::string, double>& forcedRefSlew4()
{
  static const auto values = [] {
    std::unordered_map<std::string, double> map;
    const char* path = std::getenv("ISTA_FORCE_REF_SLEW4");
    if (path != nullptr) {
      if (FILE* f = std::fopen(path, "r"); f != nullptr) {
        char pin[512];
        int trans = 0;
        int analysis = 0;
        double slew = 0.;
        while (std::fscanf(f, "%511[^,],%d,%d,%lf\n", pin, &trans, &analysis, &slew) == 4)
          map[std::string(pin) + "," + std::to_string(trans) + "," + std::to_string(analysis)] = slew;
        std::fclose(f);
      }
    }
    return map;
  }();
  return values;
}

std::optional<double> forcedRefSlewFor(const std::string& pin, TransType edge)
{
  const auto& map = forcedRefSlew();
  if (map.empty())
    return std::nullopt;
  const auto it = map.find(pin + "," + std::to_string(static_cast<int>(edge)));
  if (it == map.end() || !(it->second > 0.))
    return std::nullopt;
  return it->second;
}

std::optional<double> forcedRefSlew4For(const std::string& pin, TransType edge, AnalysisType analysis_type)
{
  const auto& map = forcedRefSlew4();
  if (map.empty())
    return std::nullopt;
  const auto it = map.find(pin + "," + std::to_string(static_cast<int>(edge)) + "," + std::to_string(static_cast<int>(analysis_type)));
  if (it == map.end() || !(it->second > 0.))
    return std::nullopt;
  return it->second;
}

std::optional<double> forcedSlewAny(const std::string& pin, TransType edge, AnalysisType analysis_type)
{
  if (const auto v = forcedRefSlew4For(pin, edge, analysis_type))
    return v;
  return forcedRefSlewFor(pin, edge);
}
}  // namespace

bool CellDelayCalculator::calculateDrivingCell(std::string& output_pin, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType output_trans_type,
                                               double input_transition_rise, double input_transition_fall, DelayResult& timing_result)
{
  timing_result.set_output_trans_type(output_trans_type);
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    timing_result.set_delay(0.0);
    timing_result.set_slew(output_trans_type == TransType::kRise ? input_transition_rise : input_transition_fall);
    return true;
  }

  const double output_load = _load.pinLoad(output_pin, analysis_type, output_trans_type);
  TimingArc* selected_arc = nullptr;
  double selected_input_slew = 0.;
  double selected_table_delay = 0.;
  for (TransType input_trans_type : {TransType::kRise, TransType::kFall}) {
    const double input_slew = input_trans_type == TransType::kRise ? input_transition_rise : input_transition_fall;
    for (TimingArc* timing_arc : _selection.cellCandidates(timing_cell_arc, input_trans_type, output_trans_type, output_pin)) {
      if (!timing_arc->get_delay_table_map().contains(output_trans_type) && !timing_arc->get_slew_table_map().contains(output_trans_type))
        continue;
      // Choose the external drive condition from its library load delay.
      // A fitted waveform has a small delay residual, which can reverse the
      // ordering of weak/strong conditional arcs after intrinsic subtraction.
      const double load_delay = calcTimingArcDelayByLoad(*timing_arc, output_trans_type, input_slew, output_load)
                                - calcTimingArcDelayByLoad(*timing_arc, output_trans_type, input_slew, 0.);
      if (!selected_arc || (analysis_type == AnalysisType::kMin && load_delay < selected_table_delay)
          || (analysis_type == AnalysisType::kMax && load_delay > selected_table_delay)) {
        selected_arc = timing_arc;
        selected_input_slew = input_slew;
        selected_table_delay = load_delay;
      }
    }
  }
  if (!selected_arc)
    return false;
  double load_delay = 0.;
  if (selected_arc->get_delay_table_map().contains(output_trans_type)) {
    const double loaded_delay = calcTimingArcDelay(output_pin, *selected_arc, analysis_type, output_trans_type, selected_input_slew, output_load);
    const double intrinsic_delay = calcTimingArcDelayByLoad(*selected_arc, output_trans_type, selected_input_slew, 0.);
    load_delay = loaded_delay - intrinsic_delay;
  }
  timing_result.set_delay(load_delay);
  const double output_slew = selected_arc->get_slew_table_map().contains(output_trans_type)
                                 ? calcTimingArcSlew(output_pin, *selected_arc, analysis_type, output_trans_type, selected_input_slew, output_load)
                                 : selected_input_slew;
  timing_result.set_slew(output_slew);
  if (const auto forced = forcedSlewAny(output_pin, output_trans_type, analysis_type))
    timing_result.set_slew(*forced);
  return true;
}

double CellDelayCalculator::calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type)
{
  return calcTimingCellArcDelay(arc, timing_cell_arc, analysis_type, TransType::kRise);
}

double CellDelayCalculator::calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    arc.get_trans_type_map()[input_trans_type] = input_trans_type;
    if (analysis_type == AnalysisType::kMin) {
      return timing_cell_arc.get_delay_min();
    }
    return timing_cell_arc.get_delay_max();
  }
  TransType output_trans_type = _selection.getOutputTransType(timing_cell_arc, input_trans_type);
  arc.get_trans_type_map()[input_trans_type] = output_trans_type;
  if (!_selection.isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return timing_cell_arc.get_delay();
  }
  double input_slew = 0.0;
  double raw_output_load = _load.arcLoad(arc, analysis_type, output_trans_type);
  std::vector<double> delay_list;
  for (TimingArc* timing_arc : _selection.cellCandidates(timing_cell_arc, input_trans_type, output_trans_type, arc.get_sink_pin())) {
    if (timing_arc->get_delay_table_map().count(output_trans_type) == 0) {
      continue;
    }
    {
      static const char* dp = std::getenv("ISTA_DUMP_DELAY_SLEW");
      if (dp != nullptr) {
        if (FILE* fp2 = std::fopen(dp, "a"); fp2 != nullptr) {
          std::fprintf(fp2, "%s,%s,%d,%d,%d,%.12g,%.12g\n", arc.get_sink_pin().c_str(),
                       g_dump_source_pin != nullptr ? g_dump_source_pin->c_str() : "",
                       static_cast<int>(analysis_type), static_cast<int>(output_trans_type),
                       timing_arc->get_when_expression().get_is_empty() ? 0 : 1, input_slew, raw_output_load);
          std::fclose(fp2);
        }
      }
    }
    double effective_slew = input_slew;
    {
      // A missing propagation state surfaces as slew == 0, which the table then
      // reads at its leftmost slew column. Diagnostic fallback for measurement.
      static const bool slew_fallback = [] {
        const char* e = std::getenv("ISTA_SLEW_ZERO_FALLBACK");
        return e != nullptr && std::atoi(e) != 0;
      }();
      if (slew_fallback && !(effective_slew > 0.)) {
        if (const auto forced = forcedSlewAny(arc.get_source_pin(), input_trans_type, analysis_type))
          effective_slew = *forced;
      }
    }
    double delay = calcTimingArcDelay(arc.get_sink_pin(), *timing_arc, analysis_type, output_trans_type, effective_slew, raw_output_load);
    updateTimingArcDelay(arc, *timing_arc, analysis_type, input_trans_type, output_trans_type, delay, false);
    delay_list.push_back(delay);
  }
  if (delay_list.empty()) {
    return timing_cell_arc.get_delay();
  }
  std::ranges::sort(delay_list, std::greater<double>());
  if (analysis_type == AnalysisType::kMin) {
    return delay_list.back();
  }
  return delay_list.front();
}

double CellDelayCalculator::calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                                                   TransType output_trans_type)
{
  return calcTimingCellArcDelay(arc, timing_cell_arc, analysis_type, input_trans_type, output_trans_type, 0.0);
}

double CellDelayCalculator::calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                                                   TransType output_trans_type, double input_slew, bool is_initialization)
{
  const std::string& arc_source_pin = arc.get_source_pin();
  g_dump_source_pin = &arc_source_pin;
  g_dump_source_pin_cstr = arc_source_pin.c_str();
  static const char* dump_path = std::getenv("ISTA_DUMP_CELL_ARC");
  if (dump_path != nullptr) {
    const double dump_load = _load.arcLoad(arc, analysis_type, output_trans_type);
    if (FILE* dump_fp = std::fopen(dump_path, "a"); dump_fp != nullptr) {
      std::fprintf(dump_fp, "%s,%s,%s,%d,%d,%.12f,%.12f\n", arc.get_sink_pin().c_str(), arc.get_source_pin().c_str(), arc.get_owner_name().c_str(),
                   static_cast<int>(analysis_type), static_cast<int>(output_trans_type), input_slew, dump_load);
      std::fclose(dump_fp);
    }
  }
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    if (analysis_type == AnalysisType::kMin) {
      return timing_cell_arc.get_delay_min();
    }
    return timing_cell_arc.get_delay_max();
  }
  if (!_selection.isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return timing_cell_arc.get_delay();
  }
  double raw_output_load = _load.arcLoad(arc, analysis_type, output_trans_type);
  {
    static const char* cl_env = std::getenv("ISTA_FORCE_CELL_LOAD");
    static const std::unordered_map<std::string, double> cl_map = [] {
      std::unordered_map<std::string, double> m;
      if (cl_env == nullptr) return m;
      FILE* f = std::fopen(cl_env, "r");
      if (f == nullptr) return m;
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
        if (part.size() < 4) continue;
        m[part[0] + "," + part[1] + "," + part[2]] = std::atof(part[3].c_str());
      }
      std::fclose(f);
      return m;
    }();
    if (!cl_map.empty()) {
      const std::string key = arc.get_sink_pin() + "," + std::to_string(static_cast<int>(analysis_type)) + "," +
                              std::to_string(static_cast<int>(output_trans_type));
      const auto it = cl_map.find(key);
      if (it != cl_map.end() && it->second > 0.) {
        raw_output_load = it->second;
        if (const char* fd = std::getenv("ISTA_DUMP_CELL_LOAD")) {
          if (FILE* fp = std::fopen(fd, "a"); fp != nullptr) {
            std::fprintf(fp, "CLO,%s,%d,%d,%.15g\n", arc.get_sink_pin().c_str(), static_cast<int>(analysis_type),
                         static_cast<int>(output_trans_type), raw_output_load);
            std::fclose(fp);
          }
        }
      }
    }
  }
  std::vector<double> delay_list;
  for (TimingArc* timing_arc : _selection.cellCandidates(timing_cell_arc, input_trans_type, output_trans_type, arc.get_sink_pin())) {
    if (timing_arc->get_delay_table_map().count(output_trans_type) == 0) {
      continue;
    }
    {
      // Diagnostic: which (input edge, output edge, slew) triple reaches the table.
      static const char* td = std::getenv("ISTA_DUMP_TRIPLE");
      if (td != nullptr) {
        if (FILE* f = std::fopen(td, "a"); f != nullptr) {
          std::fprintf(f, "%s,%s,%d,%d,%d,%.12g,%.12g\n", arc.get_sink_pin().c_str(),
                       g_dump_source_pin != nullptr ? g_dump_source_pin->c_str() : "",
                       static_cast<int>(input_trans_type), static_cast<int>(output_trans_type),
                       static_cast<int>(timing_arc->get_sense()), input_slew, raw_output_load);
          std::fclose(f);
        }
      }
    }
    double delay = calcTimingArcDelay(arc.get_sink_pin(), *timing_arc, analysis_type, output_trans_type, input_slew, raw_output_load);
    updateTimingArcDelay(arc, *timing_arc, analysis_type, input_trans_type, output_trans_type, delay, is_initialization,
                         !(input_slew > 0.));
    delay_list.push_back(delay);
  }
  if (delay_list.empty()) {
    return timing_cell_arc.get_delay();
  }
  std::ranges::sort(delay_list, std::greater<double>());
  if (analysis_type == AnalysisType::kMin) {
    return delay_list.back();
  }
  return delay_list.front();
}

void CellDelayCalculator::updateTimingArcDelay(Arc& arc, TimingArc& timing_arc, AnalysisType analysis_type, TransType input_trans_type,
                                               TransType output_trans_type, double delay, bool is_initialization, bool is_zero_slew_probe)
{
  arc.update_timing_arc_delay(timing_arc.get_arc_idx(), analysis_type, input_trans_type, output_trans_type, delay, is_initialization,
                             is_zero_slew_probe);
}

double CellDelayCalculator::calcTimingCellArcSlew(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                                                  TransType output_trans_type, double input_slew)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    return input_slew;
  }
  if (!_selection.isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return input_slew;
  }
  double output_load = _load.arcLoad(arc, analysis_type, output_trans_type);
  {
    // The Elmore arc load and the Arnoldi effective capacitance differ on
    // large nets (measured up to 20+ fF on salsa20). Prefer the Ceff that the
    // network solve actually converged to when it is available.
    static const bool use_ceff_load = [] {
      const char* e = std::getenv("ISTA_CELL_USE_CEFF_LOAD");
      return e != nullptr && std::atoi(e) != 0;
    }();
    if (use_ceff_load) {
      const auto ceff = _arnoldi.effectiveCapacitance(arc, analysis_type, output_trans_type);
      if (ceff && *ceff > 0.) {
        output_load = *ceff;
      }
    }
  }
  std::vector<TimingArc*> candidate_arc_list = _selection.cellCandidates(timing_cell_arc, input_trans_type, output_trans_type, arc.get_sink_pin());
  std::vector<double> slew_list;
  for (TimingArc* timing_arc : candidate_arc_list) {
    if (timing_arc->get_slew_table_map().count(output_trans_type) == 0) {
      continue;
    }
    double slew = calcTimingArcSlew(arc.get_sink_pin(), *timing_arc, analysis_type, output_trans_type, input_slew, output_load);
    slew_list.push_back(slew);
  }
  if (slew_list.empty()) {
    return input_slew;
  }
  std::ranges::sort(slew_list, std::greater<double>());
  double result = analysis_type == AnalysisType::kMin ? slew_list.back() : slew_list.front();
  {
    // Diagnostic: quantify how much of the cell-arc residual comes from the
    // slew the network solve produced. Overrides with reference's converged
    // slew for the same sink pin and edge.
    if (const auto forced = forcedSlewAny(arc.get_sink_pin(), output_trans_type, analysis_type))
      result = *forced;
  }
  return result;
}

double CellDelayCalculator::calcTimingCellArcDelay(TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                                                   TransType output_trans_type, double input_slew, double output_load)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    if (analysis_type == AnalysisType::kMin) {
      return timing_cell_arc.get_delay_min();
    }
    return timing_cell_arc.get_delay_max();
  }
  if (!_selection.isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return timing_cell_arc.get_delay();
  }
  std::vector<double> delay_list;
  for (TimingArc* timing_arc : _selection.cellCandidates(timing_cell_arc, input_trans_type, output_trans_type)) {
    if (timing_arc->get_delay_table_map().count(output_trans_type) == 0) {
      continue;
    }
    double delay = calcTimingArcDelayByLoad(*timing_arc, output_trans_type, input_slew, output_load);
    delay_list.push_back(delay);
  }
  if (delay_list.empty()) {
    return timing_cell_arc.get_delay();
  }
  std::ranges::sort(delay_list, std::greater<double>());
  if (analysis_type == AnalysisType::kMin) {
    return delay_list.back();
  }
  return delay_list.front();
}

double CellDelayCalculator::calcTimingCellArcSlew(TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                                                  TransType output_trans_type, double input_slew, double output_load)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    return input_slew;
  }
  if (!_selection.isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return input_slew;
  }
  std::vector<TimingArc*> candidate_arc_list = _selection.cellCandidates(timing_cell_arc, input_trans_type, output_trans_type);
  std::vector<double> slew_list;
  for (TimingArc* timing_arc : candidate_arc_list) {
    if (timing_arc->get_slew_table_map().count(output_trans_type) == 0) {
      continue;
    }
    double slew = calcTimingArcSlewByLoad(*timing_arc, output_trans_type, input_slew, output_load);
    slew_list.push_back(slew);
  }
  if (slew_list.empty()) {
    return input_slew;
  }
  std::ranges::sort(slew_list, std::greater<double>());
  if (analysis_type == AnalysisType::kMin) {
    return slew_list.back();
  }
  return slew_list.front();
}

double CellDelayCalculator::convertOutputLoad(TimingArc& timing_arc, double output_load)
{
  if (std::abs(timing_arc.get_cap_unit_scale() - 1.0) < ista::kTimingTolerance) {
    return output_load;
  }
  return static_cast<int>(std::ceil(output_load * timing_arc.get_cap_unit_scale()));
}

double CellDelayCalculator::calcTimingArcDelay(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type,
                                               double input_slew, double output_load)
{
  DriverResponse& timing_result = _arnoldi.response(output_pin, timing_arc, analysis_type, output_trans_type, input_slew);
  static const bool cell_delay_from_table = [] {
    const char* e = std::getenv("ISTA_CELL_DELAY_FROM_TABLE");
    return e != nullptr && *e != '0';
  }();
  if (cell_delay_from_table && timing_result.get_is_valid()) {
    const double table_load = timing_result.get_effective_capacitance() > 0. ? timing_result.get_effective_capacitance() : output_load;
    const double table_delay = calcTimingArcDelayByLoad(timing_arc, output_trans_type, input_slew, table_load);
    if (const char* td = std::getenv("ISTA_DUMP_CELL_TABLE")) {
      if (FILE* tf = std::fopen(td, "a"); tf != nullptr) {
        std::fprintf(tf, "CT,%s,%d,%d,%.15g,%.15g,%.15g,%.15g\n", output_pin.c_str(), static_cast<int>(analysis_type),
                     static_cast<int>(output_trans_type), input_slew, table_load, table_delay, timing_result.get_gate_delay());
        std::fclose(tf);
      }
    }
    return table_delay;
  }
  static const double degenerate_cap = [] {
    const char* e = std::getenv("ISTA_DEGENERATE_NET_CAP");
    return e != nullptr ? std::atof(e) : 1e-6;
  }();
  {
    static const char* gd = std::getenv("ISTA_DUMP_DEGEN_CAND");
    if (gd != nullptr && output_pin.find("fixio") == 0 && timing_result.get_is_valid()) {
      if (FILE* gf = std::fopen(gd, "a"); gf != nullptr) {
        std::fprintf(gf, "GC,%s,%d,%d,inslew=%.15g,output_load=%.15g,effcap=%.15g,delay=%.15g\n", output_pin.c_str(),
                     static_cast<int>(analysis_type), static_cast<int>(output_trans_type), input_slew, output_load,
                     timing_result.get_effective_capacitance(), timing_result.get_gate_delay());
        std::fclose(gf);
      }
    }
  }
  if (degenerate_cap > 0. && timing_result.get_is_valid()) {
    const double solved_cap = timing_result.get_effective_capacitance();
    // The load the caller resolved for this arc is the net's own total capacitance;
    // DriverResponse does not carry it, so compare that instead of the solved Ceff.
    const double total_cap = output_load;
    if (solved_cap >= 0. && total_cap >= 0. && total_cap <= degenerate_cap) {
      static const char* dd = std::getenv("ISTA_DUMP_DEGENERATE");
      if (dd != nullptr) {
        if (FILE* df = std::fopen(dd, "a"); df != nullptr) {
          std::fprintf(df, "DG,%s,%d,%d,%.15g,%.15g,%.15g,%.15g\n", output_pin.c_str(), static_cast<int>(analysis_type),
                       static_cast<int>(output_trans_type), input_slew, solved_cap, total_cap, timing_result.get_gate_delay());
          std::fclose(df);
        }
      }
      return calcTimingArcDelayByLoad(timing_arc, output_trans_type, input_slew, solved_cap);
    }
  }
  if (timing_result.get_is_valid()) {
    static const char* dp1 = std::getenv("ISTA_DUMP_DISPATCH");
    if (dp1 != nullptr) {
      if (FILE* d1 = std::fopen(dp1, "a"); d1 != nullptr) {
        std::fprintf(d1, "%s,%s,%d,%d,%.12g,1,%.12g\n", output_pin.c_str(),
                     g_dump_source_pin != nullptr ? g_dump_source_pin->c_str() : "", static_cast<int>(analysis_type),
                     static_cast<int>(output_trans_type), input_slew, timing_result.get_gate_delay());
        std::fclose(d1);
      }
    }
    static const char* ac_dump = std::getenv("ISTA_DUMP_ARNOLDI_CEFF");
    if (ac_dump != nullptr) {
      if (FILE* ac_fp = std::fopen(ac_dump, "a"); ac_fp != nullptr) {
        std::fprintf(ac_fp, "%s,%s,%d,%d,%.12g,%.12g,%.12g\n", output_pin.c_str(),
                     g_dump_source_pin != nullptr ? g_dump_source_pin->c_str() : "", static_cast<int>(analysis_type),
                     static_cast<int>(output_trans_type), input_slew, timing_result.get_effective_capacitance(), timing_result.get_gate_delay());

        std::fclose(ac_fp);
      }
    }
    return timing_result.get_gate_delay();
  }
  DriverResponse& dmp_timing_result = _dmp.response(output_pin, timing_arc, analysis_type, output_trans_type, input_slew, output_load);
  if (dmp_timing_result.get_is_valid()) {
    static const char* dp_dump = std::getenv("ISTA_DUMP_DISPATCH");
    if (dp_dump != nullptr) {
      if (FILE* dp_fp = std::fopen(dp_dump, "a"); dp_fp != nullptr) {
        std::fprintf(dp_fp, "%s,%s,%d,%d,%.12g,2,%.12g\n", output_pin.c_str(),
                     g_dump_source_pin != nullptr ? g_dump_source_pin->c_str() : "", static_cast<int>(analysis_type),
                     static_cast<int>(output_trans_type), input_slew, dmp_timing_result.get_gate_delay());
        std::fclose(dp_fp);
      }
    }
    return dmp_timing_result.get_gate_delay();
  }
  {
    static const char* dp_dump = std::getenv("ISTA_DUMP_DISPATCH");
    if (dp_dump != nullptr) {
      if (FILE* dp_fp = std::fopen(dp_dump, "a"); dp_fp != nullptr) {
        std::fprintf(dp_fp, "%s,%s,%d,%d,%.12g,3,%.12g\n", output_pin.c_str(),
                     g_dump_source_pin != nullptr ? g_dump_source_pin->c_str() : "", static_cast<int>(analysis_type),
                     static_cast<int>(output_trans_type), input_slew,
                     calcTimingArcDelayByLoad(timing_arc, output_trans_type, input_slew, output_load));
        std::fclose(dp_fp);
      }
    }
  }
  return calcTimingArcDelayByLoad(timing_arc, output_trans_type, input_slew, output_load);
}

double CellDelayCalculator::calcTimingArcSlew(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type,
                                              double input_slew, double output_load)
{
  static const char* slew_dump_path = std::getenv("ISTA_DUMP_CELL_SLEW");
  int slew_source = 0;
  double slew_value = 0.0;
  DriverResponse& timing_result = _arnoldi.response(output_pin, timing_arc, analysis_type, output_trans_type, input_slew);
  static const bool cell_delay_from_table_slew = std::getenv("ISTA_CELL_DELAY_FROM_TABLE") != nullptr
                                                 && *std::getenv("ISTA_CELL_DELAY_FROM_TABLE") != '0';
  if (cell_delay_from_table_slew && timing_result.get_is_valid()) {
    static const bool slew_from_table = [] {
      const char* e = std::getenv("ISTA_CELL_SLEW_FROM_TABLE");
      return e != nullptr && *e != '0';
    }();
    if (slew_from_table) {
      const double table_load = timing_result.get_effective_capacitance() > 0. ? timing_result.get_effective_capacitance() : output_load;
      slew_value = calcTimingArcSlewByLoad(timing_arc, output_trans_type, input_slew, table_load);
      slew_source = 4;
    } else {
      slew_value = timing_result.get_driver_slew();
      slew_source = 1;
    }
  } else if (timing_result.get_is_valid()) {
    slew_value = timing_result.get_driver_slew();
    slew_source = 1;
  } else {
    DriverResponse& dmp_timing_result = _dmp.response(output_pin, timing_arc, analysis_type, output_trans_type, input_slew, output_load);
    if (dmp_timing_result.get_is_valid()) {
      slew_value = dmp_timing_result.get_driver_slew();
      slew_source = 2;
    } else {
      slew_value = calcTimingArcSlewByLoad(timing_arc, output_trans_type, input_slew, output_load);
      slew_source = 3;
    }
  }
  if (slew_dump_path != nullptr) {
    if (FILE* slew_fp = std::fopen(slew_dump_path, "a"); slew_fp != nullptr) {
      std::fprintf(slew_fp, "%s,%d,%d,%d,%d,%.12f,%.12f,%.12f,%d,%s,%s\n", output_pin.c_str(), timing_arc.get_arc_idx(),
                   static_cast<int>(analysis_type), static_cast<int>(output_trans_type),
                   static_cast<int>(timing_arc.get_trigger_trans_type()), input_slew, output_load, slew_value, slew_source,
                   g_dump_source_pin != nullptr ? g_dump_source_pin->c_str() : "", timing_arc.get_when_expression().get_is_empty() ? "N" : "C");
      std::fclose(slew_fp);
    }
  }
  return slew_value;
}

double CellDelayCalculator::calcTimingArcDelayByLoad(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load)
{
  if (timing_arc.get_delay_table_map().count(output_trans_type) == 0) {
    return 0.0;
  }
  double converted_output_load = convertOutputLoad(timing_arc, output_load);
  double delay = timing_arc.get_delay_table_map()[output_trans_type].findValue(input_slew * timing_arc.get_time_unit_scale(), converted_output_load);
  {
    // Diagnostic only: the two inputs of the NLDM cell-delay lookup. Reference
    // reports both numbers, so this dump makes the cell-arc comparison direct.
    static const char* cp = std::getenv("ISTA_DUMP_CELLLOOKUP");
    if (cp != nullptr) {
      if (FILE* fp = std::fopen(cp, "a"); fp != nullptr) {
        std::fprintf(fp, "CL,%s,%d,%d,%.12g,%.12g,%.12g,%.12g\n", g_dump_source_pin != nullptr ? g_dump_source_pin->c_str() : "",
                     static_cast<int>(output_trans_type), static_cast<int>(timing_arc.get_trigger_trans_type()), input_slew,
                     converted_output_load, output_load, delay / timing_arc.get_time_unit_scale());
        std::fclose(fp);
      }
    }
  }
  return delay / timing_arc.get_time_unit_scale();
}

double CellDelayCalculator::calcTimingArcSlewByLoad(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load)
{
  if (timing_arc.get_slew_table_map().count(output_trans_type) == 0) {
    return input_slew;
  }
  double converted_output_load = convertOutputLoad(timing_arc, output_load);
  double slew = timing_arc.get_slew_table_map()[output_trans_type].findValue(input_slew * timing_arc.get_time_unit_scale(), converted_output_load);
  return slew / timing_arc.get_time_unit_scale();
}

double CellDelayCalculator::calcTimingCellArcDelay(std::string& output_pin, TimingCellArc& timing_cell_arc, AnalysisType analysis_type,
                                                   TransType input_trans_type, TransType output_trans_type, double input_slew)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    if (analysis_type == AnalysisType::kMin) {
      return timing_cell_arc.get_delay_min();
    }
    return timing_cell_arc.get_delay_max();
  }
  if (!_selection.isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return timing_cell_arc.get_delay();
  }
  double output_load = _load.pinLoad(output_pin, analysis_type, output_trans_type);
  std::vector<double> delay_list;
  for (TimingArc* timing_arc : _selection.cellCandidates(timing_cell_arc, input_trans_type, output_trans_type, output_pin)) {
    if (timing_arc->get_delay_table_map().count(output_trans_type) == 0) {
      continue;
    }
    {
      static const char* dp = std::getenv("ISTA_DUMP_DELAY_SLEW");
      if (dp != nullptr) {
        if (FILE* fp2 = std::fopen(dp, "a"); fp2 != nullptr) {
          std::fprintf(fp2, "%s,%d,%d,%d,%.12g,%.12g\n", output_pin.c_str(),
                       static_cast<int>(analysis_type), static_cast<int>(output_trans_type),
                       timing_arc->get_when_expression().get_is_empty() ? 0 : 1, input_slew, output_load);
          std::fclose(fp2);
        }
      }
    }
    double delay = calcTimingArcDelay(output_pin, *timing_arc, analysis_type, output_trans_type, input_slew, output_load);
    delay_list.push_back(delay);
  }
  if (delay_list.empty()) {
    return timing_cell_arc.get_delay();
  }
  std::ranges::sort(delay_list, std::greater<double>());
  if (analysis_type == AnalysisType::kMin) {
    return delay_list.back();
  }
  return delay_list.front();
}

double CellDelayCalculator::calcTimingCellArcSlew(std::string& output_pin, TimingCellArc& timing_cell_arc, AnalysisType analysis_type,
                                                  TransType input_trans_type, TransType output_trans_type, double input_slew)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    return input_slew;
  }
  if (!_selection.isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return input_slew;
  }
  double output_load = _load.pinLoad(output_pin, analysis_type, output_trans_type);
  std::vector<double> slew_list;
  for (TimingArc* timing_arc : _selection.cellCandidates(timing_cell_arc, input_trans_type, output_trans_type, output_pin)) {
    if (timing_arc->get_slew_table_map().count(output_trans_type) == 0) {
      continue;
    }
    double slew = calcTimingArcSlew(output_pin, *timing_arc, analysis_type, output_trans_type, input_slew, output_load);
    slew_list.push_back(slew);
  }
  if (slew_list.empty()) {
    return input_slew;
  }
  std::ranges::sort(slew_list, std::greater<double>());
  if (analysis_type == AnalysisType::kMin) {
    return slew_list.back();
  }
  return slew_list.front();
}

}  // namespace ista

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
#include "WireDelayCalculator.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>

#include "Logger.hpp"

namespace ista {

namespace {
void dumpBranch(Arc& arc, AnalysisType analysis_type, TransType trans_type, const char* tag, double v)
{
  static int seq = 0;
  ++seq;
  static FILE* bf = [] {
    const char* br = std::getenv("ISTA_DUMP_BRANCH");
    return br != nullptr ? std::fopen(br, "a") : nullptr;
  }();
  if (bf == nullptr)
    return;
  std::fprintf(bf, "B,%d,%s,%s,%d,%d,%s,%.15g\n", seq, arc.get_source_pin().c_str(), arc.get_sink_pin().c_str(),
               static_cast<int>(analysis_type), static_cast<int>(trans_type), tag, v);
  std::fflush(bf);
}
}  // namespace

void WireDelayCalculator::markFailed(Arc& arc, AnalysisType analysis, TransType edge)
{
  if (!arc.rcFailed(analysis, edge)) {
    STALOG.warn(Loc::current(), "RC receiver crossing failed: ", arc.get_source_pin(), " -> ", arc.get_sink_pin(),
                analysis == AnalysisType::kMin ? " min " : " max ", edge == TransType::kRise ? "rise" : "fall",
                "; nonbounding RC fallback: zero delay and source slew; timing path retained");
  }
  arc.setRcFailed(analysis, edge);
}

double WireDelayCalculator::calcNetArcSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew)
{
  Database& database = _database;
  if (!database.parasitics().nets().contains(arc.get_owner_name())) {
    const auto source = database.network().pins().find(arc.get_source_pin());
    // Without a wire model, propagate the port waveform directly. A disabled
    // bidirectional load does not supply a physical source voltage domain.
    if (source != database.network().pins().end() && source->second.get_is_port())
      return input_slew;
  }
  if (_rc.hasDisabledDriver(arc.get_owner_name(), arc.get_source_pin())) {
    double unused_delay = 0., slew = input_slew;
    _threshold.adjustSignalLevel(arc.get_source_pin(), arc.get_sink_pin(), analysis_type, trans_type, unused_delay, slew);
    return slew;
  }
  if (database.parasitics().nets().count(arc.get_owner_name()) > 0) {
    return calcParasiticSlew(arc, analysis_type, trans_type, input_slew);
  }
  return input_slew;
}

double WireDelayCalculator::calcParasiticSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew)
{
  Database& database = _database;
  ParasiticNet& parasitic_net = database.parasitics().nets()[arc.get_owner_name()];

  if (_rc.hasIncompleteNetwork(parasitic_net, arc.get_source_pin())) {
    double unused_delay = 0., slew = input_slew;
    _threshold.adjustSignalLevel(arc.get_source_pin(), arc.get_sink_pin(), analysis_type, trans_type, unused_delay, slew);
    return slew;
  }
  dumpBranch(arc, analysis_type, trans_type, "M2", 1.);
  std::string source_node_name = _rc.nodeName(parasitic_net, arc.get_source_pin());
  std::string sink_node_name = _rc.nodeName(parasitic_net, arc.get_sink_pin());
  dumpBranch(arc, analysis_type, trans_type, "M3", source_node_name.empty() ? 0. : 1.);
  if (source_node_name.empty() || sink_node_name.empty()) {
    return input_slew;
  }

  dumpBranch(arc, analysis_type, trans_type, "M4", 1.);
  if (_arnoldi.loadFailed(arc, analysis_type, trans_type, input_slew) || _dmp.loadFailed(arc, analysis_type, trans_type, input_slew)) {
    markFailed(arc, analysis_type, trans_type);
    return input_slew;  // Propagate a flagged fallback, preserving the source slew.
  }
  std::optional<double> cached_load_slew = _arnoldi.loadSlew(arc, analysis_type, trans_type, input_slew);
  {
    static const char* sp = std::getenv("ISTA_DUMP_SLEWSRC");
    if (sp != nullptr) {
      if (FILE* fp = std::fopen(sp, "a"); fp != nullptr) {
        std::fprintf(fp, "SS,%s,%s,%d,%d,%.15g,arnoldi=%d,%.15g\n", arc.get_source_pin().c_str(),
                     arc.get_sink_pin().c_str(), static_cast<int>(analysis_type), static_cast<int>(trans_type),
                     input_slew, cached_load_slew ? 1 : 0, cached_load_slew ? *cached_load_slew : 0.0);
        std::fclose(fp);
      }
    }
  }
  if (cached_load_slew) {
    return *cached_load_slew;
  }
  cached_load_slew = _dmp.loadSlew(arc, analysis_type, trans_type, input_slew);
  if (cached_load_slew) {
    return *cached_load_slew;
  }
  std::optional<double> input_port_slew = _input_port.slew(parasitic_net, source_node_name, sink_node_name, analysis_type, trans_type, input_slew);
  if (input_port_slew) {
    return *input_port_slew;
  }
  if (_input_port.loadFailed(parasitic_net, source_node_name, sink_node_name, analysis_type, trans_type, input_slew)) {
    markFailed(arc, analysis_type, trans_type);
    return input_slew;
  }

  dumpBranch(arc, analysis_type, trans_type, "M7", 1.);
  _rc.analyze(parasitic_net, source_node_name, analysis_type, trans_type);
  auto cached_impulse = _rc.impulse(parasitic_net.get_net_name(), analysis_type, trans_type, sink_node_name);
  if (!cached_impulse)
    return input_slew;
  double impulse = *cached_impulse;
  double output_slew = std::sqrt(input_slew * input_slew + impulse);
  {
    static const char* sp = std::getenv("ISTA_DUMP_SLEWSRC");
    if (sp != nullptr) {
      if (FILE* fp = std::fopen(sp, "a"); fp != nullptr) {
        std::fprintf(fp, "SS,%s,%s,%d,%d,%.15g,sqrt=1,%.15g,impulse=%.15g\n", arc.get_source_pin().c_str(),
                     arc.get_sink_pin().c_str(), static_cast<int>(analysis_type), static_cast<int>(trans_type),
                     input_slew, output_slew, impulse);
        std::fclose(fp);
      }
    }
  }
  if (input_slew < 0.0) {
    return -output_slew;
  }
  return output_slew;
}

double WireDelayCalculator::calcNetArcDelay(Arc& arc)
{
  return calcNetArcDelay(arc, AnalysisType::kMax, TransType::kRise);
}

double WireDelayCalculator::calcNetArcDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type)
{
  return calcNetArcDelay(arc, analysis_type, trans_type, std::numeric_limits<double>::quiet_NaN());
}

double WireDelayCalculator::calcNetArcDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew)
{
  Database& database = _database;
  if (const char* np = std::getenv("ISTA_DUMP_NAD"); np != nullptr) {
    static const char* want = std::getenv("ISTA_DUMP_NAD_NET");
    if (want == nullptr || arc.get_owner_name() == want) {
      if (FILE* f = std::fopen(np, "a"); f != nullptr) {
        std::fprintf(f, "NA,%s,%s,%d,%d,haspar=%d,sl=%.15g\n", arc.get_source_pin().c_str(), arc.get_sink_pin().c_str(),
                     static_cast<int>(analysis_type), static_cast<int>(trans_type),
                     database.parasitics().nets().count(arc.get_owner_name()) > 0 ? 1 : 0, input_slew);
        std::fclose(f);
      }
    }
  }
  if (database.parasitics().nets().count(arc.get_owner_name()) > 0) {
    return calcParasiticDelay(arc, analysis_type, trans_type, input_slew);
  }
  const auto source = database.network().pins().find(arc.get_source_pin());
  if (source != database.network().pins().end() && source->second.get_is_port())
    return 0.;
  if (_rc.hasDisabledDriver(arc.get_owner_name(), arc.get_source_pin())) {
    double delay = 0., slew = input_slew;
    _threshold.adjustSignalLevel(arc.get_source_pin(), arc.get_sink_pin(), analysis_type, trans_type, delay, slew);
    return delay;
  }
  return 0.0;
}

double WireDelayCalculator::calcParasiticDelay(Arc& arc)
{
  return calcParasiticDelay(arc, AnalysisType::kMax, TransType::kRise);
}

double WireDelayCalculator::calcParasiticDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type)
{
  return calcParasiticDelay(arc, analysis_type, trans_type, std::numeric_limits<double>::quiet_NaN());
}

double WireDelayCalculator::calcParasiticDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew)
{

  Database& database = _database;
  ParasiticNet& parasitic_net = database.parasitics().nets()[arc.get_owner_name()];
  dumpBranch(arc, analysis_type, trans_type, "ENTRY", input_slew);
  dumpBranch(arc, analysis_type, trans_type, "M1", 1.);
  if (_rc.hasDisabledDriver(arc.get_owner_name(), arc.get_source_pin()) || _rc.hasIncompleteNetwork(parasitic_net, arc.get_source_pin())) {
    double delay = _rc.balancedDelay(parasitic_net, arc.get_source_pin(), arc.get_sink_pin(), analysis_type, trans_type);
    double slew = input_slew;
    _threshold.adjustSignalLevel(arc.get_source_pin(), arc.get_sink_pin(), analysis_type, trans_type, delay, slew);
    dumpBranch(arc, analysis_type, trans_type, "BALANCED", delay);
    return delay;
  }

  std::string source_node_name = _rc.nodeName(parasitic_net, arc.get_source_pin());
  std::string sink_node_name = _rc.nodeName(parasitic_net, arc.get_sink_pin());
  if (source_node_name.empty() || sink_node_name.empty()) {
    double source_capacitance = _rc.nodeCapacitance(parasitic_net, arc.get_source_pin());
    double sink_capacitance = _rc.nodeCapacitance(parasitic_net, arc.get_sink_pin());
    double resistance = _rc.totalResistance(parasitic_net);
    dumpBranch(arc, analysis_type, trans_type, "EMPTYNODE", resistance * (source_capacitance + sink_capacitance) * 0.5 * 1E-3);
    return resistance * (source_capacitance + sink_capacitance) * 0.5 * 1E-3;
  }

  if (_arnoldi.loadFailed(arc, analysis_type, trans_type, input_slew) || _dmp.loadFailed(arc, analysis_type, trans_type, input_slew)) {
    markFailed(arc, analysis_type, trans_type);
    dumpBranch(arc, analysis_type, trans_type, "LOADFAILED", 0.);
    return 0.;  // Nonbounding placeholder; retain the failure state on the arc.
  }
  // calcParasiticDelay prefers the Arnoldi cache, then DMP, then the input-port
  // solve, then bare Elmore. Only the Arnoldi path has been compared against
  // reference so far. ISTA_WIRE_PREFER_DMP=1 skips the Arnoldi
  // entry so the DMP result can be compared on the same arcs.
  static const bool prefer_dmp = [] {
    const char* e = std::getenv("ISTA_WIRE_PREFER_DMP");
    return e != nullptr && *e != '0';
  }();
  dumpBranch(arc, analysis_type, trans_type, "M5", prefer_dmp ? 1. : 0.);
  std::optional<double> cached_wire_delay;
  if (!prefer_dmp) {
    cached_wire_delay = _arnoldi.wireDelay(arc, analysis_type, trans_type, input_slew);
    if (cached_wire_delay) {
      return *cached_wire_delay;
    }
  }
  cached_wire_delay = _dmp.wireDelay(arc, analysis_type, trans_type, input_slew);
  {
    static const char* sp = std::getenv("ISTA_DUMP_WIRESRC");
    if (sp != nullptr) {
      if (FILE* fp = std::fopen(sp, "a"); fp != nullptr) {
        std::fprintf(fp, "WS,%s,%s,%d,%d,%d,%.15g\n", arc.get_source_pin().c_str(), arc.get_sink_pin().c_str(),
                     static_cast<int>(analysis_type), static_cast<int>(trans_type), cached_wire_delay ? 1 : 0,
                     cached_wire_delay ? *cached_wire_delay : 0.0);
        std::fclose(fp);
      }
    }
  }
  if (cached_wire_delay) {
    dumpBranch(arc, analysis_type, trans_type, "DMP", *cached_wire_delay);
    return *cached_wire_delay;
  }
  dumpBranch(arc, analysis_type, trans_type, "M6", 1.);
  std::optional<double> input_port_delay = _input_port.delay(parasitic_net, source_node_name, sink_node_name, analysis_type, trans_type, input_slew);
  if (input_port_delay) {
    dumpBranch(arc, analysis_type, trans_type, "INPUTPORT", *input_port_delay);
    return *input_port_delay;
  }
  if (_input_port.loadFailed(parasitic_net, source_node_name, sink_node_name, analysis_type, trans_type, input_slew)) {
    markFailed(arc, analysis_type, trans_type);
    dumpBranch(arc, analysis_type, trans_type, "IPORTFAILED", 0.);
    return 0.;
  }

  _rc.analyze(parasitic_net, source_node_name, analysis_type, trans_type);
  {
    double dv = _rc.delay(parasitic_net.get_net_name(), analysis_type, trans_type, sink_node_name).value_or(0.0);
    dumpBranch(arc, analysis_type, trans_type, "RC_ELMORE", dv);
    return dv;
  }
}

}  // namespace ista

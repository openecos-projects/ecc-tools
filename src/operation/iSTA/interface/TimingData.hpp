// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "TimingEndpointQualification.hpp"
#include "TimingMinData.hpp"
#include "SparseTimingSnapshot.hpp"

namespace ista {

// Public value types returned by STAInterface::exportTimingData().
// Strings and containers own their data; no field borrows the STA database.
// Unit suffixes describe already converted values. See README.md for identities
// and the distinction between these values and source-unit scale metadata.

struct LibertyTableData
{
  std::vector<std::vector<double>> axes;
  std::vector<double> values;
};

struct LibertyPortData
{
  std::string name;
  double capacitance_pf = 0.0;
  double rise_capacitance_pf = 0.0;
  double fall_capacitance_pf = 0.0;
  double min_rise_capacitance_pf = 0.0;
  double min_fall_capacitance_pf = 0.0;
  double max_capacitance_pf = 0.0;
  double max_slew_ps = 0.0;
  bool is_input = false;
  bool is_output = false;
  bool is_clock = false;
};

struct LibertyArcData
{
  std::string source_port;
  std::string sink_port;
  int32_t sense = 0;
  int32_t trigger = 0;
  int32_t check_type = 0;
  bool is_latch_data_arc = false;
  LibertyTableData rise_delay;
  LibertyTableData fall_delay;
  LibertyTableData rise_slew;
  LibertyTableData fall_slew;
  LibertyTableData rise_check;
  LibertyTableData fall_check;
};

struct LibertyCellData
{
  std::string name;
  int32_t main_id = -1;
  int32_t size_index = 0;
  int32_t vt_index = 0;
  double area = 0.0;
  double width_dbu = 0.0;
  double height_dbu = 0.0;
  double leakage = 0.0;
  bool is_sequential = false;
  bool is_macro = false;
  bool is_buffer = false;
  std::vector<LibertyPortData> ports;
  std::vector<LibertyArcData> arcs;
};

struct TimingPinData
{
  TimingMaxQualification max_qualification;
  TimingMinQualification min_qualification;
  std::string name;
  std::string net_name;
  std::string instance_name;
  int32_t direction = 0;
  bool is_port = false;
  bool is_unconstrained_output = false;
  bool is_clock = false;
  bool is_sequential = false;
  double capacitance_pf = 0.0;
  double rise_capacitance_pf = 0.0;
  double fall_capacitance_pf = 0.0;
  double max_rise_aat_ps = 0.0;
  double max_fall_aat_ps = 0.0;
  double max_rise_rat_ps = 1.0e30;
  double max_fall_rat_ps = 1.0e30;
  double setup_check_time_ps = 0.0;
  double min_rise_aat_ps = 0.0;
  double min_fall_aat_ps = 0.0;
  double min_rise_rat_ps = 1.0e30;
  double min_fall_rat_ps = 1.0e30;
  double max_rise_slew_ps = 0.0;
  double max_fall_slew_ps = 0.0;
  double min_rise_slew_ps = 0.0;
  double min_fall_slew_ps = 0.0;
  double min_output_rise_load_pf = 0.0;
  double min_output_fall_load_pf = 0.0;
  std::array<bool, 2> min_path_valid{false, false};
  int32_t level = 0;
};

struct TimingGraphArcData
{
  std::string source_pin;
  std::string sink_pin;
  std::string owner_name;
  std::string library_cell;
  int32_t arc_type = 0;
  bool is_clock_arc = false;
  int32_t sense = 0;
  int32_t timing_type = 0;
  int32_t library_arc_id = -1;
  int32_t level = 0;
  double delay_max_ps = 0.0;
  double delay_min_ps = 0.0;
};

struct TimingNetArcData
{
  std::string source_pin;
  std::string sink_pin;
};

struct TimingEndpointArcData
{
  TimingMaxQualification max_qualification;
  TimingMinQualification min_qualification;
  std::string source_pin;
  std::string sink_pin;
  std::string library_cell;
  int32_t library_arc_id = -1;
  int32_t sense = 0;
  int32_t timing_type = 0;
  int32_t check_type = 0;
};

struct TimingClockData
{
  std::string name;
  double period_ps = 0.0;
  double rise_edge_ps = 0.0;
  double fall_edge_ps = 0.0;
  double setup_uncertainty_ps = 0.0;
  double hold_uncertainty_ps = 0.0;
  std::vector<double> waveform_ps;
};

struct TimingData
{
  static constexpr int32_t kSchemaVersion = 2;

  int32_t schema_version = kSchemaVersion;
  // Source-library unit scales, not multipliers for the *_ps / *_pf fields.
  double time_unit_ps = 1.0;
  double capacitance_unit_pf = 1.0;
  double resistance_unit_ohm = 1.0;
  std::string timing_unit_provenance;
  std::string timing_corner;
  std::string sdc_path;
  std::string spef_path;
  std::string parasitic_capacitance_unit;
  std::string parasitic_resistance_unit;
  std::string parasitics_initialization;
  std::vector<std::string> diagnostics;
  std::map<std::string, int64_t> phase_calls;
  std::map<std::string, double> phase_seconds;
  std::vector<TimingPinData> pins;
  std::vector<TimingGraphArcData> graph_arcs;
  std::vector<TimingNetArcData> net_arcs;
  std::vector<TimingEndpointArcData> constraint_arcs;
  std::vector<TimingEndpointArcData> timing_check_arcs;
  std::vector<TimingMinLaunchData> min_launches;
  std::vector<TimingMinContextData> min_contexts;
  std::vector<std::string> start_points;
  std::vector<std::string> end_points;
  std::vector<std::string> clock_pins;
  std::vector<std::string> ff_output_pins;
  std::vector<TimingClockData> clock_metadata;
  std::vector<LibertyCellData> lib_cells;
  std::vector<std::string> clock_names;
  SparseTimingSnapshot sparse;
  SparseTimingSnapshot sparse_min;
};

}  // namespace ista

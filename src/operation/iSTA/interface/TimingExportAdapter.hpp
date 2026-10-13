// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../source/data_manager/advance/TimingEndpointQualification.hpp"

namespace ista {

struct TimingLutSnapshot
{
  std::vector<std::vector<double>> axes;
  std::vector<double> values;
};

struct TimingLibPortSnapshot
{
  std::string name;
  double capacitance_pf = 0.0;
  double rise_capacitance_pf = 0.0;
  double fall_capacitance_pf = 0.0;
  double max_capacitance_pf = 0.0;
  double max_slew_ps = 0.0;
  bool is_input = false;
  bool is_output = false;
  bool is_clock = false;
};

struct TimingLibArcSnapshot
{
  std::string source_port;
  std::string sink_port;
  int32_t sense = 0;
  int32_t trigger = 0;
  int32_t check_type = 0;
  TimingLutSnapshot rise_delay;
  TimingLutSnapshot fall_delay;
  TimingLutSnapshot rise_slew;
  TimingLutSnapshot fall_slew;
  TimingLutSnapshot rise_check;
  TimingLutSnapshot fall_check;
};

struct TimingLibCellSnapshot
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
  std::vector<TimingLibPortSnapshot> ports;
  std::vector<TimingLibArcSnapshot> arcs;
};

struct TimingPinSnapshot
{
  TimingMaxQualification max_qualification;
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
  int32_t level = 0;
};

struct TimingGraphArcSnapshot
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

struct TimingNetArcSnapshot
{
  std::string source_pin;
  std::string sink_pin;
};

struct TimingEndpointArcSnapshot
{
  TimingMaxQualification max_qualification;
  std::string source_pin;
  std::string sink_pin;
  std::string library_cell;
  int32_t library_arc_id = -1;
  int32_t sense = 0;
  int32_t timing_type = 0;
  int32_t check_type = 0;
};

struct TimingClockSnapshot
{
  std::string name;
  double period_ps = 0.0;
  double rise_edge_ps = 0.0;
  double fall_edge_ps = 0.0;
  double setup_uncertainty_ps = 0.0;
  double hold_uncertainty_ps = 0.0;
  std::vector<double> waveform_ps;
};

struct TimingSnapshot
{
  static constexpr int32_t kSchemaVersion = 2;

  int32_t schema_version = kSchemaVersion;
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
  std::vector<TimingPinSnapshot> pins;
  std::vector<TimingGraphArcSnapshot> graph_arcs;
  std::vector<TimingNetArcSnapshot> net_arcs;
  std::vector<TimingEndpointArcSnapshot> constraint_arcs;
  std::vector<TimingEndpointArcSnapshot> timing_check_arcs;
  std::vector<std::string> start_points;
  std::vector<std::string> end_points;
  std::vector<std::string> clock_pins;
  std::vector<std::string> ff_output_pins;
  std::vector<TimingClockSnapshot> clock_metadata;
  std::vector<TimingLibCellSnapshot> lib_cells;
  std::vector<std::string> clock_names;
};

class TimingExportAdapter
{
 public:
  static TimingSnapshot exportSnapshot();
};

}  // namespace ista

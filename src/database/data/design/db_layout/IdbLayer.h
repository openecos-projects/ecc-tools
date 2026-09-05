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
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once
/**
 * @project		iDB
 * @file		IdbLayer.h
 * @date		25/05/2021
 * @version		0.1
 * @description


        Describe Tech Layer information,.
 *
 */

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <limits.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../../../basic/geometry/IdbGeometry.h"
#include "../IdbEnum.h"
#include "../db_property/IdbCutLayerLef58Property.h"
#include "../db_property/IdbRoutingLayerLef58Property.h"

namespace idb {

using std::map;
using std::string;
using std::vector;

class IdbViaRuleGenerate;
class IdbTrackGrid;

class IdbLayer
{
 public:
  IdbLayer();
  virtual ~IdbLayer() = default;

  // getter
  const string& get_name() const { return _name; };
  const IdbLayerType get_type() const { return _type; }
  bool is_routing() { return _type == IdbLayerType::kLayerRouting; }
  bool is_cut() { return _type == IdbLayerType::kLayerCut; }
  int8_t get_id() { return _layer_id; }
  uint8_t get_order() { return _layer_order; }
  const string& get_lef58_type() const { return _lef58_type; }
  bool is_lef58_backside() const { return _lef58_backside; }

  // setter
  void set_name(string name) { _name = name; }
  void set_type(IdbLayerType type) { _type = type; }
  void set_type(string type);
  void set_id(int8_t id) { _layer_id = id; }
  void set_order(uint8_t z_order) { _layer_order = z_order; }
  void set_lef58_type(string type) { _lef58_type = std::move(type); }
  void set_lef58_backside(bool backside = true) { _lef58_backside = backside; }

  // operator
  bool compareLayer(string name)
  {
    string this_name = _name;
    std::transform(this_name.begin(), this_name.end(), this_name.begin(), ::toupper);
    std::transform(name.begin(), name.end(), name.begin(), ::toupper);
    return this_name.compare(name) == 0 ? true : false;
  }

  bool compareLayer(IdbLayer* layer) { return compareLayer(layer->get_name()); }

  // verify data
  virtual void print();

 private:
  string _name;
  IdbLayerType _type;
  int8_t _layer_id;
  uint8_t _layer_order;
  string _lef58_type;
  bool _lef58_backside = false;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class IdbLayerSpacing
{
 public:
  IdbLayerSpacing();
  ~IdbLayerSpacing();
  // getter
  const IdbLayerSpacingType& get_spacing_type() const { return _spacing_type; }
  const int32_t get_min_spacing() const { return _min_spacing; }
  const int32_t get_min_width() const { return _min_width; }
  const int32_t get_max_width() const { return _max_width; }
  const int32_t get_eol_width() const { return _eol_width; }
  const int32_t get_eol_within() const { return _eol_within; }
  const bool get_has_parallel_edge() const { return _has_parallel_edge; }
  const int32_t get_par_space() const { return _par_space; }
  const int32_t get_par_within() const { return _par_within; }
  const bool get_has_two_edges() const { return _has_two_edges; }

  // setter
  void set_spacing_type(IdbLayerSpacingType spacing_type) { _spacing_type = spacing_type; }
  void set_min_spacing(int32_t min_spacing) { _min_spacing = min_spacing; }
  void set_min_width(int32_t min_width) { _min_width = min_width; }
  void set_max_width(int32_t max_width) { _max_width = max_width; }
  void set_eol_width(int32_t eol_width) { _eol_width = eol_width; }
  void set_eol_within(int32_t eol_within) { _eol_within = eol_within; }
  void set_has_parallel_edge(bool has_parallel_edge) { _has_parallel_edge = has_parallel_edge; }
  void set_par_space(int32_t par_space) { _par_space = par_space; }
  void set_par_within(int32_t par_within) { _par_within = par_within; }
  void set_has_two_edges(bool has_two_edges) { _has_two_edges = has_two_edges; }

  // operator
  bool isDefault() { return _spacing_type == IdbLayerSpacingType::kSpacingDefault ? true : false; }
  bool checkSpacing(int32_t spacing, int32_t width = -1);

 private:
  IdbLayerSpacingType _spacing_type;
  int32_t _min_spacing;
  int32_t _min_width;
  int32_t _max_width;

  // end of line 
  int32_t _eol_width;
  int32_t _eol_within;
  bool _has_parallel_edge;
  int32_t _par_space;
  int32_t _par_within;
  bool _has_two_edges;
};

class IdbLayerSpacingList
{
 public:
  IdbLayerSpacingList();
  ~IdbLayerSpacingList();

  // getter
  const uint32_t get_spacing_list_num() const { return _spacing_list_num; }
  vector<IdbLayerSpacing*>& get_spacing_list() { return _spacing_list; }

  int32_t get_spacing(int32_t width);

  // setter
  void add_spacing(IdbLayerSpacing* spacing);
  void reset();

  // operator
  bool checkSpacing(int32_t spacing, int32_t width = -1);

 private:
  uint32_t _spacing_list_num;
  vector<IdbLayerSpacing*> _spacing_list;
};

class IdbLayerSpacingNotchLength
{
 public:
  [[nodiscard]] int32_t get_notch_length() const { return _notch_length; }
  [[nodiscard]] int32_t get_min_spacing() const { return _min_spacing; }
  [[nodiscard]] bool exist() const { return _notch_length; }
  void set_notch_length(int32_t notch_length) { _notch_length = notch_length; }
  void set_min_spacing(int32_t min_spacing) { _min_spacing = min_spacing; }

 private:
  int32_t _min_spacing{0};
  int32_t _notch_length{0};
};
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class IdbParallelSpacingTable
{
 public:
  IdbParallelSpacingTable() = default;
  IdbParallelSpacingTable(int width_num, int parallel_num)
      : _parallel_run_length(parallel_num), _width(width_num), _spacing(width_num, std::vector<int32_t>(parallel_num))
  {
  }
  void set_parallel_length(int index, int32_t value) { _parallel_run_length.at(index) = value; }
  void set_width(int index, int32_t value) { _width.at(index) = value; }
  void set_spacing(int width_index, int parallel_index, int32_t value) { _spacing[width_index][parallel_index] = value; }
  int32_t get_spacing(int32_t width, int32_t parallel_length);

  std::vector<int32_t>& get_parallel_length_list() { return _parallel_run_length; }
  std::vector<int32_t>& get_width_list() { return _width; }
  std::vector<std::vector<int32_t>>& get_spacing_table() { return _spacing; }

 private:
  std::vector<int32_t> _parallel_run_length;
  std::vector<int32_t> _width;
  std::vector<std::vector<int32_t>> _spacing;
};

class IdbInfluenceSpacingTable
{
 public:
  struct Entry
  {
    int32_t width = -1;
    int32_t within = -1;
    int32_t spacing = -1;
  };

  void add_entry(int32_t width, int32_t within, int32_t spacing) { _entries.push_back(Entry{width, within, spacing}); }
  [[nodiscard]] std::vector<Entry>& get_entry_list() { return _entries; }
  [[nodiscard]] const std::vector<Entry>& get_entry_list() const { return _entries; }
  [[nodiscard]] bool empty() const noexcept { return _entries.empty(); }

 private:
  std::vector<Entry> _entries;
};

class IdbTwoWidthsSpacingTable
{
 public:
  struct WidthEntry
  {
    int32_t width = -1;
    bool has_prl = false;
    int32_t prl = -1;
  };

  IdbTwoWidthsSpacingTable() = default;
  explicit IdbTwoWidthsSpacingTable(int width_num) { resize(width_num); }

  void resize(int width_num)
  {
    _widths.resize(width_num);
    _spacing.assign(width_num, std::vector<int32_t>(width_num));
  }
  void set_width(int index, int32_t value) { _widths.at(index).width = value; }
  void set_width_prl(int index, int32_t value)
  {
    _widths.at(index).has_prl = true;
    _widths.at(index).prl = value;
  }
  void set_spacing(int row_index, int column_index, int32_t value) { _spacing.at(row_index).at(column_index) = value; }
  [[nodiscard]] int32_t get_spacing(int row_index, int column_index) const { return _spacing.at(row_index).at(column_index); }
  [[nodiscard]] int32_t get_spacing(int32_t width1, int32_t width2, int32_t parallel_length) const;
  [[nodiscard]] std::vector<WidthEntry>& get_width_list() { return _widths; }
  [[nodiscard]] const std::vector<WidthEntry>& get_width_list() const { return _widths; }
  [[nodiscard]] std::vector<std::vector<int32_t>>& get_spacing_table() { return _spacing; }
  [[nodiscard]] const std::vector<std::vector<int32_t>>& get_spacing_table() const { return _spacing; }

 private:
  [[nodiscard]] size_t find_width_index(int32_t width, int32_t parallel_length) const;

  std::vector<WidthEntry> _widths;
  std::vector<std::vector<int32_t>> _spacing;
};

class IdbRoutingCurrentDensity
{
 public:
  enum class Kind : uint8_t
  {
    kAc,
    kDc,
  };

  enum class Type : uint8_t
  {
    kUnknown,
    kPeak,
    kAverage,
    kRms,
  };

  IdbRoutingCurrentDensity() = default;
  IdbRoutingCurrentDensity(Kind kind, Type type) : _kind(kind), _type(type) {}

  [[nodiscard]] Kind get_kind() const { return _kind; }
  [[nodiscard]] Type get_type() const { return _type; }
  [[nodiscard]] bool has_scalar() const { return _has_scalar; }
  [[nodiscard]] double get_scalar() const { return _scalar; }
  [[nodiscard]] const std::vector<double>& get_frequency_list() const { return _frequencies; }
  [[nodiscard]] const std::vector<int32_t>& get_width_list() const { return _widths; }
  [[nodiscard]] const std::vector<double>& get_table_entries() const { return _table_entries; }
  [[nodiscard]] std::vector<double>& get_frequency_list() { return _frequencies; }
  [[nodiscard]] std::vector<int32_t>& get_width_list() { return _widths; }
  [[nodiscard]] std::vector<double>& get_table_entries() { return _table_entries; }
  [[nodiscard]] bool has_table() const { return !_table_entries.empty(); }

  void set_kind(Kind kind) { _kind = kind; }
  void set_type(Type type) { _type = type; }
  void set_scalar(double scalar)
  {
    _has_scalar = true;
    _scalar = scalar;
  }
  void add_frequency(double frequency) { _frequencies.push_back(frequency); }
  void add_width(int32_t width) { _widths.push_back(width); }
  void add_table_entry(double value) { _table_entries.push_back(value); }

 private:
  Kind _kind = Kind::kAc;
  Type _type = Type::kUnknown;
  bool _has_scalar = false;
  double _scalar = 0.0;
  std::vector<double> _frequencies;
  std::vector<int32_t> _widths;
  std::vector<double> _table_entries;
};

class IdbCutCurrentDensity
{
 public:
  enum class Kind : uint8_t
  {
    kAc,
    kDc,
  };

  enum class Type : uint8_t
  {
    kUnknown,
    kPeak,
    kAverage,
    kRms,
  };

  IdbCutCurrentDensity() = default;
  IdbCutCurrentDensity(Kind kind, Type type) : _kind(kind), _type(type) {}

  [[nodiscard]] Kind get_kind() const { return _kind; }
  [[nodiscard]] Type get_type() const { return _type; }
  [[nodiscard]] bool has_scalar() const { return _has_scalar; }
  [[nodiscard]] double get_scalar() const { return _scalar; }
  [[nodiscard]] const std::vector<double>& get_frequency_list() const { return _frequencies; }
  [[nodiscard]] const std::vector<int32_t>& get_cut_area_list() const { return _cut_areas; }
  [[nodiscard]] const std::vector<double>& get_table_entries() const { return _table_entries; }
  [[nodiscard]] std::vector<double>& get_frequency_list() { return _frequencies; }
  [[nodiscard]] std::vector<int32_t>& get_cut_area_list() { return _cut_areas; }
  [[nodiscard]] std::vector<double>& get_table_entries() { return _table_entries; }
  [[nodiscard]] bool has_table() const { return !_table_entries.empty(); }

  void set_kind(Kind kind) { _kind = kind; }
  void set_type(Type type) { _type = type; }
  void set_scalar(double scalar)
  {
    _has_scalar = true;
    _scalar = scalar;
  }
  void add_frequency(double frequency) { _frequencies.push_back(frequency); }
  void add_cut_area(int32_t cut_area) { _cut_areas.push_back(cut_area); }
  void add_table_entry(double value) { _table_entries.push_back(value); }

 private:
  Kind _kind = Kind::kAc;
  Type _type = Type::kUnknown;
  bool _has_scalar = false;
  double _scalar = 0.0;
  std::vector<double> _frequencies;
  std::vector<int32_t> _cut_areas;
  std::vector<double> _table_entries;
};

class IdbLayerSpacingTable
{
 public:
  void set_parallel(std::shared_ptr<IdbParallelSpacingTable> parallel) { _parallel = std::move(parallel); }
  void set_influence(std::shared_ptr<IdbInfluenceSpacingTable> influence) { _influence = std::move(influence); }
  void set_two_widths(std::shared_ptr<IdbTwoWidthsSpacingTable> two_widths) { _two_widths = std::move(two_widths); }
  [[nodiscard]] std::shared_ptr<IdbParallelSpacingTable> get_parallel() const { return _parallel; }
  [[nodiscard]] std::shared_ptr<IdbInfluenceSpacingTable> get_influence() const { return _influence; }
  [[nodiscard]] std::shared_ptr<IdbTwoWidthsSpacingTable> get_two_widths() const { return _two_widths; }
  [[nodiscard]] bool is_parallel() const { return _parallel != nullptr; }
  [[nodiscard]] bool is_influence() const { return _influence != nullptr; }
  [[nodiscard]] bool is_two_widths() const { return _two_widths != nullptr; }
  int32_t get_parallel_spacing(int32_t width, int32_t par_length) { return _parallel->get_spacing(width, par_length); }
  int32_t get_two_widths_spacing(int32_t width1, int32_t width2, int32_t par_length)
  {
    return _two_widths->get_spacing(width1, width2, par_length);
  }

 private:
  std::shared_ptr<IdbParallelSpacingTable> _parallel;
  std::shared_ptr<IdbInfluenceSpacingTable> _influence;
  std::shared_ptr<IdbTwoWidthsSpacingTable> _two_widths;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct IdbMinEncloseArea
{
  int32_t _area;
  int32_t _width;
};

class IdbMinEncloseAreaList
{
 public:
  IdbMinEncloseAreaList();
  ~IdbMinEncloseAreaList();

  // getter
  const uint32_t get_min_area_list_num() const { return _area_list_num; }
  vector<IdbMinEncloseArea>& get_min_area_list() { return _area_list; }

  // setter
  void add_min_area(int32_t area, int32_t width = -1);
  void reset();

  // operator

 private:
  uint32_t _area_list_num;
  vector<IdbMinEncloseArea> _area_list;
};
class IdbMinStep
{
 public:
  enum class Type
  {
    kNone = 0,
    kINSIDECORNER,
    kOUTSIDECORNER,
    kSTEP,
  };
  IdbMinStep() = default;
  explicit IdbMinStep(int32_t min_step) : _min_step_length(min_step) {};
  [[nodiscard]] int32_t get_min_step_length() const { return _min_step_length; }
  [[nodiscard]] Type get_type() const { return _type; }
  [[nodiscard]] bool has_length_sum() const { return _has_length_sum; }
  [[nodiscard]] bool has_max_edges() const { return _has_max_edges; }
  [[nodiscard]] int32_t get_max_length_sum() const { return _max_length_sum; }
  [[nodiscard]] int32_t get_max_edges() const { return _max_edges; }

  void set_min_step_length(int32_t min_step_len) { _min_step_length = min_step_len; }
  void set_type(Type type) { _type = type; }
  void set_type(const std::string& type)
  {
    if (type == "INSIDECORNER") {
      set_type(Type::kINSIDECORNER);
    } else if (type == "OUTSIDECORNER") {
      set_type(Type::kOUTSIDECORNER);
    } else if (type == "STEP") {
      set_type(Type::kSTEP);
    } else {
      set_type(Type::kNone);
    }
  }
  void set_max_length(int32_t max_length) { _has_length_sum = true, _max_length_sum = max_length; }
  void set_max_edges(int32_t max_edges) { _has_max_edges = true, _max_edges = max_edges; }

 private:
  bool _has_max_edges{false};
  bool _has_length_sum{false};
  int32_t _min_step_length{0};
  int32_t _max_length_sum{0};
  int32_t _max_edges{0};
  Type _type{Type::kNone};
};

class IdbLayerMinimumCut
{
 public:
  enum class Orient
  {
    kNone = 0,
    kFromAbove,
    kFromBelow,
  };

  IdbLayerMinimumCut() = default;
  IdbLayerMinimumCut(int32_t num_cuts, int32_t width) : _num_cuts(num_cuts), _width(width) {}

  [[nodiscard]] int32_t get_num_cuts() const { return _num_cuts; }
  [[nodiscard]] int32_t get_width() const { return _width; }
  [[nodiscard]] bool has_within_cut_distance() const { return _has_within_cut_distance; }
  [[nodiscard]] int32_t get_within_cut_distance() const { return _within_cut_distance; }
  [[nodiscard]] Orient get_orient() const { return _orient; }
  [[nodiscard]] bool has_length() const { return _has_length; }
  [[nodiscard]] int32_t get_length() const { return _length; }
  [[nodiscard]] int32_t get_length_distance() const { return _length_distance; }

  void set_num_cuts(int32_t num_cuts) { _num_cuts = num_cuts; }
  void set_width(int32_t width) { _width = width; }
  void set_within_cut_distance(int32_t within_cut_distance)
  {
    _has_within_cut_distance = true;
    _within_cut_distance = within_cut_distance;
  }
  void set_orient(Orient orient) { _orient = orient; }
  void set_length(int32_t length, int32_t distance)
  {
    _has_length = true;
    _length = length;
    _length_distance = distance;
  }

 private:
  int32_t _num_cuts = -1;
  int32_t _width = -1;
  bool _has_within_cut_distance = false;
  int32_t _within_cut_distance = -1;
  Orient _orient = Orient::kNone;
  bool _has_length = false;
  int32_t _length = -1;
  int32_t _length_distance = -1;
};

// Direction, Rect, Pitch, OffSet, Width, Space, TrackGrid, res, cap, WireExtension, Thickness,
class IdbLayerRouting : public IdbLayer
{
 public:
  IdbLayerRouting();
  virtual ~IdbLayerRouting();

  // getter
  const int32_t get_width() const { return _width; }
  const int32_t get_min_width() const { return _min_width; }
  const int32_t get_max_width() const { return _max_width; }
  const int32_t get_diag_width() const { return _diag_width; }
  const int32_t get_diag_spacing() const { return _diag_spacing; }
  [[nodiscard]] bool has_protrusion_width() const
  {
    return _protrusion_width_1 >= 0 && _protrusion_length >= 0 && _protrusion_width_2 >= 0;
  }
  const int32_t get_protrusion_width_1() const { return _protrusion_width_1; }
  const int32_t get_protrusion_length() const { return _protrusion_length; }
  const int32_t get_protrusion_width_2() const { return _protrusion_width_2; }
  const IdbLayerOrientValue& get_pitch() const { return _pitch; }
  const int32_t get_pitch_x() const { return _pitch.orient_x; }
  const int32_t get_pitch_y() const { return _pitch.orient_y; }
  const int32_t get_pitch_prefer() { return is_horizontal() ? _pitch.orient_y : _pitch.orient_x; }
  const int32_t get_wire_extension() const { return _wire_extension; }
  const int32_t get_thickness() const { return _thickness; }
  const int32_t get_height() const { return _height; }
  const int32_t get_shrinkage() const { return _shrinkage; }
  const double& get_cap_multiplier() const { return _cap_multiplier; }
  const int32_t get_fill_active_spacing() const { return _fill_active_spacing; }
  const int32_t get_area() const { return _area; }

  const double& get_resistance() const { return _resistance; }
  const double& get_capacitance() const { return _capacitance; }
  const double& get_edge_capacitance() const { return _edge_capacitance; }

  const double& get_min_density() const { return _min_density; }
  const double& get_max_density() const { return _max_density; }
  const int32_t get_density_check_length() const { return _density_check_length; }
  const int32_t get_density_check_width() const { return _density_check_width; }
  const int32_t get_density_check_step() const { return _density_check_step; }
  const int32_t get_min_cut_num() const { return _min_cut_num; }
  const int32_t get_min_cut_width() const { return _min_cut_width; }

  const IdbLayerOrientValue& get_offset() const { return _offset; }
  const int32_t get_offset_x() const { return _offset.orient_x; }
  const int32_t get_offset_y() const { return _offset.orient_y; }
  const int32_t get_offset_prefer() { return is_horizontal() ? _offset.orient_y : _offset.orient_x; }
  const IdbLayerDirection get_direction() const { return _direction; }
  IdbLayerDirection get_nonprefer_direction()
  {
    return _direction == IdbLayerDirection::kHorizontal ? IdbLayerDirection::kVertical : IdbLayerDirection::kHorizontal;
  }
  bool is_horizontal() { return _direction == IdbLayerDirection::kHorizontal ? true : false; }
  bool is_vertical() { return _direction == IdbLayerDirection::kVertical ? true : false; }
  bool is_lef58_rect_only() const { return _lef58_rect_only; }
  bool is_lef58_rect_only_except_non_core_pins() const { return _lef58_rect_only_except_non_core_pins; }
  bool is_lef58_right_way_on_grid_only() const { return _lef58_right_way_on_grid_only; }
  bool is_lef58_right_way_on_grid_only_check_mask() const { return _lef58_right_way_on_grid_only_check_mask; }

  vector<IdbTrackGrid*>& get_track_grid_list() { return _track_grid_list; }
  int32_t get_track_grid_num() { return _track_grid_list.size(); }
  IdbTrackGrid* get_prefer_track_grid();
  IdbTrackGrid* get_nonprefer_track_grid();
  IdbLayerSpacingList* get_spacing_list() { return _spacing_list; }
  IdbMinEncloseAreaList* get_min_enclose_area_list() { return _min_enclose_area_list; }

  int32_t get_power_segment_width() { return _power_segment_width; }

  std::shared_ptr<IdbLayerSpacingTable> get_spacing_table();
  std::shared_ptr<IdbLayerSpacingTable> get_spacing_table_from_spacing_list();
  void set_parallel_spacing_table(std::shared_ptr<IdbParallelSpacingTable> ptbl);
  void set_influence_spacing_table(std::shared_ptr<IdbInfluenceSpacingTable> table);
  void set_two_widths_spacing_table(std::shared_ptr<IdbTwoWidthsSpacingTable> table);
  int32_t get_spacing(int32_t width, int32_t par_length = 0);
  IdbLayerSpacingNotchLength& get_spacing_notchlength() { return _spacing_notch_length; }

  // lef58_property getter
  std::shared_ptr<IdbMinStep> get_min_step() { return _min_step; }
  std::vector<IdbLayerMinimumCut>& get_minimum_cut_list() { return _minimum_cut_list; }
  std::vector<std::shared_ptr<routinglayer::Lef58SpacingEol>>& get_lef58_spacing_eol_list() { return _lef58_spacing_eol_list; };
  std::vector<std::shared_ptr<routinglayer::Lef58Area>>& get_lef58_area() { return _lef58_area; }
  std::shared_ptr<routinglayer::Lef58CornerFillSpacing> get_lef58_corner_fill_spacing() { return _lef58_corner_fill_spacing; }
  std::vector<std::shared_ptr<routinglayer::Lef58CornerSpacing>>& get_lef58_corner_spacing_list() { return _lef58_corner_spacing_list; }
  std::vector<std::shared_ptr<routinglayer::Lef58MinimumCut>>& get_lef58_minimum_cut() { return _lef58_minimum_cut; }
  std::vector<std::shared_ptr<routinglayer::Lef58MinStep>>& get_lef58_min_step() { return _lef58_min_steps; }
  std::vector<std::shared_ptr<routinglayer::Lef58WidthTable>>& get_lef58_width_table() { return _lef58_width_table; }
  std::vector<std::shared_ptr<routinglayer::Lef58SpacingTablePrl>>& get_lef58_spacingtable_prl() { return _lef58_spacingtable_prl; }
  std::shared_ptr<routinglayer::Lef58SpacingNotchlength> get_lef58_spacing_notchlength() { return _lef58_spacing_notchlength; }
  std::shared_ptr<routinglayer::Lef58SpacingTableJogToJog> get_lef58_spacingtable_jogtojog() { return _lef58_spacingtable_jogtojog; }
  [[nodiscard]] std::vector<IdbRoutingCurrentDensity>& get_current_density_list() { return _current_density_list; }
  [[nodiscard]] const std::vector<IdbRoutingCurrentDensity>& get_current_density_list() const { return _current_density_list; }

  // setter
  void set_width(int32_t width) { _width = width; }
  void set_min_width(int32_t min_width) { _min_width = min_width; }
  void set_max_width(int32_t max_width) { _max_width = max_width; }
  void set_diag_width(int32_t diag_width) { _diag_width = diag_width; }
  void set_diag_spacing(int32_t diag_spacing) { _diag_spacing = diag_spacing; }
  void set_protrusion_width(int32_t width1, int32_t length, int32_t width2)
  {
    _protrusion_width_1 = width1;
    _protrusion_length = length;
    _protrusion_width_2 = width2;
  }
  void set_pitch(IdbLayerOrientValue pitch);
  void set_offset(IdbLayerOrientValue offset);
  void set_direction(IdbLayerDirection direction) { _direction = direction; }
  void set_direction(string direction_str);
  void set_lef58_rect_only(bool rect_only = true) { _lef58_rect_only = rect_only; }
  void set_lef58_rect_only_except_non_core_pins(bool except_non_core_pins = true)
  {
    _lef58_rect_only_except_non_core_pins = except_non_core_pins;
  }
  void set_lef58_right_way_on_grid_only(bool right_way_on_grid_only = true) { _lef58_right_way_on_grid_only = right_way_on_grid_only; }
  void set_lef58_right_way_on_grid_only_check_mask(bool check_mask = true) { _lef58_right_way_on_grid_only_check_mask = check_mask; }
  void set_wire_extension(int32_t wire_extension) { _wire_extension = wire_extension; }
  void set_thickness(int32_t thickness) { _thickness = thickness; }
  void set_height(int32_t height) { _height = height; }
  void set_shrinkage(int32_t shrinkage) { _shrinkage = shrinkage; }
  void set_cap_multiplier(double cap_multiplier) { _cap_multiplier = cap_multiplier; }
  void set_fill_active_spacing(int32_t fill_active_spacing) { _fill_active_spacing = fill_active_spacing; }
  void set_resistance(double resistance) { _resistance = resistance; }
  void set_capacitance(double capacitance) { _capacitance = capacitance; }
  void set_edge_capacitance(double edge_capacitance) { _edge_capacitance = edge_capacitance; }
  void set_area(int32_t area) { _area = area; }

  void set_min_density(double min_density) { _min_density = min_density; }
  void set_max_density(double max_density) { _max_density = max_density; }
  void set_density_check_length(int32_t density_check_length) { _density_check_length = density_check_length; }
  void set_density_check_width(int32_t density_check_width) { _density_check_width = density_check_width; }
  void set_density_check_step(int32_t density_check_step) { _density_check_step = density_check_step; }
  void set_min_cut_num(int32_t min_cut_num) { _min_cut_num = min_cut_num; }
  void set_min_cut_width(int32_t min_cut_width) { _min_cut_width = min_cut_width; }

  // void set_track_grid_list(IdbTrackGridList* track_grid_list){_track_grid_list = track_grid_list;}
  void add_track_grid(IdbTrackGrid* track_grid) { _track_grid_list.emplace_back(track_grid); }
  void set_spacing_list(IdbLayerSpacingList* spacing_list) { _spacing_list = spacing_list; }
  void set_min_enclose_area_list(IdbMinEncloseAreaList* min_area_list) { _min_enclose_area_list = min_area_list; }

  void set_power_segment_width(int32_t power_segment_width) { _power_segment_width = power_segment_width; }

  void set_spacing_table(std::shared_ptr<IdbLayerSpacingTable> spacing_table) { _spacing_table = std::move(spacing_table); }
  void add_minimum_cut(IdbLayerMinimumCut minimum_cut)
  {
    if (_min_cut_num < 0 && _min_cut_width < 0) {
      _min_cut_num = minimum_cut.get_num_cuts();
      _min_cut_width = minimum_cut.get_width();
    }
    _minimum_cut_list.push_back(std::move(minimum_cut));
  }
  // operator

  // verifier
  void set_min_step(std::shared_ptr<IdbMinStep> min_step) { _min_step = std::move(min_step); }

  // lef58_property setter
  void add_spacing_eol(std::shared_ptr<routinglayer::Lef58SpacingEol> spacing) { _lef58_spacing_eol_list.emplace_back(std::move(spacing)); }
  void add_lef58_area(std::shared_ptr<routinglayer::Lef58Area> lef58_area) { _lef58_area.emplace_back(std::move(lef58_area)); }
  void set_lef58_cornerfill_spacing(std::shared_ptr<routinglayer::Lef58CornerFillSpacing> cornerfill_spacing)
  {
    _lef58_corner_fill_spacing = std::move(cornerfill_spacing);
  }
  void add_lef58_corner_spacing(std::shared_ptr<routinglayer::Lef58CornerSpacing> corner_spacing)
  {
    _lef58_corner_spacing_list.emplace_back(std::move(corner_spacing));
  }
  void add_lef58_minimum_cut(std::shared_ptr<routinglayer::Lef58MinimumCut> minimum_cut)
  {
    _lef58_minimum_cut.push_back(std::move(minimum_cut));
  }
  void add_lef58_min_step(std::shared_ptr<routinglayer::Lef58MinStep> min_step) { _lef58_min_steps.push_back(std::move(min_step)); }
  void add_lef58_width_table(std::shared_ptr<routinglayer::Lef58WidthTable> width_table)
  {
    _lef58_width_table.push_back(std::move(width_table));
  }
  void add_lef58_spacingtable_prl(std::shared_ptr<routinglayer::Lef58SpacingTablePrl> spacingtable)
  {
    _lef58_spacingtable_prl.push_back(std::move(spacingtable));
  }
  void set_lef58_spacing_notchlength(std::shared_ptr<routinglayer::Lef58SpacingNotchlength> spacing)
  {
    _lef58_spacing_notchlength = std::move(spacing);
  }
  void set_lef58_spacingtable_jogtojog(std::shared_ptr<routinglayer::Lef58SpacingTableJogToJog> jogtojog)
  {
    _lef58_spacingtable_jogtojog = std::move(jogtojog);
  }
  void add_current_density(IdbRoutingCurrentDensity current_density) { _current_density_list.push_back(std::move(current_density)); }

 private:
  int32_t _width;
  int32_t _min_width;
  int32_t _max_width;
  int32_t _diag_width;
  int32_t _diag_spacing;
  int32_t _protrusion_width_1;
  int32_t _protrusion_length;
  int32_t _protrusion_width_2;
  IdbLayerOrientValue _pitch;
  IdbLayerOrientValue _offset;
  IdbLayerDirection _direction;
  bool _lef58_rect_only = false;
  bool _lef58_rect_only_except_non_core_pins = false;
  bool _lef58_right_way_on_grid_only = false;
  bool _lef58_right_way_on_grid_only_check_mask = false;
  int32_t _wire_extension;
  int32_t _thickness;
  int32_t _height;
  int32_t _shrinkage;
  double _cap_multiplier;
  int32_t _fill_active_spacing;
  int32_t _area;
  double _resistance;
  double _capacitance;
  double _edge_capacitance;

  double _min_density;
  double _max_density;
  int32_t _density_check_length;
  int32_t _density_check_width;
  int32_t _density_check_step;
  int32_t _min_cut_num;
  int32_t _min_cut_width;

  IdbLayerSpacingList* _spacing_list;
  vector<IdbTrackGrid*> _track_grid_list;
  IdbMinEncloseAreaList* _min_enclose_area_list;

  ///
  int32_t _power_segment_width;

  std::shared_ptr<IdbLayerSpacingTable> _spacing_table;
  IdbLayerSpacingNotchLength _spacing_notch_length;
  std::shared_ptr<IdbMinStep> _min_step;
  std::vector<IdbLayerMinimumCut> _minimum_cut_list;
  /// lef58_properties
  std::vector<std::shared_ptr<routinglayer::Lef58SpacingEol>> _lef58_spacing_eol_list;
  std::vector<std::shared_ptr<routinglayer::Lef58Area>> _lef58_area;
  std::shared_ptr<routinglayer::Lef58CornerFillSpacing> _lef58_corner_fill_spacing;
  std::vector<std::shared_ptr<routinglayer::Lef58CornerSpacing>> _lef58_corner_spacing_list;
  std::vector<std::shared_ptr<routinglayer::Lef58MinimumCut>> _lef58_minimum_cut;
  std::vector<std::shared_ptr<routinglayer::Lef58MinStep>> _lef58_min_steps;
  std::vector<std::shared_ptr<routinglayer::Lef58WidthTable>> _lef58_width_table;
  std::vector<std::shared_ptr<routinglayer::Lef58SpacingTablePrl>> _lef58_spacingtable_prl;
  std::shared_ptr<routinglayer::Lef58SpacingNotchlength> _lef58_spacing_notchlength;
  std::shared_ptr<routinglayer::Lef58SpacingTableJogToJog> _lef58_spacingtable_jogtojog;
  std::vector<IdbRoutingCurrentDensity> _current_density_list;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct IdbArrayCut
{
  int32_t _array_cut;
  int32_t _array_spacing;
};

struct IdbCutOrthogonalSpacingTableItem
{
  int32_t within = 0;
  int32_t spacing = 0;
};

struct IdbCutOrthogonalSpacingTable
{
  bool lef58_property = false;
  std::vector<IdbCutOrthogonalSpacingTableItem> items;
};

class IdbLayerCutArraySpacing
{
 public:
  IdbLayerCutArraySpacing();
  ~IdbLayerCutArraySpacing();

  // getter
  const bool is_long_array() const { return _is_long_array; }
  const int32_t get_via_width() const { return _via_width; }
  const int32_t get_cut_spacing() const { return _cut_spacing; }
  const int32_t get_array_cut_number() const { return _num_array_cut; }
  vector<IdbArrayCut>& get_array_cut_list() { return _array_cut_list; }
  int get_array_cut_min_num()
  {
    if (_array_cut_list.size() <= 0) {
      return 0;
    }

    int num = INT_MAX;
    for (auto array_cut : _array_cut_list) {
      if (_cut_spacing != array_cut._array_spacing) {
        num = std::min(num, array_cut._array_cut);
      }
    }

    return INT_MAX == num ? 0 : num;
  }

  // setter
  void set_long_array(bool long_array) { _is_long_array = long_array; }
  void set_via_width(int32_t via_width) { _via_width = via_width; }
  void set_cut_spacing(int32_t cut_spacing) { _cut_spacing = cut_spacing; }
  void set_array_cut_num(int32_t num)
  {
    _num_array_cut = num;
    _array_cut_list.resize(num);
  }
  void set_array_cut_list(vector<IdbArrayCut> array_cut_list) { _array_cut_list = array_cut_list; }
  bool set_array_value(int32_t index, int32_t array_cut, int32_t array_spacing);

 private:
  bool _is_long_array;
  int32_t _via_width;
  int32_t _cut_spacing;
  int32_t _num_array_cut;
  vector<IdbArrayCut> _array_cut_list;
};

class IdbLayerCutEnclosure
{
 public:
  enum class Side : uint8_t
  {
    kNone,
    kAbove,
    kBelow,
  };

  IdbLayerCutEnclosure();
  ~IdbLayerCutEnclosure();
  // getter
  const int32_t get_overhang_1() const { return _overhang_1; }
  const int32_t get_overhang_2() const { return _overhang_2; }
  const int32_t get_min_width() const { return _min_width; }
  const int32_t get_cut_within() const { return _cut_within; }
  const int32_t get_min_length() const { return _min_length; }
  Side get_side() const { return _side; }
  const IdbRect& get_rect() const { return _rect; }

  // setter
  void set_overhang_1(int32_t value) { _overhang_1 = value; }
  void set_overhang_2(int32_t value) { _overhang_2 = value; }
  void set_min_width(int32_t value) { _min_width = value; }
  void set_cut_within(int32_t value) { _cut_within = value; }
  void set_min_length(int32_t value) { _min_length = value; }
  void set_side(Side side) { _side = side; }
  void set_rect(IdbRect rect) { _rect = rect; }

  // operator

 private:
  int32_t _overhang_1;
  int32_t _overhang_2;
  int32_t _min_width;
  int32_t _cut_within;
  int32_t _min_length;
  Side _side;

  IdbRect _rect;
};

class IdbRuleCutSpacingList;

class IdbLayerCutSpacing
{
 public:
  class AdjacentCuts
  {
   public:
    AdjacentCuts() = default;
    AdjacentCuts(int32_t adjacent_cuts, int32_t cut_within) : _adjacnet_cuts(adjacent_cuts), _cut_within(cut_within) {};

    void set_adjacent_cuts(int32_t adjacent_cuts) { _adjacnet_cuts = adjacent_cuts; }
    void set_cut_within(int32_t cut_within) { _cut_within = cut_within; }
    [[nodiscard]] int32_t get_adjacent_cuts() const { return _adjacnet_cuts; }
    [[nodiscard]] int32_t get_cut_within() const { return _cut_within; }

   private:
    int32_t _adjacnet_cuts;
    int32_t _cut_within;
  };
  explicit IdbLayerCutSpacing(int32_t spacing = 0) : _spacing(spacing) {}

  operator int32_t() { return _spacing; }

  void set_spacing(int32_t spacing) { _spacing = spacing; }
  [[nodiscard]] int32_t get_spacing() const { return _spacing; }

  void set_adjacent_cuts(std::optional<AdjacentCuts> adj) { _adjacnet_cuts = adj; }
  [[nodiscard]] std::optional<AdjacentCuts> get_adjacent_cuts() const { return _adjacnet_cuts; }

  void set_has_same_net(bool has_same_net) { _has_same_net = has_same_net; };
  [[nodiscard]] bool get_has_same_net() const { return _has_same_net; }
  void set_center_to_center(bool value) { _center_to_center = value; }
  [[nodiscard]] bool is_center_to_center() const { return _center_to_center; }
  void set_same_net_pg_only(bool value) { _same_net_pg_only = value; }
  [[nodiscard]] bool is_same_net_pg_only() const { return _same_net_pg_only; }
  void set_second_layer_name(std::string value) { _second_layer_name = std::move(value); }
  [[nodiscard]] const std::string& get_second_layer_name() const { return _second_layer_name; }
  void set_stack(bool value) { _stack = value; }
  [[nodiscard]] bool is_stack() const { return _stack; }
  void set_except_same_pg_net(bool value) { _except_same_pg_net = value; }
  [[nodiscard]] bool is_except_same_pg_net() const { return _except_same_pg_net; }
  void set_parallel_overlap(bool value) { _parallel_overlap = value; }
  [[nodiscard]] bool is_parallel_overlap() const { return _parallel_overlap; }
  void set_cut_area(int64_t value) { _cut_area = value; }
  [[nodiscard]] int64_t get_cut_area() const { return _cut_area; }

 private:
  int32_t _spacing;
  std::optional<AdjacentCuts> _adjacnet_cuts;
  bool _has_same_net = false;
  bool _center_to_center = false;
  bool _same_net_pg_only = false;
  std::string _second_layer_name;
  bool _stack = false;
  bool _except_same_pg_net = false;
  bool _parallel_overlap = false;
  int64_t _cut_area = -1;
};

class IdbLayerCut : public IdbLayer
{
 public:
  IdbLayerCut();
  virtual ~IdbLayerCut();
  // getter
  const int32_t get_width() const { return _width; }
  [[nodiscard]] double get_resistance_per_cut() const { return _resistance_per_cut; }
  std::vector<IdbLayerCutSpacing*> get_spacings() { return _spacings; }
  const std::vector<IdbCutOrthogonalSpacingTable>& get_orthogonal_spacing_tables() const { return _orthogonal_spacing_tables; }
  IdbLayerCutArraySpacing* get_array_spacing() { return _array_spacing; }
  IdbLayerCutEnclosure* get_enclosure_below() { return _enclosure_below; }
  IdbLayerCutEnclosure* get_enclosure_above() { return _enclosure_above; }
  const std::vector<IdbLayerCutEnclosure>& get_enclosure_rules() const { return _enclosure_rules; }
  IdbViaRuleGenerate* get_via_rule() { return _via_rule == nullptr ? _via_rule_default : _via_rule; }
  IdbViaRuleGenerate* get_via_rule_raw() { return _via_rule; }
  IdbViaRuleGenerate* get_via_rule_default() { return _via_rule_default; }

  void add_spacing(IdbLayerCutSpacing* spacing) { _spacings.push_back(spacing); }
  void add_orthogonal_spacing_table(IdbCutOrthogonalSpacingTable table)
  {
    _orthogonal_spacing_tables.push_back(std::move(table));
  }
  std::vector<std::shared_ptr<cutlayer::Lef58Cutclass>>& get_lef58_cutclass_list() { return _lef58_cutclass_list; }
  std::vector<std::shared_ptr<cutlayer::Lef58Enclosure>>& get_lef58_enclosure_list() { return _lef58_enclosure_list; }
  std::vector<std::shared_ptr<cutlayer::Lef58EnclosureEdge>>& get_lef58_enclosure_edge_list() { return _lef58_enclosure_edge_list; }
  std::shared_ptr<cutlayer::Lef58EolEnclosure> get_lef58_eol_enclosure() { return _lef58_eol_enclosure; }
  std::shared_ptr<cutlayer::Lef58EolSpacing> get_lef58_eol_spacing() { return _lef58_eol_spacing; }
  std::vector<std::shared_ptr<cutlayer::Lef58SpacingTable>>& get_lef58_spacing_table() { return _lef58_spacing_tables; }
  [[nodiscard]] std::vector<IdbCutCurrentDensity>& get_current_density_list() { return _current_density_list; }
  [[nodiscard]] const std::vector<IdbCutCurrentDensity>& get_current_density_list() const { return _current_density_list; }
  // setter
  void set_width(int32_t width) { _width = width; }
  void set_resistance_per_cut(double resistance_per_cut) { _resistance_per_cut = resistance_per_cut; }
  // void set_spacing(int32_t spacing) { _spacing = spacing; }
  void set_array_spacing(IdbLayerCutArraySpacing* array_spacing) { _array_spacing = array_spacing; }
  void set_enclosure_below(IdbLayerCutEnclosure* enclosure_below) { _enclosure_below = enclosure_below; }
  void set_enclosure_above(IdbLayerCutEnclosure* enclosure_above) { _enclosure_above = enclosure_above; }
  void add_enclosure_rule(IdbLayerCutEnclosure enclosure) { _enclosure_rules.push_back(std::move(enclosure)); }
  void set_via_rule(IdbViaRuleGenerate* via_rule) { _via_rule = via_rule; }
  void set_via_rule_default(IdbViaRuleGenerate* via_rule) { _via_rule_default = via_rule; }

  void add_lef58_cutclass(std::shared_ptr<cutlayer::Lef58Cutclass> cutclass) { _lef58_cutclass_list.emplace_back(std::move(cutclass)); };
  void add_lef58_enclosure(std::shared_ptr<cutlayer::Lef58Enclosure> enclosure)
  {
    _lef58_enclosure_list.emplace_back(std::move(enclosure));
  }
  void add_lef58_enclosure_edge(std::shared_ptr<cutlayer::Lef58EnclosureEdge> enclosure_edge)
  {
    _lef58_enclosure_edge_list.emplace_back(std::move(enclosure_edge));
  }
  void set_lef58_eolenclosure(std::shared_ptr<cutlayer::Lef58EolEnclosure> eol_enclosure)
  {
    _lef58_eol_enclosure = std::move(eol_enclosure);
  }
  void set_lef58_eolspacing(std::shared_ptr<cutlayer::Lef58EolSpacing> eol_spacing) { _lef58_eol_spacing = std::move(eol_spacing); }
  void add_lef58_spacing_table(std::shared_ptr<cutlayer::Lef58SpacingTable> spacing_table)
  {
    _lef58_spacing_tables.push_back(std::move(spacing_table));
  }
  void add_current_density(IdbCutCurrentDensity current_density) { _current_density_list.push_back(std::move(current_density)); }

 private:
  //!--------tbd----------------
  int32_t _width;
  double _resistance_per_cut;
  // int32_t _spacing;
  std::vector<IdbLayerCutSpacing*> _spacings;
  std::vector<IdbCutOrthogonalSpacingTable> _orthogonal_spacing_tables;

  IdbLayerCutArraySpacing* _array_spacing;
  IdbLayerCutEnclosure* _enclosure_below;
  IdbLayerCutEnclosure* _enclosure_above;
  std::vector<IdbLayerCutEnclosure> _enclosure_rules;

  //   IdbVias _vias;
  IdbViaRuleGenerate* _via_rule_default;
  IdbViaRuleGenerate* _via_rule;

  IdbRuleCutSpacingList* _cut_spacing_list;

  std::vector<std::shared_ptr<cutlayer::Lef58Cutclass>> _lef58_cutclass_list;
  std::vector<std::shared_ptr<cutlayer::Lef58Enclosure>> _lef58_enclosure_list;
  std::vector<std::shared_ptr<cutlayer::Lef58EnclosureEdge>> _lef58_enclosure_edge_list;
  std::shared_ptr<cutlayer::Lef58EolEnclosure> _lef58_eol_enclosure;
  std::shared_ptr<cutlayer::Lef58EolSpacing> _lef58_eol_spacing;
  std::vector<std::shared_ptr<cutlayer::Lef58SpacingTable>> _lef58_spacing_tables;
  std::vector<IdbCutCurrentDensity> _current_density_list;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class IdbLayerMasterslice : public IdbLayer
{
 public:
  IdbLayerMasterslice() { set_type(IdbLayerType::kLayerMasterslice); }
  virtual ~IdbLayerMasterslice() = default;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class IdbLayerOverlap : public IdbLayer
{
 public:
  IdbLayerOverlap() { set_type(IdbLayerType::kLayerOverlap); }
  virtual ~IdbLayerOverlap() = default;

 private:
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class IdbLayerImplantSpacing
{
 public:
  IdbLayerImplantSpacing()
  {
    _direction = IdbLayerDirection::kHorizontal;
    _prl = 0;
    _is_except_abutted = false;
    _is_except_corner_touch = false;
    _layer_2nd = nullptr;
    _length = -1;
    _min_spacing = -1;
  }
  ~IdbLayerImplantSpacing() = default;

  /// getter
  int32_t get_prl() { return _prl; }
  int32_t get_length() { return _length; }
  int32_t get_min_spacing() { return _min_spacing; }
  IdbLayer* get_layer_2nd() { return _layer_2nd; }
  IdbLayerDirection get_direction() { return _direction; }
  bool is_except_abutted() { return _is_except_abutted; }
  bool is_except_corner_touch() { return _is_except_corner_touch; }
  /// setter
  void set_prl(int32_t prl) { _prl = prl; }
  void set_length(int32_t length) { _length = length; }
  void set_min_spacing(int32_t min_spacing) { _min_spacing = min_spacing; }
  void set_layer_2nd(IdbLayer* layer_2nd) { _layer_2nd = layer_2nd; }
  void set_direction(IdbLayerDirection direction) { _direction = direction; }
  void set_is_except_abutted(bool is_except_abutted) { _is_except_abutted = is_except_abutted; }
  void set_is_except_corner_touch(bool is_except_corner_touch) { _is_except_corner_touch = is_except_corner_touch; }

 private:
  int32_t _prl;
  int32_t _length;
  int32_t _min_spacing;
  IdbLayer* _layer_2nd;
  IdbLayerDirection _direction = IdbLayerDirection::kHorizontal;
  bool _is_except_abutted;
  bool _is_except_corner_touch;
};

class IdbLayerImplantSpacingList
{
 public:
  IdbLayerImplantSpacingList() {}
  ~IdbLayerImplantSpacingList() { reset(); }

 public:
  // getter
  const int32_t get_num() const { return _spacing_list.size(); };
  vector<IdbLayerImplantSpacing*>& get_min_spacing_list() { return _spacing_list; }
  IdbLayerImplantSpacing* get_min_spacing(int i)
  {
    if (i >= 0 && i < static_cast<int>(_spacing_list.size())) {
      return _spacing_list[i];
    }

    return nullptr;
  }

  // setter
  IdbLayerImplantSpacing* add_min_spacing()
  {
    IdbLayerImplantSpacing* spacing = new IdbLayerImplantSpacing();

    _spacing_list.emplace_back(spacing);

    return spacing;
  }

  void reset()
  {
    for (auto& spacing : _spacing_list) {
      if (spacing != nullptr) {
        delete spacing;
        spacing = nullptr;
      }
    }

    _spacing_list.clear();
  }

  // operator

 private:
  vector<IdbLayerImplantSpacing*> _spacing_list;
};

class IdbLayerImplant : public IdbLayer
{
 public:
  IdbLayerImplant()
  {
    set_type(IdbLayerType::kLayerImplant);
    _min_width = -1;
    _spacing_list = new IdbLayerImplantSpacingList();
  }
  virtual ~IdbLayerImplant() { delete _spacing_list; }

  ////getter
  int32_t get_min_spacing()
  {
    if (_spacing_list->get_num() == 1) {
      auto* spacing = _spacing_list->get_min_spacing(0);
      return spacing == nullptr ? -1 : spacing->get_min_spacing();
    }
    return -1;
  }
  IdbLayerImplantSpacingList* get_min_spacing_list() { return _spacing_list; }
  int32_t get_min_width() { return _min_width; }

  /// setter
  void set_min_width(int32_t min_width) { _min_width = min_width; }

 private:
  int32_t _min_width;

  IdbLayerImplantSpacingList* _spacing_list;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class IdbLayers
{
 public:
  IdbLayers();
  ~IdbLayers();

  // getter
  vector<IdbLayer*>& get_layers() { return _layers; }
  vector<IdbLayer*>& get_routing_layers() { return _routing_layers; }
  int32_t get_routing_layers_number() { return _routing_layers.size(); }
  IdbLayerRouting* get_bottom_routing_layer()
  {
    return _routing_layers.size() > 0 ? dynamic_cast<IdbLayerRouting*>(_routing_layers[0]) : nullptr;
  }
  bool is_pr_layer(IdbLayer* layer) { return layer->get_order() >= get_bottom_routing_layer()->get_order(); }
  vector<IdbLayer*>& get_cut_layers() { return _cut_layers; }
  int32_t get_cut_layers_number() { return _cut_layers.size(); }
  const int32_t get_layers_num() { return _layers.size(); }
  int32_t get_layer_order(string layer_name);
  int32_t get_layer_order(IdbLayer* layer);
  vector<string> get_all_layer_name()
  {
    std::vector<string> layer_names;
    for (auto layer : _layers) {
      layer_names.push_back(layer->get_name());
    }

    return layer_names;
  }

  // setter
  void reset_layers();
  IdbLayer* set_layer(string layer_name, string type = "");
  void add_routing_layer(IdbLayer* layer) { _routing_layers.emplace_back(layer); };
  void add_cut_layer(IdbLayer* layer) { _cut_layers.emplace_back(layer); };

  // operator
  IdbLayer* find_layer(const string& src_name, bool new_layer = false);
  IdbLayer* find_layer(IdbLayer* src_layer);
  IdbLayer* find_routing_layer(uint32_t index);
  IdbLayer* find_middle_layer(string layer_name_1, string layer_name_2);
  IdbLayer* find_layer_by_order(uint8_t order);
  vector<IdbLayerCut*> find_cut_layer_list(string layer_name_1, string layer_name_2);

  // verify data
  void print();

 private:
  vector<IdbLayer*> _layers;
  vector<IdbLayer*> _routing_layers;
  vector<IdbLayer*> _cut_layers;
  uint8_t _z_order;
  int8_t _routing_layer_index;
  int8_t _cut_layer_index;
};

}  // namespace idb

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
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace idb::routinglayer {

// LEF58AREA
class Lef58Area
{
 public:
  class ExceptMinSize
  {
   public:
    ExceptMinSize() = default;
    ExceptMinSize(int32_t min_width, int32_t min_length) : _min_width(min_width), _min_length(min_length) {}

    [[nodiscard]] int32_t get_min_width() const { return _min_width; }
    [[nodiscard]] int32_t get_min_length() const { return _min_length; }
    void set_min_width(int32_t min_width) { _min_width = min_width; }
    void set_min_length(int32_t min_length) { _min_length = min_length; }

   private:
    int32_t _min_width{0};
    int32_t _min_length{0};
  };

  class ExceptEdgeLength
  {
   public:
    [[nodiscard]] std::optional<int32_t> get_min_edge_length() const { return _min_edge_length; }
    [[nodiscard]] int32_t get_max_edge_length() const { return _max_edge_length; }
    void set_min_edge_length(int32_t min_edge_length) { _min_edge_length = min_edge_length; }
    void set_max_edge_length(int32_t max_edge_length) { _max_edge_length = max_edge_length; }

   private:
    std::optional<int32_t> _min_edge_length;
    int32_t _max_edge_length{0};
  };

  class ExceptStep
  {
   public:
    ExceptStep() = default;
    ExceptStep(int32_t step_x, int32_t step_y) : _step_x(step_x), _step_y(step_y) {}
    [[nodiscard]] int32_t get_step_x() const { return _step_x; }
    [[nodiscard]] int32_t get_step_y() const { return _step_y; }
    void set_step_x(int32_t step_x) { _step_x = step_x; }
    void set_step_y(int32_t step_y) { _step_y = step_y; }

   private:
    int32_t _step_x{0};
    int32_t _step_y{0};
  };

  Lef58Area() = default;
  explicit Lef58Area(int64_t min_area) : _min_area(min_area) {}
  [[nodiscard]] int64_t get_min_area() const { return _min_area; }
  [[nodiscard]] std::optional<uint32_t> get_mask() const { return _mask; }
  [[nodiscard]] std::optional<int32_t> get_except_min_width() const { return _except_min_width; }
  [[nodiscard]] std::shared_ptr<ExceptEdgeLength> get_except_edge_length() const { return _except_edge_length; }
  [[nodiscard]] const std::vector<ExceptMinSize>& get_except_min_size() const { return _except_min_size; }
  [[nodiscard]] std::optional<ExceptStep> get_except_step() const { return _except_step; }
  [[nodiscard]] std::optional<int32_t> get_rect_width() const { return _rect_width; }
  [[nodiscard]] bool is_except_rectangle() const { return _except_rectangle; }
  [[nodiscard]] const std::string& get_trim_layer() const { return _trim_layer; }
  [[nodiscard]] std::optional<uint32_t> get_overlap() const { return _overlap; }

  void set_min_area(int64_t min_area) { _min_area = min_area; }
  void set_mask(uint32_t mask) { _mask = mask; }
  void set_except_min_width(int32_t except_min_width) { _except_min_width = except_min_width; }
  void set_except_edge_length(std::shared_ptr<ExceptEdgeLength> excpet_edge_length)
  {
    _except_edge_length = std::move(excpet_edge_length);
  };

  void add_except_min_size(ExceptMinSize except_min_size) { _except_min_size.push_back(except_min_size); }
  void set_except_step(ExceptStep except_step) { _except_step = except_step; }
  void set_rect_width(int32_t rect_width) { _rect_width = rect_width; }
  void set_except_rectangle(bool except_rectangle) { _except_rectangle = except_rectangle; }
  void set_trim_layer(std::string trim_layer) { _trim_layer = std::move(trim_layer); }
  void set_overlap(uint32_t overlap) { _overlap = overlap; }

 private:
  int64_t _min_area{0};
  std::optional<uint32_t> _mask;
  std::optional<int32_t> _except_min_width;
  std::shared_ptr<ExceptEdgeLength> _except_edge_length;
  std::vector<ExceptMinSize> _except_min_size;
  std::optional<ExceptStep> _except_step;
  std::optional<int32_t> _rect_width;
  bool _except_rectangle = false;
  std::string _trim_layer;
  std::optional<uint32_t> _overlap;
};

// LEF58CORNERFILLSPACING
class Lef58CornerFillSpacing
{
  /*
    [PROPERTY LEF58_CORNERFILLSPACING
        "CORNERFILLSPACING spacing EDGELENGTH length1 length2
        ADJACENTEOL eolWidth
      ; " ;]
  */
 public:
  [[nodiscard]] int32_t get_spacing() const { return _spacing; }
  [[nodiscard]] int32_t get_edge_length1() const { return _edge_length1; }
  [[nodiscard]] int32_t get_edge_length2() const { return _edge_length2; }
  [[nodiscard]] int32_t get_eol_width() const { return _eol_width; }
  void set_spacing(int32_t spacing) { _spacing = spacing; }
  void set_length1(int32_t length1) { _edge_length1 = length1; }
  void set_length2(int32_t length2) { _edge_length2 = length2; }
  void set_eol_width(int32_t eol_width) { _eol_width = eol_width; }

 private:
  int32_t _spacing{0};
  int32_t _edge_length1{0};
  int32_t _edge_length2{0};
  int32_t _eol_width{0};
};

// LEF58_CORNERSPACING
class Lef58CornerSpacing
{
 public:
  enum class CornerType
  {
    kNone,
    kConvexCorner,
    kConcaveCorner
  };

  class WidthSpacing
  {
   public:
    WidthSpacing() = default;
    WidthSpacing(int32_t width, int32_t spacing) : _width(width), _spacing(spacing) {}

    [[nodiscard]] int32_t get_width() const { return _width; }
    [[nodiscard]] int32_t get_spacing() const { return _spacing; }
    void set_width(int32_t width) { _width = width; }
    void set_spacing(int32_t spacing) { _spacing = spacing; }

   private:
    int32_t _width{0};
    int32_t _spacing{0};
  };

  [[nodiscard]] CornerType get_corner_type() const { return _corner_type; }
  [[nodiscard]] std::optional<int32_t> get_except_eol() const { return _except_eol; }
  [[nodiscard]] bool is_corner_to_corner() const { return _corner_to_corner; }
  [[nodiscard]] const std::vector<WidthSpacing>& get_width_spacing_list() const { return _width_spacing_list; }

  void set_corner_type(CornerType corner_type) { _corner_type = corner_type; }
  void set_except_eol(int32_t except_eol) { _except_eol = except_eol; }
  void set_corner_to_corner(bool value) { _corner_to_corner = value; }
  template <typename... Args>
  void add_width_spacing(Args&&... args)
  {
    _width_spacing_list.emplace_back(std::forward<Args>(args)...);
  }

 private:
  CornerType _corner_type = CornerType::kNone;
  std::optional<int32_t> _except_eol;
  bool _corner_to_corner = false;
  std::vector<WidthSpacing> _width_spacing_list;
};

// LEF58_MINIMUMCUT
class Lef58MinimumCut
{
 public:
  enum class Orient
  {
    kNone,
    kFromAbove,
    kFromBelow,
  };
  class CutClass
  {
   public:
    CutClass(std::string&& name, int32_t num_cuts) : _class_name(std::move(name)), _num_cuts(num_cuts) {}
    CutClass() = default;
    [[nodiscard]] const std::string& get_class_name() const { return _class_name; }
    [[nodiscard]] int32_t get_num_cuts() const { return _num_cuts; }
    void set_class_name(std::string&& name) { _class_name = std::move(name); }
    void set_num_cuts(int32_t num_cuts) { _num_cuts = num_cuts; }

   private:
    std::string _class_name;
    int32_t _num_cuts{0};
  };
  class Length
  {
   public:
    Length() = default;
    Length(int32_t length, int32_t distance) : _length(length), _distance(distance) {}
    [[nodiscard]] int32_t get_length() const { return _length; }
    [[nodiscard]] int32_t get_distance() const { return _distance; }
    void set_length(int32_t length) { _length = length; }
    void set_distance(int32_t distance) { _distance = distance; }

   private:
    int32_t _length{0};
    int32_t _distance{0};
  };

  class Area
  {
   public:
    Area() = default;
    explicit Area(int64_t area, std::optional<int32_t> distance = std::nullopt) : _area(area), _within_distance(distance) {}
    [[nodiscard]] int64_t get_area() const { return _area; }
    [[nodiscard]] std::optional<int32_t> get_within_distance() const { return _within_distance; }
    void set_area(int64_t area) { _area = area; }
    void set_within_distance(int32_t distance) { _within_distance = distance; }

   private:
    int64_t _area{0};
    std::optional<int32_t> _within_distance;
  };
  [[nodiscard]] std::optional<int32_t> get_num_cuts() const { return _num_cuts; }
  [[nodiscard]] const std::vector<CutClass>& get_cut_classes() const { return _cut_classes; }
  [[nodiscard]] int32_t get_width() const { return _width; }
  [[nodiscard]] std::optional<int32_t> get_within_cut_distance() const { return _within_cut_distance; }
  [[nodiscard]] Orient get_orient() const { return _orient; }
  [[nodiscard]] std::optional<Length> get_length() const { return _length; }
  [[nodiscard]] std::optional<Area> get_area() const { return _area; }
  [[nodiscard]] bool is_same_metal_overlap() const { return _same_metal_overlap; }
  [[nodiscard]] bool is_fully_enclosed() const { return _fully_enclosed; }
  void set_num_cuts(int32_t num_cuts) { _num_cuts = num_cuts; }
  void add_cutclass(CutClass&& cut_class) { _cut_classes.push_back(std::move(cut_class)); }
  void set_width(int32_t width) { _width = width; }
  void set_within_cut_distance(int32_t distance) { _within_cut_distance = distance; }
  void set_orient(Orient orient) { _orient = orient; }
  void set_orient(const std::string& orient)
  {
    if (orient == "FROMABOVE") {
      set_orient(Orient::kFromAbove);
    } else if (orient == "FROMBELOW") {
      set_orient(Orient::kFromBelow);
    } else {
      set_orient(Orient::kNone);
    }
  }
  void set_length(Length length) { _length = length; }
  void set_area(Area area) { _area = area; }
  void set_same_metal_overlap(bool same_metal_overlap) { _same_metal_overlap = same_metal_overlap; }
  void set_fully_enclosed(bool fully_enclosed) { _fully_enclosed = fully_enclosed; }

 private:
  std::optional<int32_t> _num_cuts;
  std::vector<CutClass> _cut_classes;
    int32_t _width{0};
    std::optional<int32_t> _within_cut_distance;
    Orient _orient{Orient::kNone};
  std::optional<Length> _length;
    std::optional<Area> _area;
    bool _same_metal_overlap{false};
    bool _fully_enclosed{false};
};

// LEF58_MINSTEP compatibility object. The eccdb-native schema lives in TechRoutingLef58MinStepRule.
class Lef58MinStep
{
 public:
  enum class Type
  {
    kNone,
    kInsideCorner,
    kOutsideCorner,
    kStep,
  };

  class MinAdjacentLength
  {
   public:
    explicit MinAdjacentLength(int32_t min_adj_length = 0) : _min_adj_length(min_adj_length) {}
    [[nodiscard]] int32_t get_min_adj_length() const { return _min_adj_length; }
    [[nodiscard]] std::optional<int32_t> get_min_adj_length2() const { return _min_adj_length2; }
    [[nodiscard]] bool is_convex_corner() const { return _convex_corner; }
    [[nodiscard]] bool is_concave_corner() const { return _concave_corner; }
    [[nodiscard]] std::optional<int32_t> get_except_within() const { return _except_within; }
    void set_min_adj_length(int32_t min_adj_length) { _min_adj_length = min_adj_length; }
    void set_min_adj_length2(int32_t min_adj_length2) { _min_adj_length2 = min_adj_length2; }
    void set_convex_corner(bool convex_corner) { _convex_corner = convex_corner; }
    void set_concave_corner(bool concave_corner) { _concave_corner = concave_corner; }
    void set_except_within(int32_t except_within) { _except_within = except_within; }

   private:
    int32_t _min_adj_length{0};
    std::optional<int32_t> _min_adj_length2;
    bool _convex_corner{false};
    bool _concave_corner{false};
    std::optional<int32_t> _except_within;
  };

  explicit Lef58MinStep(int32_t min_step_length = 0) : _min_step_length(min_step_length) {}
  [[nodiscard]] int32_t get_min_step_length() const { return _min_step_length; }
  [[nodiscard]] Type get_type() const { return _type; }
  [[nodiscard]] std::optional<int32_t> get_max_length_sum() const { return _max_length_sum; }
  [[nodiscard]] std::optional<int32_t> get_max_edges() const { return _max_edges; }
  [[nodiscard]] bool is_except_rectangle() const { return _except_rectangle; }
  [[nodiscard]] std::optional<MinAdjacentLength> get_min_adjacent_length() const { return _min_adjacent_length; }
  [[nodiscard]] bool has_three_concave_corners() const { return _three_concave_corners; }
  [[nodiscard]] std::optional<int32_t> get_center_width() const { return _center_width; }
  [[nodiscard]] std::optional<int32_t> get_min_between_length() const { return _min_between_length; }
  [[nodiscard]] bool is_except_same_corners() const { return _except_same_corners; }
  [[nodiscard]] std::optional<int32_t> get_no_adjacent_eol_width() const { return _no_adjacent_eol_width; }
  [[nodiscard]] std::optional<int32_t> get_except_adjacent_length() const { return _except_adjacent_length; }
  [[nodiscard]] std::optional<int32_t> get_followup_min_adjacent_length() const { return _followup_min_adjacent_length; }
  [[nodiscard]] bool has_concave_corners() const { return _concave_corners; }
  [[nodiscard]] std::optional<int32_t> get_no_between_eol_width() const { return _no_between_eol_width; }
  void set_min_step_length(int32_t min_step_length) { _min_step_length = min_step_length; }
  void set_type(Type type) { _type = type; }
  void set_type(const std::string& type)
  {
    if (type == "INSIDECORNER") {
      set_type(Type::kInsideCorner);
    } else if (type == "OUTSIDECORNER") {
      set_type(Type::kOutsideCorner);
    } else if (type == "STEP") {
      set_type(Type::kStep);
    } else {
      set_type(Type::kNone);
    }
  }
  void set_max_length_sum(int32_t max_length_sum) { _max_length_sum = max_length_sum; }
  void set_max_edges(int32_t max_edges) { _max_edges = max_edges; }
  void set_except_rectangle(bool except_rectangle) { _except_rectangle = except_rectangle; }
  void set_min_adjacent_length(MinAdjacentLength min_adjacent_length) { _min_adjacent_length = min_adjacent_length; }
  void set_three_concave_corners(bool three_concave_corners) { _three_concave_corners = three_concave_corners; }
  void set_center_width(int32_t center_width) { _center_width = center_width; }
  void set_min_between_length(int32_t min_between_length) { _min_between_length = min_between_length; }
  void set_except_same_corners(bool except_same_corners) { _except_same_corners = except_same_corners; }
  void set_no_adjacent_eol_width(int32_t no_adjacent_eol_width) { _no_adjacent_eol_width = no_adjacent_eol_width; }
  void set_except_adjacent_length(int32_t except_adjacent_length) { _except_adjacent_length = except_adjacent_length; }
  void set_followup_min_adjacent_length(int32_t min_adjacent_length) { _followup_min_adjacent_length = min_adjacent_length; }
  void set_concave_corners(bool concave_corners) { _concave_corners = concave_corners; }
  void set_no_between_eol_width(int32_t no_between_eol_width) { _no_between_eol_width = no_between_eol_width; }

 private:
  int32_t _min_step_length = 0;
  Type _type = Type::kNone;
  std::optional<int32_t> _max_length_sum;
  std::optional<int32_t> _max_edges;
  bool _except_rectangle = false;
  std::optional<MinAdjacentLength> _min_adjacent_length;
  bool _three_concave_corners = false;
  std::optional<int32_t> _center_width;
  std::optional<int32_t> _min_between_length;
  bool _except_same_corners = false;
  std::optional<int32_t> _no_adjacent_eol_width;
  std::optional<int32_t> _except_adjacent_length;
  std::optional<int32_t> _followup_min_adjacent_length;
  bool _concave_corners = false;
  std::optional<int32_t> _no_between_eol_width;
};

// LEF58_SPACING "SPACING minSpacing NOTCHLENGTH ..."
class Lef58SpacingNotchlength
{
 public:
  Lef58SpacingNotchlength() = default;
  Lef58SpacingNotchlength(int32_t min_spacing, int32_t min_notch_length) : _min_spacing(min_spacing), _min_notch_length(min_notch_length) {}
  [[nodiscard]] int32_t get_min_spacing() const { return _min_spacing; }
  [[nodiscard]] int32_t get_min_notch_length() const { return _min_notch_length; }
  [[nodiscard]] std::optional<int32_t> get_concave_ends_side_of_notch_width() const { return _concave_ends_side_of_notch_width; }
  void set_min_spacing(int32_t min_spacing) { _min_spacing = min_spacing; }
  void set_min_notch_length(int32_t min_notch_length) { _min_notch_length = min_notch_length; }
  void set_concave_ends_side_of_notch_width(int32_t width) { _concave_ends_side_of_notch_width = width; }

 private:
  int32_t _min_spacing{0};
  int32_t _min_notch_length{0};
  std::optional<int32_t> _concave_ends_side_of_notch_width;
};

// LEF58SPACINGEOL
class Lef58SpacingEol
{
 public:
  enum class Direction
  {
    kNone,
    kBelow,
    kAbove,
  };
  class EndToEnd
  {
   public:
    [[nodiscard]] int32_t get_end_to_end_space() const { return _end_to_end_space; }
    [[nodiscard]] std::optional<int32_t> get_one_cut_space() const { return _one_cut_space; }
    [[nodiscard]] std::optional<int32_t> get_two_cut_space() const { return _two_cut_space; }
    [[nodiscard]] std::optional<int32_t> get_extionsion() const { return _extionsion; }
    [[nodiscard]] std::optional<int32_t> get_wrong_dir_extionsion() const { return _wrong_dir_extension; }
    [[nodiscard]] std::optional<int32_t> get_other_end_width() const { return _other_end_width; }

    void set_end_to_end_space(int32_t space) { _end_to_end_space = space; }
    void set_one_cut_space(int32_t space) { _one_cut_space = space; }
    void set_two_cut_space(int32_t space) { _two_cut_space = space; }
    void set_extionsion(int32_t extionsion) { _extionsion = extionsion; }
    void set_wrong_dir_extionsion(int32_t wrong_dir_extionsion) { _wrong_dir_extension = wrong_dir_extionsion; }
    void set_other_end_width(int32_t other_end_width) { _other_end_width = other_end_width; }

   private:
    int32_t _end_to_end_space{0};
    std::optional<int32_t> _one_cut_space;
    std::optional<int32_t> _two_cut_space;
    std::optional<int32_t> _extionsion;
    std::optional<int32_t> _wrong_dir_extension;
    std::optional<int32_t> _other_end_width;
  };

  class AdjEdgeLength
  {
   public:
    [[nodiscard]] std::optional<int32_t> get_max_length() const { return _max_length; }
    [[nodiscard]] std::optional<int32_t> get_min_length() const { return _min_length; }
    [[nodiscard]] bool is_two_sides() const { return _two_sides; }
    void set_max_length(int32_t max_length) { _max_length = max_length; }
    void set_min_length(int32_t min_length) { _min_length = min_length; }
    void set_two_sides(bool two_sides) { _two_sides = two_sides; }

   private:
    // [MAXLENGTH maxLength | MINLENGTH minLength [TWOSIDES]
    std::optional<int32_t> _max_length;
    std::optional<int32_t> _min_length;
    bool _two_sides{false};
  };
  class ParallelEdge
  {
   public:
    [[nodiscard]] int32_t get_par_space() const { return _par_space; }
    [[nodiscard]] bool is_subtract_eol_width() const { return _subtract_eol_width; }
    [[nodiscard]] int32_t get_par_within() const { return _par_within; }
    [[nodiscard]] std::optional<int32_t> get_prl() const { return _prl; }
    [[nodiscard]] std::optional<int32_t> get_min_length() const { return _min_length; }
    [[nodiscard]] bool is_two_edges() const { return _two_edges; }
    [[nodiscard]] bool is_same_metal() const { return _same_metal; }
    [[nodiscard]] bool is_non_eol_corner_only() const { return _non_eol_corner_only; }
    [[nodiscard]] bool is_parallel_same_mask() const { return _parallel_same_mask; }

    void set_par_space(int32_t par_space) { _par_space = par_space; }
    void set_subtract_eol_width(bool subtract_eol_width) { _subtract_eol_width = subtract_eol_width; }
    void set_par_within(int32_t par_within) { _par_within = par_within; }
    void set_prl(int32_t prl) { _prl = prl; }
    void set_min_length(int32_t min_length) { _min_length = min_length; }
    void set_two_edges(bool two_edges) { _two_edges = two_edges; }
    void set_same_metal(bool same_metal) { _same_metal = same_metal; }
    void set_non_eol_corner_only(bool non_eol_corner_only) { _non_eol_corner_only = non_eol_corner_only; }
    void set_parallel_same_mask(bool parallel_same_mask) { _parallel_same_mask = parallel_same_mask; }

   private:
    int32_t _par_space{0};
    bool _subtract_eol_width{false};
    int32_t _par_within{0};
    std::optional<int32_t> _prl;
    std::optional<int32_t> _min_length;
    bool _two_edges{false};
    bool _same_metal{false};
    bool _non_eol_corner_only{false};
    bool _parallel_same_mask{false};
  };

  class EncloseCut
  {
   public:
    [[nodiscard]] Direction get_direction() const { return _direction; }
    [[nodiscard]] int32_t get_enclose_dist() const { return _enclose_dist; }
    [[nodiscard]] int32_t get_cut_to_metal_space() const { return _cut_to_metal_space; }
    [[nodiscard]] bool is_all_cuts() const { return _all_cuts; }

    void set_direction(Direction direction) { _direction = direction; }
    void set_direction(const std::string& direction)
    {
      if (direction == "ABOVE") {
        set_direction(Direction::kAbove);
      } else if (direction == "BELOW") {
        set_direction(Direction::kBelow);
      } else {
        set_direction(Direction::kNone);
      }
    }
    void set_enclose_dist(int32_t dist) { _enclose_dist = dist; }
    void set_cut_to_metal_space(int32_t space) { _cut_to_metal_space = space; }
    void set_all_cuts(bool all_cuts) { _all_cuts = all_cuts; }

   private:
    Direction _direction{Direction::kNone};
    int32_t _enclose_dist{0};
    int32_t _cut_to_metal_space{0};
    bool _all_cuts{false};
  };

  class ExceptExactWidth
  {
   public:
    ExceptExactWidth() = default;
    ExceptExactWidth(int32_t width1, int32_t width2) : _width1(width1), _width2(width2) {}
    [[nodiscard]] int32_t get_width1() const { return _width1; }
    [[nodiscard]] int32_t get_width2() const { return _width2; }
    void set_width1(int32_t width1) { _width1 = width1; }
    void set_width2(int32_t width2) { _width2 = width2; }

   private:
    int32_t _width1{0};
    int32_t _width2{0};
  };

  class WithCut
  {
   public:
    [[nodiscard]] const std::string& get_cutclass() const { return _cutclass; }
    [[nodiscard]] bool is_above() const { return _above; }
    [[nodiscard]] int32_t get_with_cut_space() const { return _with_cut_space; }
    [[nodiscard]] std::optional<int32_t> get_enclosure_end_width() const { return _enclosure_end_width; }
    [[nodiscard]] std::optional<int32_t> get_enclosure_end_within() const { return _enclosure_end_within; }

    void set_cutclass(std::string cutclass) { _cutclass = std::move(cutclass); }
    void set_above(bool above) { _above = above; }
    void set_with_cut_space(int32_t with_cut_space) { _with_cut_space = with_cut_space; }
    void set_enclosure_end_width(int32_t enclosure_end_width) { _enclosure_end_width = enclosure_end_width; }
    void set_enclosure_end_within(int32_t enclosure_end_within) { _enclosure_end_within = enclosure_end_within; }

   private:
    std::string _cutclass;
    bool _above{false};
    int32_t _with_cut_space{0};
    std::optional<int32_t> _enclosure_end_width;
    std::optional<int32_t> _enclosure_end_within;
  };

  class EndPrlSpacing
  {
   public:
    EndPrlSpacing() = default;
    EndPrlSpacing(int32_t end_prl_space, int32_t end_prl) : _end_prl_space(end_prl_space), _end_prl(end_prl) {}
    [[nodiscard]] int32_t get_end_prl_space() const { return _end_prl_space; }
    [[nodiscard]] int32_t get_end_prl() const { return _end_prl; }
    void set_end_prl_space(int32_t end_prl_space) { _end_prl_space = end_prl_space; }
    void set_end_prl(int32_t end_prl) { _end_prl = end_prl; }

   private:
    int32_t _end_prl_space{0};
    int32_t _end_prl{0};
  };

  class ToConcaveCorner
  {
   public:
    [[nodiscard]] std::optional<int32_t> get_min_length() const { return _min_length; }
    [[nodiscard]] std::optional<int32_t> get_min_adj_length1() const { return _min_adj_length1; }
    [[nodiscard]] std::optional<int32_t> get_min_adj_length2() const { return _min_adj_length2; }
    void set_min_length(int32_t min_length) { _min_length = min_length; }
    void set_min_adj_length1(int32_t min_adj_length1) { _min_adj_length1 = min_adj_length1; }
    void set_min_adj_length2(int32_t min_adj_length2) { _min_adj_length2 = min_adj_length2; }

   private:
    std::optional<int32_t> _min_length;
    std::optional<int32_t> _min_adj_length1;
    std::optional<int32_t> _min_adj_length2;
  };

  [[nodiscard]] int32_t get_eol_space() const { return _eol_space; }
  [[nodiscard]] int32_t get_eol_width() const { return _eol_width; }
  [[nodiscard]] bool is_exact_width() const { return _exact_width; }
  [[nodiscard]] std::optional<int32_t> get_wrong_dir_space() const { return _wrong_dir_space; }
  [[nodiscard]] std::optional<int32_t> get_opposite_width() const { return _opposite_width; }
  [[nodiscard]] std::optional<int32_t> get_eol_within() const { return _eol_within; }
  [[nodiscard]] std::optional<int32_t> get_wrong_dir_within() const { return _wrong_dir_within; }
  [[nodiscard]] bool is_same_mask() const { return _same_mask; }
  [[nodiscard]] std::optional<ExceptExactWidth> get_except_exact_width() const { return _except_exact_width; }
  [[nodiscard]] std::optional<int32_t> get_fill_concave_corner_width() const { return _fill_concave_corner_width; }
  [[nodiscard]] std::optional<WithCut> get_with_cut() const { return _with_cut; }
  [[nodiscard]] std::optional<EndPrlSpacing> get_end_prl_spacing() const { return _end_prl_spacing; }
  [[nodiscard]] std::optional<EndToEnd> get_end_to_end() const { return _end_to_end; }
  [[nodiscard]] std::optional<AdjEdgeLength> get_adj_edge_length() const { return _adj_edge_length; }
  [[nodiscard]] bool is_equal_rect_width() const { return _equal_rect_width; }
  [[nodiscard]] std::optional<ParallelEdge> get_parallel_edge() const { return _parallel_edge; }
  [[nodiscard]] std::optional<EncloseCut> get_enclose_cut() const { return _enclose_cut; }
  [[nodiscard]] std::optional<ToConcaveCorner> get_to_concave_corner() const { return _to_concave_corner; }
  [[nodiscard]] std::optional<int32_t> get_notch_length() const { return _notch_length; }
  void set_eol_space(int32_t eol_space) { _eol_space = eol_space; }
  void set_eol_width(int32_t eol_width) { _eol_width = eol_width; }
  void set_exact_width(bool exact_width) { _exact_width = exact_width; }
  void set_wrong_dir_space(int32_t wrong_dir_space) { _wrong_dir_space = wrong_dir_space; }
  void set_opposite_width(int32_t opposite_width) { _opposite_width = opposite_width; }
  void set_eol_within(int32_t eol_within) { _eol_within = eol_within; }
  void set_wrong_dir_within(int32_t wrong_dir_within) { _wrong_dir_within = wrong_dir_within; }
  void set_same_mask(bool same_mask) { _same_mask = same_mask; }
  void set_except_exact_width(ExceptExactWidth except_exact_width) { _except_exact_width = except_exact_width; }
  void set_fill_concave_corner_width(int32_t fill_concave_corner_width) { _fill_concave_corner_width = fill_concave_corner_width; }
  void set_with_cut(WithCut with_cut) { _with_cut = std::move(with_cut); }
  void set_end_prl_spacing(EndPrlSpacing end_prl_spacing) { _end_prl_spacing = end_prl_spacing; }
  void set_end_to_end(EndToEnd end_to_end) { _end_to_end = end_to_end; }
  void set_adj_edge_length(AdjEdgeLength adj_edge_length) { _adj_edge_length = adj_edge_length; }
  void set_equal_rect_width(bool equal_rect_width) { _equal_rect_width = equal_rect_width; }
  void set_parallel_edge(ParallelEdge parallel_edge) { _parallel_edge = parallel_edge; }
  void set_enclose_cut(EncloseCut enclose_cut) { _enclose_cut = enclose_cut; }
  void set_to_concave_corner(ToConcaveCorner to_concave_corner) { _to_concave_corner = to_concave_corner; }
  void set_notch_length(int32_t notch_length) { _notch_length = notch_length; }

 private:
  int32_t _eol_space{0};
  int32_t _eol_width{0};
  bool _exact_width{false};
  std::optional<int32_t> _wrong_dir_space;
  std::optional<int32_t> _opposite_width;
  std::optional<int32_t> _eol_within;
  std::optional<int32_t> _wrong_dir_within;
  bool _same_mask = false;
  std::optional<ExceptExactWidth> _except_exact_width;
  std::optional<int32_t> _fill_concave_corner_width;
  std::optional<WithCut> _with_cut;
  std::optional<EndPrlSpacing> _end_prl_spacing;
  std::optional<EndToEnd> _end_to_end;
  std::optional<AdjEdgeLength> _adj_edge_length;
  bool _equal_rect_width = false;
  std::optional<ParallelEdge> _parallel_edge;
  std::optional<EncloseCut> _enclose_cut;
  std::optional<ToConcaveCorner> _to_concave_corner;
  std::optional<int32_t> _notch_length;
};

// LEF58_WIDTHTABLE "WIDTHTABLE {width}... [WRONGDIRECTION] [ORTHOGONAL] ;"
class Lef58WidthTable
{
 public:
  [[nodiscard]] const std::vector<int32_t>& get_widths() const { return _widths; }
  [[nodiscard]] bool is_wrong_direction() const { return _wrong_direction; }
  [[nodiscard]] bool is_orthogonal() const { return _orthogonal; }

  void add_width(int32_t width) { _widths.push_back(width); }
  void set_wrong_direction(bool wrong_direction) { _wrong_direction = wrong_direction; }
  void set_orthogonal(bool orthogonal) { _orthogonal = orthogonal; }

 private:
  std::vector<int32_t> _widths;
  bool _wrong_direction = false;
  bool _orthogonal = false;
};

// LEF58_SPACINGTABLE "SPACINGTABLE PARALLELRUNLENGTH ..."
class Lef58SpacingTablePrl
{
 public:
  class Width
  {
   public:
    Width() = default;
    Width(int32_t width, std::vector<int32_t> spacings) : _width(width), _spacings(std::move(spacings)) {}

    [[nodiscard]] int32_t get_width() const { return _width; }
    [[nodiscard]] const std::vector<int32_t>& get_spacings() const { return _spacings; }
    [[nodiscard]] std::optional<std::pair<int32_t, int32_t>> get_except_within() const { return _except_within; }
    void set_width(int32_t width) { _width = width; }
    void set_spacings(std::vector<int32_t> spacings) { _spacings = std::move(spacings); }
    void set_except_within(int32_t low, int32_t high) { _except_within = std::make_pair(low, high); }

   private:
    int32_t _width = 0;
    std::optional<std::pair<int32_t, int32_t>> _except_within;
    std::vector<int32_t> _spacings;
  };

  class Influence
  {
   public:
    Influence() = default;
    Influence(int32_t width, int32_t within, int32_t spacing) : _width(width), _within(within), _spacing(spacing) {}

    [[nodiscard]] int32_t get_width() const { return _width; }
    [[nodiscard]] int32_t get_within() const { return _within; }
    [[nodiscard]] int32_t get_spacing() const { return _spacing; }

   private:
    int32_t _width = 0;
    int32_t _within = 0;
    int32_t _spacing = 0;
  };

  [[nodiscard]] bool is_wrong_direction() const { return _wrong_direction; }
  [[nodiscard]] bool is_same_mask() const { return _same_mask; }
  [[nodiscard]] std::optional<int32_t> get_except_eol_width() const { return _except_eol_width; }
  [[nodiscard]] const std::vector<int32_t>& get_parallel_run_lengths() const { return _parallel_run_lengths; }
  [[nodiscard]] const std::vector<Width>& get_widths() const { return _widths; }
  [[nodiscard]] const std::vector<Influence>& get_influences() const { return _influences; }

  void set_wrong_direction(bool wrong_direction) { _wrong_direction = wrong_direction; }
  void set_same_mask(bool same_mask) { _same_mask = same_mask; }
  void set_except_eol_width(int32_t width) { _except_eol_width = width; }
  void add_parallel_run_length(int32_t length) { _parallel_run_lengths.push_back(length); }
  void add_width(Width width) { _widths.push_back(std::move(width)); }
  void add_influence(Influence influence) { _influences.push_back(std::move(influence)); }

 private:
  bool _wrong_direction = false;
  bool _same_mask = false;
  std::optional<int32_t> _except_eol_width;
  std::vector<int32_t> _parallel_run_lengths;
  std::vector<Width> _widths;
  std::vector<Influence> _influences;
};

// LEF58_SPACINGTABLE "SPACINGTABLE JOGTOJOGSPACING ..."
class Lef58SpacingTableJogToJog
{
 public:
  class Width
  {
   public:
    Width() = default;
    Width(int32_t width, int32_t par_len, int32_t par_within, int32_t long_jog_spacing)
        : _width(width), _par_length(par_len), _par_within(par_within), _long_jog_spacing(long_jog_spacing)
    {
    }
    [[nodiscard]] int32_t get_width() const { return _width; }
    [[nodiscard]] int32_t get_par_length() const { return _par_length; }
    [[nodiscard]] int32_t get_par_within() const { return _par_within; }
    [[nodiscard]] int32_t get_long_jog_spacing() const { return _long_jog_spacing; }
    void set_width(int32_t width) { _width = width; }
    void set_par_length(int32_t par_length) { _par_length = par_length; }
    void set_par_within(int32_t par_within) { _par_within = par_within; }
    void set_long_jog_spacing(int32_t long_jog_spacing) { _long_jog_spacing = long_jog_spacing; }

   private:
    int32_t _width{0};
    int32_t _par_length{0};
    int32_t _par_within{0};
    int32_t _long_jog_spacing{0};
  };

  Lef58SpacingTableJogToJog() = default;
  Lef58SpacingTableJogToJog(int32_t jog_to_jog_spacing, int32_t jog_width, int32_t short_jog_spacing)
      : _jog_to_jog_spacing(jog_to_jog_spacing), _jog_width(jog_width), _short_jog_spacing(short_jog_spacing)
  {
  }
  [[nodiscard]] int32_t get_jog_to_jog_spacing() const { return _jog_to_jog_spacing; }
  [[nodiscard]] int32_t get_jog_width() const { return _jog_width; }
  [[nodiscard]] int32_t get_short_jog_spacing() const { return _short_jog_spacing; }
  [[nodiscard]] std::vector<Width>& get_width_list() { return _width_list; }
  void set_jog_to_jog_spacing(int32_t jog_to_jog_spacing) { _jog_to_jog_spacing = jog_to_jog_spacing; }
  void set_jog_width(int32_t jog_width) { _jog_width = jog_width; }
  void set_short_jog_spacing(int32_t short_jog_spacing) { _short_jog_spacing = short_jog_spacing; }
  template <typename... Args>
  void add_width(Args&&... args)
  {
    _width_list.emplace_back(std::forward<Args>(args)...);
  }

 private:
  int32_t _jog_to_jog_spacing{0};
  int32_t _jog_width{0};
  int32_t _short_jog_spacing{0};
  std::vector<Width> _width_list;
};
}  // namespace idb::routinglayer

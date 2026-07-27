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
/**
 * @file itfiVia.cpp
 * @brief Legacy ITF parser data structure implementation detail.
 */
#include <algorithm>
#include <cmath>

#include "itfiVia.hpp"

namespace itf
{
namespace
{

const char* stringOrNull(const std::string& value)
{
  return value.empty() ? nullptr : value.c_str();
}

} // namespace

itfiEVCAGS::itfiEVCAGS()
: _number_of_tables(0),
  _tables()
{

}

itfiEVCAGS::~itfiEVCAGS()
{
  clear();
}

int
itfiEVCAGS::get_table_count() const
{
  return _number_of_tables;
}

void
itfiEVCAGS::set_table_count(int n)
{ 
  _number_of_tables = n;
}

void
itfiEVCAGS::add_table(const char* title,
                      const itf2DLUT<float, float, float>& lut) {
  _tables.push_back(itfTitleLut<float, float, float>(title, lut));
}

bool
itfiEVCAGS::operator==(const itfiEVCAGS& rhs) const
{
  if (this == &rhs) return true;

  return _number_of_tables == rhs._number_of_tables
    && _tables == rhs._tables
  ;
}

void
itfiEVCAGS::clear()
{
  _tables.clear();
  _number_of_tables = 0;
}

itfiVia::itfiVia()
: _via_name(),
  _from(),
  _to(),
  _wmin(std::nullopt),
  _smin(std::nullopt),
  _layer_type(),
  _side_tangent(std::nullopt),
  _side_tangent_pair(std::nullopt),
  _crt1(std::nullopt),
  _crt2(std::nullopt),
  _crt_vs_area(),
  _t0(0),
  _has_t0(false),
  _rho(std::nullopt),
  _rpv(std::nullopt),
  _area(std::nullopt),
  _rpv_vs_area(),
  _etch_cg(),
  _etch_vws(),
  _etch_vwl(),
  _etch_vwl_type(),
  _capacitive_only_etch(0)
{ }

bool
itfiVia::operator==(const itfiVia& rhs) const
{
  if (this == &rhs) return true;

  return _via_name == rhs._via_name
    && _from == rhs._from
    && _to == rhs._to
    && _wmin == rhs._wmin
    && _smin == rhs._smin
    && _layer_type == rhs._layer_type
    && _side_tangent == rhs._side_tangent
    && _side_tangent_pair == rhs._side_tangent_pair
    && _crt1 == rhs._crt1
    && _crt2 == rhs._crt2
    && _crt_vs_area == rhs._crt_vs_area
    && _t0 == rhs._t0
    && _has_t0 == rhs._has_t0
    && _rho == rhs._rho
    && _rpv == rhs._rpv
    && _area == rhs._area
    && _rpv_vs_area == rhs._rpv_vs_area
    && _etch_cg == rhs._etch_cg
    && _etch_vws == rhs._etch_vws
    && _etch_vwl == rhs._etch_vwl
    && _etch_vwl_type == rhs._etch_vwl_type
    && _capacitive_only_etch == rhs._capacitive_only_etch
  ;
}

void
itfiVia::clear() {
  _via_name.clear();
  _from.clear();
  _to.clear();
  _wmin.reset();
  _smin.reset();
  _layer_type.clear();
  _side_tangent.reset();
  _side_tangent_pair.reset();
  _crt1.reset();
  _crt2.reset();
  _crt_vs_area.clear();
  _t0 = 0;
  _has_t0 = false;
  _rho.reset();
  _rpv.reset();
  _area.reset();
  _rpv_vs_area.clear();
  _etch_cg.clear();
  _etch_vws.clear();
  _etch_vwl.clear();
  _etch_vwl_type.clear();
  _capacitive_only_etch = 0;
}

itfiEVCAGS&
itfiVia::get_etch_contact_gate()
{
  return _etch_cg;
}

const itfiEVCAGS&
itfiVia::get_etch_contact_gate() const
{
  return _etch_cg;
}

const char*
itfiVia::get_via_name() const
{
  return stringOrNull(_via_name);
}

std::optional<float>
itfiVia::get_wmin() const
{
  return _wmin;
}

std::optional<float>
itfiVia::get_smin() const
{
  return _smin;
}

const char*
itfiVia::get_layer_type() const
{
  return stringOrNull(_layer_type);
}

std::optional<float>
itfiVia::get_side_tangent() const
{
  return _side_tangent;
}

std::optional<std::pair<float, float>>
itfiVia::get_side_tangent_pair() const
{
  return _side_tangent_pair;
}

std::optional<float>
itfiVia::get_rho() const
{
  return _rho;
}

std::optional<float>
itfiVia::get_rpv() const
{ 
  return _rpv;
}

const std::vector<itfiAreaRpv>&
itfiVia::get_rpv_by_area() const
{
  return _rpv_vs_area;
}

std::optional<float> 
itfiVia::get_area() const
{
  return _area;
}

std::optional<float>
itfiVia::get_crt1() const
{
  return _crt1;
}

std::optional<float>
itfiVia::get_crt2() const
{
  return _crt2;
}

const std::vector<itfiAreaCrt>&
itfiVia::get_crt_by_area() const
{
  return _crt_vs_area;
}

float
itfiVia::get_t0() const
{
  return _t0;
}

bool
itfiVia::has_t0() const
{
  return _has_t0;
}

const char*
itfiVia::get_from() const
{
  return stringOrNull(_from);
}

const char*
itfiVia::get_to() const
{
  return stringOrNull(_to);
}

const itf2DLUT<float, float, std::pair<float, float>>&
itfiVia::get_etch_width_length() const
{
  return _etch_vwl;
}

const std::string&
itfiVia::get_etch_width_length_type() const
{
  return _etch_vwl_type;
}

bool
itfiVia::has_etch_width_length() const
{
  return !_etch_vwl.get_rows().empty() && !_etch_vwl.get_cols().empty();
}

bool
itfiVia::use_etch_width_length_for_resistance() const
{
  return has_etch_width_length() && _etch_vwl_type != "CAPACITIVE_ONLY";
}

bool
itfiVia::use_etch_width_length_for_capacitance() const
{
  return has_etch_width_length() && _etch_vwl_type != "RESISTIVE_ONLY";
}

std::optional<std::pair<float, float>>
itfiVia::query_etch_width_length(double width,
                                 double length) const
{
  return _etch_vwl.query_interpolation(static_cast<float>(width), static_cast<float>(length));
}

double
itfiVia::query_rpv_by_area(double area) const
{
  const auto& table = get_rpv_by_area();
  if (table.empty()) {
    return 0.0;
  }
  if (table.size() == 1) {
    return table.front().rpv;
  }

  auto it_high = std::upper_bound(
    table.begin(), table.end(), area,
    [](double query_area, const itfiAreaRpv& elem) {
      return query_area < elem.area;
    });

  if (it_high == table.end()) {
    return table.back().rpv;
  }
  if (area <= table.front().area) {
    return table.front().rpv;
  }

  const size_t high_idx = std::distance(table.begin(), it_high);
  const size_t low_idx = high_idx - 1;
  const double reciprocal_rpv_low = 1.0 / table.at(low_idx).rpv;
  const double reciprocal_rpv_high = 1.0 / table.at(high_idx).rpv;
  const double ratio =
    (area - table.at(low_idx).area) / (table.at(high_idx).area - table.at(low_idx).area);
  const double reciprocal_rpv = std::lerp(reciprocal_rpv_low, reciprocal_rpv_high, ratio);
  return 1.0 / reciprocal_rpv;
}

void
itfiVia::query_crt_by_area(double area,
                           std::optional<double>& crt1,
                           std::optional<double>& crt2) const
{
  crt1.reset();
  crt2.reset();

  const auto& table = get_crt_by_area();
  if (table.empty()) {
    return;
  }
  if (table.size() == 1) {
    crt1 = table.front().crt1;
    crt2 = table.front().crt2;
    return;
  }

  auto it_high = std::upper_bound(
    table.begin(), table.end(), area,
    [](double query_area, const itfiAreaCrt& elem) {
      return query_area < elem.area;
    });

  if (it_high == table.end()) {
    crt1 = table.back().crt1;
    crt2 = table.back().crt2;
    return;
  }
  if (area <= table.front().area) {
    crt1 = table.front().crt1;
    crt2 = table.front().crt2;
    return;
  }

  const size_t high_idx = std::distance(table.begin(), it_high);
  const size_t low_idx = high_idx - 1;
  const double ratio =
    (area - table.at(low_idx).area) / (table.at(high_idx).area - table.at(low_idx).area);
  crt1 = std::lerp(table.at(low_idx).crt1, table.at(high_idx).crt1, ratio);
  crt2 = std::lerp(table.at(low_idx).crt2, table.at(high_idx).crt2, ratio);
}

void
itfiVia::set_via_name(const char* name)
{
  _via_name = name ? name : "";
}

void
itfiVia::set_from(const char* from)
{
  _from = from ? from : "";
}

void
itfiVia::set_to(const char* to)
{
  _to = to ? to : "";
}

void
itfiVia::set_wmin(float v)
{
  _wmin = v;
}

void
itfiVia::set_smin(float v)
{
  _smin = v;
}

void
itfiVia::set_layer_type(const char* v)
{
  _layer_type = v ? v : "";
}

void
itfiVia::set_side_tangent(float v)
{
  _side_tangent = v;
  _side_tangent_pair.reset();
}

void
itfiVia::set_side_tangent(float coco,
                          float poco)
{
  _side_tangent.reset();
  _side_tangent_pair = std::make_pair(coco, poco);
}

void
itfiVia::set_crt1(float v)
{
  _crt1 = v;
}

void
itfiVia::set_crt2(float v)
{
  _crt2 = v;
}

void
itfiVia::add_area_crt1_crt2(float area,
                            float crt1,
                            float crt2)
{
  _crt_vs_area.emplace_back(area, crt1, crt2);
}

void
itfiVia::set_t0(float t)
{
  _t0 = t;
  _has_t0 = true;
}

void
itfiVia::set_rho(float r)
{
  _rho = r;
}

void
itfiVia::set_rpv(float v)
{
  _rpv = v;
}

void
itfiVia::set_area(float v)
{
  _area = v;
}

void
itfiVia::add_area_rpv(float area,
                      float rpv)
{
  _rpv_vs_area.emplace_back(area, rpv);
}

void
itfiVia::set_etch_width_spacing(const char* title,
                                const itf2DLUT<float, float, float>& lut)
{
  _etch_vws.set_title(title);
  _etch_vws.set_lut(lut);
}

void
itfiVia::set_etch_width_length(const char* type,
                               const itf2DLUT<float, float, std::pair<float, float>>& lut)
{
  _etch_vwl_type = type ? type : "";
  _etch_vwl = lut;
}

void
itfiVia::set_capacitive_only_etch(float v)
{
  _capacitive_only_etch = v;
}

} // namespace itf

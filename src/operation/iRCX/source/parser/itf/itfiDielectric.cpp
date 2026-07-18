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
 * @file itfiDielectric.cpp
 * @brief Legacy ITF parser data structure implementation detail.
 */
#include "itfiDielectric.hpp"

namespace itf
{
namespace
{

const char* stringOrNull(const std::string& value)
{
  return value.empty() ? nullptr : value.c_str();
}

} // namespace

itfiDielectric::itfiDielectric()
: _dielectric_name(),
  _er(0),
  _thickness(0),
  _measured_from(),
  _sw_t(0),
  _tw_t(0),
  _associated_conductor(),
  _damage_thickness(0),
  _damage_er(0),
  _has_measured_from(0),
  _has_sw_t(0),
  _has_tw_t(0),
  _has_associated_conductor(0),
  _has_measured_from_conductor(0),
  _is_conformal(0),
  _has_damage_thickness(0),
  _has_damage_er(0)
{

}

bool
itfiDielectric::operator==(const itfiDielectric& rhs) const
{
  if (this == &rhs) return true;

  return _dielectric_name == rhs._dielectric_name
    && _er == rhs._er
    && _thickness == rhs._thickness
    && _measured_from == rhs._measured_from
    && _sw_t == rhs._sw_t
    && _tw_t == rhs._tw_t
    && _associated_conductor == rhs._associated_conductor
    && _damage_thickness == rhs._damage_thickness
    && _damage_er == rhs._damage_er
    
    && _has_measured_from == rhs._has_measured_from
    && _has_sw_t == rhs._has_sw_t
    && _has_tw_t == rhs._has_tw_t
    && _has_associated_conductor == rhs._has_associated_conductor
    && _has_measured_from_conductor == rhs._has_measured_from_conductor
    && _is_conformal == rhs._is_conformal
    && _has_damage_thickness == rhs._has_damage_thickness
    && _has_damage_er == rhs._has_damage_er
  ;
} 

const char*
itfiDielectric::get_dielectric_name() const
{
  return stringOrNull(_dielectric_name);
}

float
itfiDielectric::get_er() const
{
  return _er;
}

float
itfiDielectric::get_thickness() const
{
  return _thickness;
}

const char*
itfiDielectric::get_measured_from() const {
  return stringOrNull(_measured_from);
}

float
itfiDielectric::get_sw_t() const {
  return _sw_t;
}

float
itfiDielectric::get_tw_t() const {
  return _tw_t;
}

const char*
itfiDielectric::get_associated_conductor() const {
  return stringOrNull(_associated_conductor);
}

float
itfiDielectric::get_damage_thickness() const {
  return _damage_thickness;
}

float
itfiDielectric::get_damage_er() const {
  return _damage_er;
}

void
itfiDielectric::set_dielectric_name(const char* layer_name)
{
  _dielectric_name = layer_name ? layer_name : "";
}

void
itfiDielectric::set_er(float er)
{
  _er = er;
}

void
itfiDielectric::set_thickness(float thickness)
{
  _thickness = thickness;
}

void
itfiDielectric::set_measured_from(const char* from)
{
  _measured_from = from ? from : "";
  _has_measured_from = 1;
}

void
itfiDielectric::set_sw_t(float sw_t)
{
  _sw_t = sw_t;
  _has_sw_t = 1;
}

void
itfiDielectric::set_tw_t(float tw_t)
{
  _tw_t = tw_t;
  _has_tw_t = 1;
}

void
itfiDielectric::set_associated_conductor(const char* conductor)
{
  _associated_conductor = conductor ? conductor : "";
  _has_associated_conductor = 1;
}

void
itfiDielectric::set_measured_from_conductor()
{
  _has_measured_from_conductor = 1;
}

void
itfiDielectric::set_conformal() {
  _is_conformal = 1;
}

void
itfiDielectric::set_damage_thickness(float thickness)
{
  _damage_thickness = thickness;
  _has_damage_thickness = 1;
}

void
itfiDielectric::set_damage_er(float er)
{
  _damage_er = er;
  _has_damage_er = 1;
}

int
itfiDielectric::has_measured_from() const
{
  return _has_measured_from;
}

int
itfiDielectric::has_sw_t() const
{
  return _has_sw_t;
}

int
itfiDielectric::has_tw_t() const
{
  return _has_tw_t;
}

int
itfiDielectric::has_associated_conductor() const
{
  return _has_associated_conductor;
}

int
itfiDielectric::has_measured_from_conductor() const
{
  return _has_measured_from_conductor;
}

int
itfiDielectric::is_conformal() const
{
  return _is_conformal;
}

int
itfiDielectric::has_damage_thickness() const
{
  return _has_damage_thickness;
}

int
itfiDielectric::has_damage_er() const
{
  return _has_damage_er;
}

void
itfiDielectric::clear()
{
  _dielectric_name.clear();
  _er = 0;
  _thickness = 0;
  _measured_from.clear();
  _sw_t = 0;
  _tw_t = 0;
  _associated_conductor.clear();
  _damage_thickness = 0;
  _damage_er = 0;

  _has_measured_from = 0;
  _has_sw_t = 0;
  _has_tw_t = 0;
  _has_associated_conductor = 0;
  _has_measured_from_conductor = 0;
  _is_conformal = 0;
  _has_damage_thickness = 0;
  _has_damage_er = 0;
}

} // namespace itf

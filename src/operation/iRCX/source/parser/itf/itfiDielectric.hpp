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
 * @file itfiDielectric.hpp
 * @brief Legacy ITF parser data structure implementation detail.
 */
#pragma once 

#include <string>

namespace itf
{

class itfiDielectric {
 public:
  // constructor
  itfiDielectric();
  itfiDielectric(const itfiDielectric&) = default;
  ~itfiDielectric() = default;

  // getter
  const char* get_dielectric_name() const;
  float get_er() const;
  float get_thickness() const;
  const char* get_measured_from() const;
  float get_sw_t() const;
  float get_tw_t() const;
  const char* get_associated_conductor() const;
  float get_damage_thickness() const;
  float get_damage_er() const;
  int has_measured_from() const;
  int has_sw_t() const;
  int has_tw_t() const;
  int has_associated_conductor() const;
  int has_measured_from_conductor() const;
  int is_conformal() const;
  int has_damage_thickness() const;
  int has_damage_er() const;

  // setter
  void set_dielectric_name(const char*);
  void set_er(float);
  void set_thickness(float);
  void set_measured_from(const char*);
  void set_sw_t(float);
  void set_tw_t(float);
  void set_associated_conductor(const char*);
  void set_measured_from_conductor();
  void set_damage_thickness(float);
  void set_damage_er(float);
  void set_conformal();

  // operator
  itfiDielectric& operator=(const itfiDielectric&) = default;
  bool operator==(const itfiDielectric&) const;

  // function
  void clear();

 private:
  // members
  std::string _dielectric_name;
  float _er; // Relative permittivity
  float _thickness; // Units: microns
  std::string _measured_from;
  float _sw_t; // The sidewall thickness. Units: microns
  float _tw_t; // Topwall thickness. Units: microns
  std::string _associated_conductor;
  float _damage_thickness; // Units: microns
  float _damage_er; // Equivalent permittivity

  unsigned _has_measured_from : 1;
  unsigned _has_sw_t : 1;
  unsigned _has_tw_t : 1;
  unsigned _has_associated_conductor : 1;
  unsigned _has_measured_from_conductor : 1;
  unsigned _is_conformal : 1;
  unsigned _has_damage_thickness : 1;
  unsigned _has_damage_er : 1;
};

} // namespace itf

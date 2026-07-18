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
 * @file itfiVia.hpp
 * @brief Legacy ITF parser data structure implementation detail.
 */
#pragma once

#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "itf2DLUT.hpp"

namespace itf
{

// Etch Vs Contact And Gate Spacings
class itfiEVCAGS {
 public:
  // constructor
  itfiEVCAGS();
  ~itfiEVCAGS();

  // getter
  int get_table_count() const;

  // setter
  void set_table_count(int);
  void add_table(const char*,
                 const itf2DLUT<float, float, float>&);

  // operator
  bool operator==(const itfiEVCAGS&) const;

  // function
  void clear();
  
 private:
  // members
  int _number_of_tables;
  // (gate_to_contact, contact_to_contact) -> values
  std::vector<itfTitleLut<float, float, float>> _tables;
};

struct itfiAreaRpv {
  double area;
  double rpv;

  bool operator==(const itfiAreaRpv& rhs) const {
    return (area == rhs.area) && (rpv == rhs.rpv);
  }
};

struct itfiAreaCrt {
  double area;
  double crt1;
  double crt2;

  bool operator==(const itfiAreaCrt& rhs) const {
    return (area == rhs.area) && (crt1 == rhs.crt1) && (crt2 == rhs.crt2);
  }
};

class itfiVia {
 public:
  // constructor
  itfiVia();
  itfiVia(const itfiVia&) = default;
  ~itfiVia() = default;

  // getter
  itfiEVCAGS& get_etch_contact_gate();
  const itfiEVCAGS& get_etch_contact_gate() const;
  const char* get_via_name() const;
  std::optional<float> get_wmin() const;
  std::optional<float> get_smin() const;
  const char* get_layer_type() const;
  std::optional<float> get_side_tangent() const;
  std::optional<std::pair<float, float>> get_side_tangent_pair() const;
  std::optional<float> get_rho() const;
  std::optional<float> get_rpv() const;
  const std::vector<itfiAreaRpv>& get_rpv_by_area() const;
  std::optional<float> get_area() const;
  std::optional<float> get_crt1() const;
  std::optional<float> get_crt2() const;
  const std::vector<itfiAreaCrt>& get_crt_by_area() const;
  float get_t0() const;
  bool has_t0() const;
  const char* get_from() const;
  const char* get_to() const;
  const itf2DLUT<float, float, std::pair<float, float>>& get_etch_width_length() const;
  const std::string& get_etch_width_length_type() const;
  bool has_etch_width_length() const;
  bool use_etch_width_length_for_resistance() const;
  bool use_etch_width_length_for_capacitance() const;
  std::optional<std::pair<float, float>> query_etch_width_length(double width,
                                                                 double length) const;
  double query_rpv_by_area(double) const;
  void query_crt_by_area(double,
                         std::optional<double>&,
                         std::optional<double>&) const;

  // setter
  void set_via_name(const char*);
  void set_from(const char*);
  void set_to(const char*);
  void set_wmin(float);
  void set_smin(float);
  void set_layer_type(const char*);
  void set_side_tangent(float);
  void set_side_tangent(float,
                        float);
  void set_crt1(float);
  void set_crt2(float);
  void add_area_crt1_crt2(float,
                          float,
                          float);
  void set_t0(float);
  void set_rho(float);
  void set_rpv(float);
  void set_area(float);
  void add_area_rpv(float,
                    float);
  void set_etch_width_spacing(const char*,
                              const itf2DLUT<float, float, float>&);
  void set_etch_width_length(const char*,
                             const itf2DLUT<float, float, std::pair<float, float>>&);
  void set_capacitive_only_etch(float);

  // operator
  itfiVia& operator=(const itfiVia&) = default;
  bool operator==(const itfiVia&) const;

  // function
  void clear();

 private:
  // members
  std::string _via_name;
  std::string _from;
  std::string _to;
  std::optional<float> _wmin;
  std::optional<float> _smin;
  std::string _layer_type;
  std::optional<float> _side_tangent;
  std::optional<std::pair<float, float>> _side_tangent_pair;
  std::optional<float> _crt1;
  std::optional<float> _crt2;
  std::vector<itfiAreaCrt> _crt_vs_area;
  float _t0;
  bool _has_t0;
  std::optional<float> _rho; // resistivity. Units: ohms-micron
  std::optional<float> _rpv;
  std::optional<float> _area;  // Area of default via. Units: square microns
  std::vector<itfiAreaRpv> _rpv_vs_area; // (RPV, area). Units: (ohms, square microns)
  itfiEVCAGS _etch_cg; //etch_vs_contact_and_gate_spacings
  itfTitleLut<float, float, float> _etch_vws; // etch_vs_width_and_spacing;
  // etch_vs_width_and_length; (widths, lengths) -> values)
  itf2DLUT<float, float, std::pair<float, float>> _etch_vwl;
  std::string _etch_vwl_type; // empty: capacitance and resistance; CAPACITIVE_ONLY; RESISTIVE_ONLY
  float _capacitive_only_etch;
};
  
} // namespace itf

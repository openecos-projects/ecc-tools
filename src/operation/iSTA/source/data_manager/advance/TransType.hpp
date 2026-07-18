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

#include "Logger.hpp"

namespace ista {

enum class TransType
{
  kNone,
  kRise,
  kFall
};

struct GetTransTypeName
{
  std::string operator()(const TransType& trans_type) const
  {
    std::string trans_type_name;
    switch (trans_type) {
      case TransType::kNone:
        trans_type_name = "none";
        break;
      case TransType::kRise:
        trans_type_name = "rise";
        break;
      case TransType::kFall:
        trans_type_name = "fall";
        break;
      default:
        STALOG.error(Loc::current(), "Unrecognized type!");
        break;
    }
    return trans_type_name;
  }
};

struct GetTransTypeInitial
{
  std::string operator()(const TransType& trans_type) const
  {
    std::string trans_type_initial;
    switch (trans_type) {
      case TransType::kNone:
        trans_type_initial = "";
        break;
      case TransType::kRise:
        trans_type_initial = "r";
        break;
      case TransType::kFall:
        trans_type_initial = "f";
        break;
      default:
        STALOG.error(Loc::current(), "Unrecognized type!");
        break;
    }
    return trans_type_initial;
  }
};

struct GetTransTypeLibName
{
  std::string operator()(const TransType& trans_type) const
  {
    std::string trans_type_lib_name;
    switch (trans_type) {
      case TransType::kNone:
        trans_type_lib_name = "none";
        break;
      case TransType::kRise:
        trans_type_lib_name = "rising";
        break;
      case TransType::kFall:
        trans_type_lib_name = "falling";
        break;
      default:
        STALOG.error(Loc::current(), "Unrecognized type!");
        break;
    }
    return trans_type_lib_name;
  }
};

struct GetTransTypeLibEdgeName
{
  std::string operator()(const TransType& trans_type) const
  {
    std::string trans_type_lib_edge_name;
    switch (trans_type) {
      case TransType::kNone:
        trans_type_lib_edge_name = "none";
        break;
      case TransType::kRise:
        trans_type_lib_edge_name = "rising_edge";
        break;
      case TransType::kFall:
        trans_type_lib_edge_name = "falling_edge";
        break;
      default:
        STALOG.error(Loc::current(), "Unrecognized type!");
        break;
    }
    return trans_type_lib_edge_name;
  }
};

}  // namespace ista

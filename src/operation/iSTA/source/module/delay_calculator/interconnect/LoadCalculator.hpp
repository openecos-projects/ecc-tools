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
#pragma once

#include <string>

#include "Database.hpp"
#include "PinCapacitanceCalculator.hpp"
#include "RcTreeAnalyzer.hpp"

namespace ista {
class LoadCalculator
{
 public:
  LoadCalculator(Database& database, PinCapacitanceCalculator& pin_capacitance, RcTreeAnalyzer& rc)
      : _database(database), _pin_capacitance(pin_capacitance), _rc(rc)
  {
  }
  LoadCalculator(const LoadCalculator&) = delete;
  LoadCalculator& operator=(const LoadCalculator&) = delete;
  double arcLoad(Arc& arc, AnalysisType analysis_type, TransType output_trans_type);
  double pinLoad(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type);

 private:
  double netLoad(Net& net, const std::string& driver_pin, AnalysisType analysis_type, TransType output_trans_type);
  double netPinLoad(Net& net, const std::string& driver_pin, AnalysisType analysis_type, TransType trans_type);
  double parasiticLoad(const std::string& driver_pin, ParasiticNet& parasitic_net, AnalysisType analysis_type, TransType trans_type);
  Database& _database;
  PinCapacitanceCalculator& _pin_capacitance;
  RcTreeAnalyzer& _rc;
};
}  // namespace ista

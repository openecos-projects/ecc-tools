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
#include "ArnoldiResponseCalculator.hpp"
#include "Database.hpp"
#include "DmpResponseCalculator.hpp"
#include "InputPortResponseCalculator.hpp"
#include "RcTreeAnalyzer.hpp"

namespace ista {
// Evaluates interconnect delay and receiver slew using cached driver responses or RC fallback.
class WireDelayCalculator
{
 public:
  WireDelayCalculator(Database& database, RcTreeAnalyzer& rc, DmpResponseCalculator& dmp, ArnoldiResponseCalculator& arnoldi,
                      InputPortResponseCalculator& input_port)
      : _database(database), _rc(rc), _dmp(dmp), _arnoldi(arnoldi), _input_port(input_port), _threshold(database)
  {
  }
  WireDelayCalculator(const WireDelayCalculator&) = delete;
  WireDelayCalculator& operator=(const WireDelayCalculator&) = delete;
  double calcNetArcSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew);
  double calcNetArcDelay(Arc& arc);
  double calcNetArcDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type);
  double calcNetArcDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew);

 private:
  void markFailed(Arc& arc, AnalysisType analysis, TransType edge);
  double calcParasiticSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew);
  double calcParasiticDelay(Arc& arc);
  double calcParasiticDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type);
  double calcParasiticDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew);
  Database& _database;
  RcTreeAnalyzer& _rc;
  DmpResponseCalculator& _dmp;
  ArnoldiResponseCalculator& _arnoldi;
  InputPortResponseCalculator& _input_port;
  ReceiverThresholdCalculator _threshold;
};
}  // namespace ista

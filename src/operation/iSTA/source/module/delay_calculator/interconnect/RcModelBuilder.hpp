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

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "Config.hpp"
#include "Database.hpp"
#include "ParasiticArnoldiModel.hpp"
#include "ParasiticDmpModel.hpp"
#include "RcTreeAnalyzer.hpp"

namespace ista {
// Builds and caches reduced RC models; no arc-result or propagated-state ownership.
class RcModelBuilder
{
 public:
  RcModelBuilder(Database& database, const Config& config, RcTreeAnalyzer& rc) : _database(database), _config(config), _rc(rc) {}
  RcModelBuilder(const RcModelBuilder&) = delete;
  RcModelBuilder& operator=(const RcModelBuilder&) = delete;
  void clear()
  {
    _parasitic_dmp_model_cache.clear();
    _parasitic_arnoldi_model_cache.clear();
  }
  ParasiticDmpModel& dmpModel(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type, TransType trans_type);
  ParasiticArnoldiModel& arnoldiModel(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type, TransType trans_type);

 private:
  using ArnoldiModelKey = std::tuple<std::string, std::string, AnalysisType, TransType>;

  std::string getParasiticDmpModelKey(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type, TransType trans_type);
  ParasiticDmpModel buildParasiticDmpModel(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type, TransType trans_type);
  void buildParasiticDmpPiModel(ParasiticDmpModel& dmp_model, std::vector<int32_t>& parent_idx_list, std::vector<double>& resistance_list,
                                std::vector<double>& capacitance_list);
  void buildParasiticDmpLoadModelList(ParasiticDmpModel& dmp_model, std::vector<std::string>& node_name_list, std::vector<int32_t>& parent_idx_list,
                                      std::vector<double>& resistance_list, std::vector<double>& capacitance_list, std::string& source_node_name);
  ParasiticDmpLoadModel buildParasiticDmpLoadModel(double moment1, double moment2, double moment3);
  ArnoldiModelKey getParasiticArnoldiModelKey(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type, TransType trans_type);
  ParasiticArnoldiModel buildParasiticArnoldiModel(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                                   TransType trans_type);
  void initParasiticArnoldiTerm(ParasiticArnoldiModel& arnoldi_model, std::vector<std::string>& node_name_list, std::string& source_node_name);
  void updateParasiticArnoldiModel(ParasiticArnoldiModel& arnoldi_model, ParasiticNet& parasitic_net, std::vector<std::string>& node_name_list,
                                   std::vector<int32_t>& parent_idx_list, std::vector<double>& resistance_list, std::vector<double>& capacitance_list,
                                   std::vector<std::size_t>& term_point_idx_list);
  void updateParasiticArnoldiProjection(ParasiticArnoldiModel& arnoldi_model, std::vector<double>& basis_list, std::vector<std::size_t>& term_point_idx_list,
                                        std::size_t order_idx);
  Database& _database;
  const Config& _config;
  RcTreeAnalyzer& _rc;
  std::map<std::string, ParasiticDmpModel> _parasitic_dmp_model_cache;
  std::map<ArnoldiModelKey, ParasiticArnoldiModel> _parasitic_arnoldi_model_cache;
};
}  // namespace ista

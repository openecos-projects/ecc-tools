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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "AnalysisType.hpp"
#include "ParasiticArnoldiModel.hpp"
#include "ParasiticDelayResult.hpp"
#include "ParasiticDmpModel.hpp"
#include "PWHeader.hpp"
#include "TransType.hpp"

namespace ipw {

class DCModel
{
 public:
  DCModel() = default;
  ~DCModel() = default;
  // getter
  std::map<std::string, std::map<std::string, std::vector<std::pair<std::string, double>>>>& get_parasitic_resistor_map_cache()
  {
    return _parasitic_resistor_map_cache;
  }
  std::map<std::string, std::map<AnalysisType, std::map<TransType, std::map<std::string, double>>>>& get_parasitic_load_map_cache()
  {
    return _parasitic_load_map_cache;
  }
  std::map<std::string, std::map<AnalysisType, std::map<TransType, std::map<std::string, double>>>>& get_parasitic_delay_map_cache()
  {
    return _parasitic_delay_map_cache;
  }
  std::map<std::string, std::map<AnalysisType, std::map<TransType, std::map<std::string, double>>>>& get_parasitic_impulse_map_cache()
  {
    return _parasitic_impulse_map_cache;
  }
  std::map<std::string, ParasiticDmpModel>& get_parasitic_dmp_model_cache() { return _parasitic_dmp_model_cache; }
  std::map<std::string, ParasiticDelayResult>& get_parasitic_dmp_timing_result_cache() { return _parasitic_dmp_timing_result_cache; }
  std::map<std::string, ParasiticDelayResult>& get_parasitic_dmp_driver_result_cache() { return _parasitic_dmp_driver_result_cache; }
  std::map<ParasiticArnoldiModelKey, ParasiticArnoldiModel>& get_parasitic_arnoldi_model_cache() { return _parasitic_arnoldi_model_cache; }
  std::map<ParasiticArnoldiTimingResultKey, ParasiticDelayResult>& get_parasitic_arnoldi_timing_result_cache()
  {
    return _parasitic_arnoldi_timing_result_cache;
  }
  std::map<ParasiticArnoldiDriverResultKey, ParasiticDelayResult>& get_parasitic_arnoldi_driver_result_cache()
  {
    return _parasitic_arnoldi_driver_result_cache;
  }
  std::map<ParasiticArnoldiDriverResultKey, ParasiticDelayResult>& get_parasitic_input_port_result_cache()
  {
    return _parasitic_input_port_result_cache;
  }
  // function
  void clear()
  {
    _parasitic_resistor_map_cache.clear();
    _parasitic_load_map_cache.clear();
    _parasitic_delay_map_cache.clear();
    _parasitic_impulse_map_cache.clear();
    _parasitic_dmp_model_cache.clear();
    _parasitic_dmp_timing_result_cache.clear();
    _parasitic_dmp_driver_result_cache.clear();
    _parasitic_arnoldi_model_cache.clear();
    _parasitic_arnoldi_timing_result_cache.clear();
    _parasitic_arnoldi_driver_result_cache.clear();
    _parasitic_input_port_result_cache.clear();
  }

 private:
  std::map<std::string, std::map<std::string, std::vector<std::pair<std::string, double>>>> _parasitic_resistor_map_cache;
  std::map<std::string, std::map<AnalysisType, std::map<TransType, std::map<std::string, double>>>> _parasitic_load_map_cache;
  std::map<std::string, std::map<AnalysisType, std::map<TransType, std::map<std::string, double>>>> _parasitic_delay_map_cache;
  std::map<std::string, std::map<AnalysisType, std::map<TransType, std::map<std::string, double>>>> _parasitic_impulse_map_cache;
  std::map<std::string, ParasiticDmpModel> _parasitic_dmp_model_cache;
  std::map<std::string, ParasiticDelayResult> _parasitic_dmp_timing_result_cache;
  std::map<std::string, ParasiticDelayResult> _parasitic_dmp_driver_result_cache;
  std::map<ParasiticArnoldiModelKey, ParasiticArnoldiModel> _parasitic_arnoldi_model_cache;
  std::map<ParasiticArnoldiTimingResultKey, ParasiticDelayResult> _parasitic_arnoldi_timing_result_cache;
  std::map<ParasiticArnoldiDriverResultKey, ParasiticDelayResult> _parasitic_arnoldi_driver_result_cache;
  std::map<ParasiticArnoldiDriverResultKey, ParasiticDelayResult> _parasitic_input_port_result_cache;
};

}  // namespace ipw

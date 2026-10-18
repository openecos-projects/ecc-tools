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

#include "ArcTimingLookup.hpp"
#include "DCTask.hpp"
#include "ParasiticArnoldiSolver.hpp"
#include "ParasiticDmpSolver.hpp"

namespace ipw {

class Arc;
class TimingCellArc;

#define PWDC (ipw::DelayCalculator::getInst())

class DelayCalculator
{
 public:
  static void initInst();
  static DelayCalculator& getInst();
  static void destroyInst();
  // function
  void init();
  void calculate(DCTask& dc_task);
  void destroy();
  double getPowerOutputLoad(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type);
  std::vector<TransType> getOutputTransTypeList(Arc& arc, TransType input_trans_type);
  bool calculateDrivingCell(
      std::string& output_pin,
      TimingCellArc& timing_cell_arc,
      AnalysisType analysis_type,
      TransType output_trans_type,
      double input_transition_rise,
      double input_transition_fall,
      double& output_slew
  );

 private:
  // self
  static DelayCalculator* _dc_instance;
  // 寄生延时求解器（无状态；缓存走 DelayCache::threadCache()）
  ParasiticDmpSolver _dmp_solver;
  // Arnoldi 降阶求解器（无状态；缓存走 DelayCache::threadCache()）
  ParasiticArnoldiSolver _arnoldi_solver;
  // 弧级查询（持有两个求解器的引用，按 Arnoldi → DMP → 查表 取结果）
  ArcTimingLookup _arc_timing_lookup{_arnoldi_solver, _dmp_solver};

  DelayCalculator() = default;
  DelayCalculator(const DelayCalculator& other) = delete;
  DelayCalculator(DelayCalculator&& other) = delete;
  ~DelayCalculator() = default;
  DelayCalculator& operator=(const DelayCalculator& other) = delete;
  DelayCalculator& operator=(DelayCalculator&& other) = delete;
  // function
  void clearParasiticCache();
};

}  // namespace ipw

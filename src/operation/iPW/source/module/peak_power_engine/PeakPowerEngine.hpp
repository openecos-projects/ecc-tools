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

#include "Database.hpp"
#include "PWHeader.hpp"
#include "VcdParser.hh"

namespace ipw {

#define PWPE (ipw::PeakPowerEngine::getInst())

class PeakPowerEngine
{
 public:
  static void initInst();
  static PeakPowerEngine& getInst();
  static void destroyInst();
  // function
  void run(const std::string& vcd_file_path);

 private:
  // Windows are handed out round-robin, so every worker walks the waveform but analyzes only its
  // own windows. The merge of the per-worker peaks keeps the earliest window on a tie.
  struct WindowWorkerResult
  {
    std::size_t window_num = 0;
    std::size_t truncated_window_num = 0;
    std::size_t last_window_index = 0;
    bool has_window = false;
    bool is_open = false;
  };

  // self
  static PeakPowerEngine* _pe_instance;

  PeakPowerEngine() = default;
  PeakPowerEngine(const PeakPowerEngine& other) = delete;
  PeakPowerEngine(PeakPowerEngine&& other) = delete;
  ~PeakPowerEngine() = default;
  PeakPowerEngine& operator=(const PeakPowerEngine& other) = delete;
  PeakPowerEngine& operator=(PeakPowerEngine&& other) = delete;
  // function
  double check(const std::string& vcd_file_path);
  // 峰值流程用 PTPX time_based 口径（X 折算、初始 X 处理见 VcdTransitionModel.hh）。
  vcd::ActivityModelOptions buildActivityModelOptions() const;
  double resolveWindowLength();
  int32_t resolveThreadNum();
  WindowWorkerResult analyzeWindows(
      const std::string& vcd_file_path,
      const double window_length,
      const int32_t worker_id,
      const int32_t worker_num,
      Database::PowerWorkspace& workspace
  );
  void mergeWorkerResults(
      const std::vector<WindowWorkerResult>& worker_result_list,
      const std::vector<Database::PowerWorkspace>& workspace_list,
      std::size_t& window_num,
      std::size_t& truncated_window_num
  );
  void updatePeakPower(const vcd::VcdWindowSignalActivity& window_activity);
};

}  // namespace ipw

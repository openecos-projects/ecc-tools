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

#include <string>
#include <vector>

#include "Types.hh"

namespace ircx {

class RCXConfig
{
 public:
  struct CornerConfig
  {
    std::string name;
    std::string itf_file;
    std::string captab_file;
  };

  RCXConfig() = default;
  ~RCXConfig() = default;

  [[nodiscard]] bool loadFromFile(const std::string& config_path);

  [[nodiscard]] const std::string& get_config_path() const { return _config_path; }
  [[nodiscard]] unsigned get_thread_num() const { return _thread_num; }
  [[nodiscard]] F64 get_operating_temperature() const { return _operating_temperature; }
  [[nodiscard]] const std::string& get_output_dir() const { return _output_dir; }
  [[nodiscard]] const std::string& get_mapping_file() const { return _mapping_file; }
  [[nodiscard]] const std::vector<CornerConfig>& get_corners() const { return _corners; }

 private:
  std::string _config_path;
  unsigned _thread_num = 64U;
  F64 _operating_temperature = 25.0;
  std::string _output_dir;
  std::string _mapping_file;
  std::vector<CornerConfig> _corners;
};

}  // namespace ircx

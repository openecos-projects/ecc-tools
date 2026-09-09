// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#pragma once

#include "EMIRHeader.hpp"

namespace iemir {

struct PTPXPowerRecord
{
  std::string instance_name;
  double voltage = 0.0;
  double internal_power = 0.0;
  double switching_power = 0.0;
  double leakage_power = 0.0;
  double total_power = 0.0;
  double average_current = 0.0;
};

class PTPXPowerReader
{
 public:
  static std::vector<PTPXPowerRecord> read(const std::string& file_path);
};

}  // namespace iemir

// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#pragma once

#include "Database.hpp"

namespace ista {

class TimingCaseAnalysis
{
 public:
  static void apply(Database& database);
  static bool allowsTransition(const Database& database, std::string_view pin_name, TransType trans_type);
};

}  // namespace ista

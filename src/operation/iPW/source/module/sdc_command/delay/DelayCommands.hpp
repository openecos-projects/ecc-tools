// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#pragma once

#include "frontend/SdcTclCmd.hpp"

namespace ipw::sdc {

class TclSetInputTransition : public SdcTclCmd
{
 public:
  TclSetInputTransition(const char* cmd_name);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclSetDrivingCell : public SdcTclCmd
{
 public:
  TclSetDrivingCell(const char* cmd_name);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclSetLoad : public SdcTclCmd
{
 public:
  TclSetLoad(const char* cmd_name);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

}  // namespace ipw::sdc

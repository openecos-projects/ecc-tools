// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#pragma once

#include "SdcTclCmd.hpp"
#include "query/ObjectCommands.hpp"

namespace ista::sdc {





class TclGetDesigns : public SdcTclCmd
{
 public:
  TclGetDesigns(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclRemoveFromCollection : public SdcTclCmd
{
 public:
  TclRemoveFromCollection(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclGetFullName : public SdcTclCmd
{
 public:
  TclGetFullName(const char* cmd_name, ClientData client_data);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclGetGeneratedClocks : public TclGetClocks
{
 public:
  TclGetGeneratedClocks(const char* cmd_name, ClientData client_data);
  unsigned exec() override;
};

}  // namespace ista::sdc

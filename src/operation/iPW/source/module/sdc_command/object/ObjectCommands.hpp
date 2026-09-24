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

class TclGetPins : public SdcTclCmd
{
 public:
  TclGetPins(const char* cmd_name);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclGetCells : public SdcTclCmd
{
 public:
  TclGetCells(const char* cmd_name);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclGetNets : public SdcTclCmd
{
 public:
  TclGetNets(const char* cmd_name);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclAllClocks : public SdcTclCmd
{
 public:
  TclAllClocks(const char* cmd_name);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclGetObjectName : public SdcTclCmd
{
 public:
  TclGetObjectName(const char* cmd_name);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclGetPorts : public SdcTclCmd
{
 public:
  TclGetPorts(const char* cmd_name);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclGetClocks : public SdcTclCmd
{
 public:
  TclGetClocks(const char* cmd_name);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclGetGeneratedClocks : public TclGetClocks
{
 public:
  TclGetGeneratedClocks(const char* cmd_name);
  unsigned exec() override;
};

class TclCurrentDesign : public SdcTclCmd
{
 public:
  TclCurrentDesign(const char* cmd_name);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclRemoveFromCollection : public SdcTclCmd
{
 public:
  TclRemoveFromCollection(const char* cmd_name);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclAllInputs : public SdcTclCmd
{
 public:
  TclAllInputs(const char* cmd_name);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

class TclAllOutputs : public SdcTclCmd
{
 public:
  TclAllOutputs(const char* cmd_name);
  unsigned check() override { return 1; }
  unsigned exec() override;
};

}  // namespace ipw::sdc

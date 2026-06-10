// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************

#pragma once

#include <string>

namespace idb {

class IdbDefService;

class ViewJsonEditApplier
{
 public:
  explicit ViewJsonEditApplier(IdbDefService* def_service);

  bool apply(const std::string& edits_path);

 private:
  IdbDefService* _def_service = nullptr;
};

}  // namespace idb

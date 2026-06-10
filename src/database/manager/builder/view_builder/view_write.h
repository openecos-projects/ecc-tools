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

bool writeViewJson(IdbDefService* def_service, const std::string& output_dir);
bool applyViewJsonEdits(IdbDefService* def_service, const std::string& edits_path);

}  // namespace idb

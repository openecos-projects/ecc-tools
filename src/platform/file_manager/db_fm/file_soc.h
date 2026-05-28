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

#include <algorithm>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <fstream>
#include <vector>

#include "file_manager.h"
#include "json.hpp"

using std::string;
using std::vector;

namespace idb {

using json = nlohmann::ordered_json;

class JsonSoc : public iplf::FileManager
{
 public:
  explicit JsonSoc(string data_path, std::vector<std::string> harden_cores) : iplf::FileManager(data_path), _harden_cores(harden_cores) {}

  ~JsonSoc() = default;

  virtual bool readFile() override;
  virtual bool saveFileData() override;

 private:
  bool saveJson();
  bool readJson();

 private:
   std::vector<std::string> _harden_cores;

  void parseJson(std::string path = "");

  bool is_exist_harden_core(std::string core_name) {
    return std::find(_harden_cores.begin(), _harden_cores.end(), core_name) != _harden_cores.end();
  }
};

}  // namespace idb

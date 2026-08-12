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
#include <fstream>

#include "RCXInterface.hpp"

int main()
{
  std::ofstream config_file_stream("ircx.config");
  config_file_stream << R"({
  "thread_num": 1,
  "mapping_file": "mapping.map",
  "corners": [
    {
      "name": "TYPICAL",
      "itf_file": "typical.itf",
      "captab_file": "typical.captab"
    }
  ]
})";
  config_file_stream.close();

  std::map<std::string, std::any> config_map;
  config_map["-config"] = std::string("ircx.config");

  RCXI.initRCX(config_map);
  RCXI.runRCX();
  RCXI.destroyRCX();
  ircx::RCXInterface::destroyInst();

  std::filesystem::remove("ircx.config");
  std::filesystem::remove_all("rcx_temp_directory");
  return EXIT_SUCCESS;
}

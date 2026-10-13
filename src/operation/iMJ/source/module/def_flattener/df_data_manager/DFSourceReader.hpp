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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "DFSource.hpp"
#include "defrReader.hpp"

namespace imj {

class DFSourceReader
{
 public:
  DFSourceReader() = default;
  ~DFSourceReader() = default;
  DFSourceReader(const DFSourceReader& other) = delete;
  DFSourceReader(DFSourceReader&& other) = delete;
  DFSourceReader& operator=(const DFSourceReader& other) = delete;
  DFSourceReader& operator=(DFSourceReader&& other) = delete;
  // function
  bool read(DFSource& df_source);

 private:
  DFSource* _df_source = nullptr;
  // function
  bool readDef();
  bool readGzipDef();
  static int32_t readDesign(defrCallbackType_e type, const char* design_name, defiUserData data);
  static int32_t readDieArea(defrCallbackType_e type, defiBox* die_area, defiUserData data);
  static int32_t readPin(defrCallbackType_e type, defiPin* pin, defiUserData data);
};

}  // namespace imj

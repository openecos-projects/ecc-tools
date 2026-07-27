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

#include <cstdint>
#include <string>

#include "Config.hpp"
#include "DataManager.hpp"
#include "Database.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"

namespace ilvs {

#define LVSSIO (ilvs::LVSSnapshotIO::getInst())

enum class LVSSnapshotType : uint32_t
{
  kLogical = 1,
  kPhysical = 2,
};

class LVSSnapshotIO
{
 public:
  static void initInst();
  static LVSSnapshotIO& getInst();
  static void destroyInst();
  // function
#if 1  // snapshot
  bool write(const Netlist& netlist, LVSSnapshotType snapshot_type, const std::string& file_path, std::string& error_message);
  bool read(const std::string& file_path, LVSSnapshotType expected_snapshot_type, Netlist& netlist, std::string& error_message);
#endif

 private:
  // self
  static LVSSnapshotIO* _sio_instance;

  LVSSnapshotIO() = default;
  LVSSnapshotIO(const LVSSnapshotIO& other) = delete;
  LVSSnapshotIO(LVSSnapshotIO&& other) = delete;
  ~LVSSnapshotIO() = default;
  LVSSnapshotIO& operator=(const LVSSnapshotIO& other) = delete;
  LVSSnapshotIO& operator=(LVSSnapshotIO&& other) = delete;
};

}  // namespace ilvs

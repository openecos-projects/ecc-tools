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
/**
 * @file SDCCaseAnalysis.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-10-28
 * @brief Registers CTS-local symbolic case constraints in the shared SDC frontend.
 */

#pragma once

namespace ista {
class SdcCommand;
}

namespace icts {
struct SdcClockData;
}

namespace icts::sdc_reader {

auto RegisterCaseAnalysisRecorder(ista::SdcCommand& interpreter, SdcClockData& data) -> void;

}  // namespace icts::sdc_reader

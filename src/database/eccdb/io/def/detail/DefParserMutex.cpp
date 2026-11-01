// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "def/detail/DefParserMutex.h"

namespace eccdb::def_detail {

std::mutex& parserMutex()
{
  static std::mutex mutex;
  return mutex;
}

}  // namespace eccdb::def_detail

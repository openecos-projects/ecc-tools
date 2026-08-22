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
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

#include "utility/logger/Logger.hpp"

int main()
{
  setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);

  ecc::Logger& logger = ecc::Logger::getInst();

  // info/warn must not throw in throw-on-error mode.
  logger.info(ecc::Loc::current(), "info message should not throw");
  logger.warn(ecc::Loc::current(), "warn message should not throw");

  bool threw = false;
  try {
    logger.error(ecc::Loc::current(), "boom", 42);
  } catch (const std::runtime_error& e) {
    threw = true;
    assert(std::strcmp(e.what(), "boom42") == 0);
  }
  assert(threw);

  return 0;
}

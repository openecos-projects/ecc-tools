// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include <cassert>
#include <cstdlib>

#include "Utility.hpp"

int main()
{
  assert(ircx::Utility::ceilDivPositive(0, 2) == 0);
  assert(ircx::Utility::ceilDivPositive(5, 2) == 3);
  assert(ircx::Utility::ceilDivPositive(5, 0) == 0);

  assert(ircx::Utility::getThreadNum(0, 64) == 1);
  assert(ircx::Utility::getThreadNum(-1, 64) == 1);
  assert(ircx::Utility::getThreadNum(4, 64) == 4);
  assert(ircx::Utility::getThreadNum(4, 0) == 1);

  std::vector<int32_t> value_list = {3, 1, 3, 2, 1};
  ircx::Utility::sortAndUnique(value_list);
  assert(value_list == std::vector<int32_t>({1, 2, 3}));

  GTLRectInt first_rect(0, 5, 10, 15);
  GTLRectInt second_rect(5, 0, 15, 10);
  GTLRectInt bounding_rect = ircx::Utility::getBoundingRect(first_rect, second_rect);
  assert(ircx::Utility::minX(bounding_rect) == 0);
  assert(ircx::Utility::minY(bounding_rect) == 0);
  assert(ircx::Utility::maxX(bounding_rect) == 15);
  assert(ircx::Utility::maxY(bounding_rect) == 15);

  return EXIT_SUCCESS;
}

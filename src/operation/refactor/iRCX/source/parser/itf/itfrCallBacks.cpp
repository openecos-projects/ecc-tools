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
 * @file itfrCallBacks.cpp
 * @brief Legacy ITF parser data structure implementation detail.
 */
#include <memory>

#include "itfrCallBacks.hpp"

namespace itf
{
namespace
{

std::unique_ptr<itfrCallBacks> itfCallbacksOwner;

} // namespace
  
itfrCallBacks* itfCallbacks = nullptr;

void
itfrCallBacks::reset()
{
  itfCallbacksOwner = std::make_unique<itfrCallBacks>();
  itfCallbacks = itfCallbacksOwner.get();
}

void
itfrCallBacks::clear()
{
  itfCallbacksOwner.reset();
  itfCallbacks = nullptr;
}

} // namespace itf

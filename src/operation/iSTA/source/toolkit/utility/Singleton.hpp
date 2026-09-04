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

#include "STAHeader.hpp"

namespace ista {

template <typename T>
class Singleton
{
 public:
  Singleton() = delete;
  ~Singleton() = delete;

  template <typename... Args>
  static void initInst(Args&&... args)
  {
    std::lock_guard lock{mutex_};
    instance_.emplace(std::forward<Args>(args)...);
  }

  static T& getInst()
  {
    std::lock_guard lock{mutex_};
    if (!instance_.has_value()) {
      instance_.emplace();
    }
    return *instance_;
  }

  static void destroyInst()
  {
    std::lock_guard lock{mutex_};
    instance_.reset();
  }

  static bool isInitialized()
  {
    std::lock_guard<std::mutex> lock{mutex_};
    return instance_.has_value();
  }

 private:
  static std::optional<T> instance_;
  static std::mutex mutex_;
};

template <typename T>
std::optional<T> Singleton<T>::instance_;

template <typename T>
std::mutex Singleton<T>::mutex_;

}  // namespace ista

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
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "Logger.hpp"

namespace ircx {

class AuxLogMessage
{
 public:
  explicit AuxLogMessage(bool is_error) : _is_error(is_error) {}
  ~AuxLogMessage()
  {
    if (_is_error) {
      RCXLOG.warn(Loc::current(), _stream.str());
    } else {
      RCXLOG.info(Loc::current(), _stream.str());
    }
  }

  template <typename T>
  AuxLogMessage& operator<<(const T& value)
  {
    _stream << value;
    return *this;
  }

 private:
  bool _is_error = false;
  std::ostringstream _stream;
};

}  // namespace ircx

#define LOG_INFO ircx::AuxLogMessage(false)
#define LOG_ERROR ircx::AuxLogMessage(true)

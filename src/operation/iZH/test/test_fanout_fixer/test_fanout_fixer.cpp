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
#include "FFModel.hpp"
#include "FanoutFixer.hpp"

int main()
{
  izh::FFModel ff_model;
  ff_model.set_buffer_name("BUF_X1");
  ff_model.set_max_fanout(8);
  ff_model.addFixedNetNum(2);
  ff_model.addInsertedNetNum(3);
  ff_model.addInsertedBufferNum(4);
  if (ff_model.get_buffer_name() != "BUF_X1" || ff_model.get_max_fanout() != 8 || ff_model.get_fixed_net_num() != 2
      || ff_model.get_inserted_net_num() != 3 || ff_model.get_inserted_buffer_num() != 4) {
    return 1;
  }

  izh::FanoutFixer::initInst();
  izh::FanoutFixer* ff_instance = &ZHFF;
  izh::FanoutFixer::initInst();
  if (ff_instance != &ZHFF) {
    return 1;
  }
  izh::FanoutFixer::destroyInst();
  return 0;
}

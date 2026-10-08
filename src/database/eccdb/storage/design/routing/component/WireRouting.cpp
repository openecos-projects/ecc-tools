// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
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

#include "design/routing/component/WireRouting.h"

#include <memory>

namespace eccdb {

DesignWirePathView DesignWirePath::view() const
{
  const DesignWirePathExtra extra{.width = width, .mask = mask, .taper_rule = taper_rule, .shape = shape, .style = style};
  return DesignWirePathView::create(layer, flags, &extra, DesignWirePointRange::fromValues(points),
                                    DesignWireViaRange::fromValues(vias), rectangles);
}

DesignWirePathView ownedWirePathView(DesignWirePath path)
{
  auto owner = std::make_shared<const DesignWirePath>(std::move(path));
  auto view = owner->view();
  view.retain(std::move(owner));
  return view;
}

}  // namespace eccdb

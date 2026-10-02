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

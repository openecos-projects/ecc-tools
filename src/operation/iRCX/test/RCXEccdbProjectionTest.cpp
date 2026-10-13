// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// ***************************************************************************************

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "DataManager.hpp"
#include "RCXInterface.hpp"
#include "context/DatabaseContext.h"
#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"
#include "tech/via_master/storage/ViaMasterInput.h"

namespace {

bool samePoint(const GTLPointInt& point, const int32_t x, const int32_t y)
{
  return point.x() == x && point.y() == y;
}

bool sameRect(const GTLRectInt& rectangle, const int32_t ll_x, const int32_t ll_y, const int32_t ur_x, const int32_t ur_y)
{
  return boost::polygon::xl(rectangle) == ll_x && boost::polygon::yl(rectangle) == ll_y
         && boost::polygon::xh(rectangle) == ur_x && boost::polygon::yh(rectangle) == ur_y;
}

}  // namespace

int main()
{
  eccdb::TechStore technology;
  const auto m1 = technology.createRoutingLayer(
      eccdb::TechLayerInfo{.name = "M1"}, eccdb::TechRoutingLayer{.direction = eccdb::TechRoutingDirection::kHorizontal, .width = 20});
  const auto v1 = technology.createCutLayer(eccdb::TechLayerInfo{.name = "V1"}, eccdb::TechCutLayer{.width = 4});
  const auto m2 = technology.createRoutingLayer(
      eccdb::TechLayerInfo{.name = "M2"}, eccdb::TechRoutingLayer{.direction = eccdb::TechRoutingDirection::kVertical, .width = 20});
  const auto via = technology.viaMasterStorage().createFixedViaMaster(
      eccdb::TechViaMaster{.name = "VIA12"},
      eccdb::TechViaMasterShapeInput{.bottom_layer = m1,
                                     .bottom_geometry = {.rects = {{-4, -3, 4, 3}, {-7, -1, -5, 1}}},
                                     .cut_layer = v1,
                                     .cut_geometry = {.rects = {{-2, -2, 2, 2}, {3, -1, 4, 1}}},
                                     .top_layer = m2,
                                     .top_geometry = {.rects = {{-5, -4, 5, 4}, {6, -1, 8, 1}}}});
  eccdb::LibraryStore library{technology.techRegistry()};
  eccdb::DesignStore design{technology.techRegistry(), library.libraryRegistry(), &technology.geometryRegistry()};
  eccdb::DatabaseContext context{technology, library, design};
  design.globalStorage().setInfo(eccdb::DesignInfo{.name = "projection", .database_units_per_micron = 1000});

  const auto regular_net = design.netlistStorage().createNet(eccdb::DesignNet{.name = "signal"});
  const auto special_net = design.netlistStorage().createSpecialNet(eccdb::DesignNet{.name = "VDD"});
  auto& routing = design.routingStorage();

  const auto regular_wire = routing.createWire(eccdb::DesignWire{.net = regular_net});
  static_cast<void>(routing.createShape(
      regular_wire,
      eccdb::DesignRouteShape{.layer = m1,
                              .geometry = eccdb::DesignRouteSegment{.begin = {.position = {100, 100}}, .end = {.position = {300, 100}}}}));

  const auto special_wire = routing.createWire(eccdb::DesignWire{.net = special_net});
  static_cast<void>(routing.createShape(
      special_wire,
      eccdb::DesignRouteShape{.layer = m1,
                              .flags = eccdb::DesignWirePathFlag::kHasWidth,
                              .geometry = eccdb::DesignRouteSegment{.begin = {.position = {100, 200}}, .end = {.position = {300, 200}}}},
      eccdb::DesignRouteShapeAttributes{.width = 20}));
  static_cast<void>(routing.createShape(
      special_wire,
      eccdb::DesignRouteShape{.layer = m1,
                              .flags = eccdb::DesignWirePathFlag::kHasWidth,
                              .geometry = eccdb::DesignRouteVia{
                                  .origin = {.position = {120, 200}},
                                  .via = {.tech_via = via,
                                          .flags = eccdb::DesignWireViaFlag::kHasArray,
                                          .rows = 3,
                                          .columns = 2,
                                          .step_x = 40,
                                          .step_y = 50}}},
      eccdb::DesignRouteShapeAttributes{.width = 20}));

  ircx::DataManager::initInst();
  auto& interface = ircx::RCXInterface::getInst();
  interface.setDesignSource(context);
  interface.wrapDatabase();

  auto& layout = RCXDM.getDatabase().get_layout_data();
  assert(layout.get_net_list().size() == 1u);
  auto& regular_segments = layout.get_net_list().front().get_segment_list();
  assert(regular_segments.size() == 1u);
  // Regular iDB-compatible segments retain their default half-width endpoint caps.
  assert(sameRect(regular_segments.front().get_shape(), 90, 90, 310, 110));

  auto& special = layout.get_special_net();
  assert(special.get_segment_list().size() == 1u);
  // SPECIALNET stripes preserve explicit longitudinal endpoints.
  assert(sameRect(special.get_segment_list().front().get_shape(), 100, 190, 300, 210));

  auto& special_vias = special.get_via_list();
  assert(special_vias.size() == 6u);
  constexpr int32_t kExpectedOrigins[][2] = {{120, 200}, {120, 250}, {120, 300}, {160, 200}, {160, 250}, {160, 300}};
  for (std::size_t index = 0; index < special_vias.size(); ++index) {
    assert(special_vias[index].get_via_name() == "VIA12");
    assert(samePoint(special_vias[index].get_point(), kExpectedOrigins[index][0], kExpectedOrigins[index][1]));
  }
  assert(sameRect(special_vias.front().get_bottom_layer_shape().get_shape(), 113, 197, 124, 203));
  assert(sameRect(special_vias.front().get_cut_layer_shape().get_shape(), 118, 198, 124, 202));
  assert(sameRect(special_vias.front().get_top_layer_shape().get_shape(), 115, 196, 128, 204));

  interface.setDesignSource(static_cast<eccdb::DatabaseContext*>(nullptr));
  ircx::DataManager::destroyInst();
  ircx::RCXInterface::destroyInst();
  return EXIT_SUCCESS;
}

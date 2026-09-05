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
#include "DesignTestFixture.h"

#include <stdexcept>
#include <unordered_map>

#include "DatabaseTestAccess.h"
#include "eccdb/db.h"

namespace eccdb {
namespace {

TEST_F(DesignStorageTest, PublicApiProvidesTypedIdAndSnapshotCrud)
{
  auto database = detail::DatabaseTestAccess::borrow(design);
  const auto signal = database.createNet(NetData{.name = "signal", .use = SignalUse::kSignal});
  const auto power = database.createSpecialNet(NetData{.name = "VDD", .use = SignalUse::kPower});
  const auto stored_instance = createInstance("u1", {10, 20});
  const auto instance = database.createInstance(
      InstanceData{.name = stored_instance.name,
                   .master = CellMasterId{stored_instance.master.packed()},
                   .origin = stored_instance.origin});
  const auto input = database.findInstancePinId(instance, "A");
  const auto io = database.createIoPin(
      IoPinData{.name = "IN", .direction = IoDirection::kInput, .use = SignalUse::kSignal});

  ASSERT_TRUE(signal);
  ASSERT_TRUE(power);
  ASSERT_TRUE(instance);
  ASSERT_TRUE(input);
  ASSERT_TRUE(io);
  EXPECT_TRUE(database.contains(signal));
  EXPECT_TRUE(database.isSpecialNet(power));
  EXPECT_EQ(database.netData(signal)->name, "signal");
  EXPECT_EQ(database.findInstanceId("u1"), instance);

  database.connect(input, signal);
  database.connect(io, signal);
  EXPECT_EQ(database.instancePinData(input)->net, signal);
  EXPECT_EQ(database.ioPinData(io)->net, signal);
  EXPECT_EQ(database.instancePinIds(signal).front(), input);
  EXPECT_EQ(database.ioPinIds(signal).front(), io);

  auto net_snapshot = *database.netData(signal);
  net_snapshot.name = "signal_main";
  net_snapshot.source = NetSource::kUser;
  net_snapshot.weight = 5;
  EXPECT_EQ(database.netData(signal)->name, "signal");
  database.updateNet(signal, net_snapshot);

  auto instance_snapshot = *database.instanceData(instance);
  instance_snapshot.origin = {100, 200};
  instance_snapshot.placement_status = PlacementStatus::kFixed;
  database.updateInstance(instance, instance_snapshot);

  auto io_snapshot = *database.ioPinData(io);
  io_snapshot.direction = IoDirection::kInOut;
  database.updateIoPin(io, io_snapshot);

  EXPECT_FALSE(database.findNetId("signal"));
  EXPECT_EQ(database.findNetId("signal_main"), signal);
  EXPECT_EQ(database.netData(signal)->weight, 5);
  EXPECT_EQ(database.instanceData(instance)->origin, (Point{100, 200}));
  EXPECT_EQ(database.ioPinData(io)->direction, IoDirection::kInOut);

  std::unordered_map<NetId, std::string> names;
  names.emplace(signal, database.netData(signal)->name);
  EXPECT_EQ(names.at(signal), "signal_main");

  database.disconnect(input, signal);
  database.disconnect(io, signal);
  EXPECT_FALSE(database.instancePinData(input)->net);
  EXPECT_FALSE(database.ioPinData(io)->net);
  EXPECT_TRUE(database.destroyIoPin(io));
  EXPECT_TRUE(database.destroyInstance(instance));
  EXPECT_TRUE(database.destroyNet(signal));
  EXPECT_FALSE(database.contains(signal));
  EXPECT_FALSE(database.netData(signal));
}

TEST_F(DesignStorageTest, PublicApiModelsWireAsEntityAndPathAsValue)
{
  auto database = detail::DatabaseTestAccess::borrow(design);
  const auto net = database.createSpecialNet(NetData{.name = "VDD", .use = SignalUse::kPower});
  const auto via = database.createDesignVia(
      DesignViaData{.name = "LOCAL_VIA",
                    .rectangles = {{.layer = LayerId{layer.packed()},
                                    .rectangle = {-5, -5, 5, 5}}}});

  WireRoutingData routing{
      .paths = {{.layer = RoutingLayerId{routing_layer.packed()},
                 .width = 20,
                 .points = {{{0, 0}}, {{100, 0}}},
                 .vias = {{.point_index = 1, .definition = ViaDefinitionId{via}}}}}};
  const auto wire = database.createWire(
      WireMetadata{.net = net, .status = WireStatus::kFixed}, std::move(routing));

  ASSERT_TRUE(wire);
  EXPECT_TRUE(database.contains(wire));
  EXPECT_EQ(database.wireMetadata(wire)->net, net);
  EXPECT_EQ(database.wireMetadata(wire)->status, WireStatus::kFixed);
  ASSERT_EQ(database.wireRoutingData(wire)->paths.size(), 1u);
  EXPECT_EQ(database.wireRoutingData(wire)->paths.front().points.size(), 2u);
  EXPECT_EQ(database.wireIds(net).front(), wire);
  EXPECT_EQ(database.findDesignViaId("LOCAL_VIA"), via);
  EXPECT_EQ(database.designViaData(via)->rectangles.size(), 1u);
  EXPECT_FALSE(database.destroyDesignVia(via));

  auto wire_ref = database.wire(wire);
  ASSERT_TRUE(wire_ref);
  EXPECT_EQ(wire_ref.metadata().net, net);
  EXPECT_EQ(wire_ref.pathCount(), 1u);
  EXPECT_EQ(wire_ref.pathData(0).points.size(), 2u);

  EXPECT_TRUE(database.destroyWire(wire));
  EXPECT_FALSE(wire_ref);
  EXPECT_TRUE(database.destroyDesignVia(via));
  EXPECT_TRUE(database.destroyNet(net));
}

TEST_F(DesignStorageTest, ConvenienceRefsRejectRelationshipsAcrossDatabases)
{
  auto first = detail::DatabaseTestAccess::borrow(design);
  const auto first_net_id = first.createNet(NetData{.name = "first"});
  const auto first_pin_id = first.createIoPin(IoPinData{.name = "first_pin"});

  DesignStore other_store{tech, library.libraryRegistry()};
  auto second = detail::DatabaseTestAccess::borrow(other_store);
  const auto second_net_id = second.createNet(NetData{.name = "second"});

  EXPECT_EQ(first_net_id.packed(), second_net_id.packed());
  auto first_pin = first.ioPin(first_pin_id);
  EXPECT_THROW(first_pin.connect(second.net(second_net_id)), std::invalid_argument);
  EXPECT_FALSE(first_pin.net());

  first_pin.connect(first.net(first_net_id));
  EXPECT_EQ(first_pin.net().id(), first_net_id);
}

}  // namespace
}  // namespace eccdb

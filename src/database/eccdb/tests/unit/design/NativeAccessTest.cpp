// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "DesignTestFixture.h"

#include <cstdint>
#include <stdexcept>
#include <utility>

#include "connectivity/ConnectivityService.h"
#include "context/DatabaseContext.h"
#include "instance/InstanceService.h"

namespace eccdb {
namespace {

struct ChangeObserver final : DatabaseObserver
{
  void afterChange(const DatabaseChange& change) noexcept override
  {
    ++after_count;
    last_domains = change.domains;
  }

  uint32_t after_count = 0;
  DatabaseChangeDomain last_domains = DatabaseChangeDomain::kNone;
};

// Reads and business edits operate on the same native model.
TEST_F(DesignStorageTest, NativeStorageReadsServiceManagedConnectivityAndLifecycle)
{
  DatabaseContext context{design};
  ChangeObserver observer;
  context.addObserver(observer);
  ConnectivityService connectivity(context);
  InstanceService instances(context);
  auto& netlist = design.netlistStorage();

  const auto signal = connectivity.createNet(DesignNet{.name = "signal", .use = DesignSignalUse::kSignal});
  const auto power = connectivity.createNet(DesignNet{.name = "VDD", .use = DesignSignalUse::kPower}, true);
  const auto instance = instances.create(createInstance("u1", {10, 20}));
  const auto input = netlist.findInstanceTerm(instance, "A");
  const auto block = connectivity.createBlockTerm(
      DesignBlockTerm{.name = "IN", .direction = DesignIoPinDirection::kInput, .use = DesignSignalUse::kSignal});

  ASSERT_TRUE(signal);
  ASSERT_TRUE(power);
  ASSERT_TRUE(instance);
  ASSERT_TRUE(input);
  ASSERT_TRUE(block);
  EXPECT_TRUE(netlist.contains(signal));
  EXPECT_TRUE(netlist.isSpecialNet(power));
  EXPECT_EQ(netlist.net(signal).name, "signal");
  EXPECT_EQ(netlist.findInstance("u1"), instance);

  connectivity.connect(input, signal);
  connectivity.connect(block, signal);
  EXPECT_EQ(netlist.instanceTerm(input).net, signal);
  EXPECT_EQ(netlist.blockTerm(block).net, signal);
  EXPECT_EQ(netlist.instanceTerms(signal).front(), input);
  EXPECT_EQ(netlist.blockTerms(signal).front(), block);

  auto net = netlist.net(signal);
  net.name = "signal_main";
  net.source = DesignNetSource::kUser;
  net.flags |= DesignNetFlag::kHasWeight;
  net.weight = 5;
  connectivity.setNetProperties(signal, std::move(net));
  instances.place(instance, {100, 200}, DesignOrientation::kN, DesignPlacementStatus::kFixed);

  EXPECT_FALSE(netlist.findNet("signal"));
  EXPECT_EQ(netlist.findNet("signal_main"), signal);
  EXPECT_EQ(netlist.net(signal).weight, 5);
  EXPECT_EQ(netlist.instance(instance).origin, (Point{100, 200}));
  EXPECT_EQ(netlist.instance(instance).placement_status, DesignPlacementStatus::kFixed);

  connectivity.disconnect(input, signal);
  connectivity.disconnect(block, signal);
  EXPECT_FALSE(netlist.instanceTerm(input).net);
  EXPECT_FALSE(netlist.blockTerm(block).net);
  EXPECT_TRUE(connectivity.deleteBlockTerm(block));
  EXPECT_TRUE(instances.destroy(instance));
  EXPECT_TRUE(connectivity.deleteNet(signal));
  EXPECT_FALSE(netlist.contains(signal));
  EXPECT_FALSE(netlist.contains(instance));
  EXPECT_FALSE(netlist.contains(block));
  EXPECT_GT(observer.after_count, 0u);
  EXPECT_EQ(context.revision(), static_cast<uint64_t>(observer.after_count) + 1u);
  EXPECT_TRUE(includes(observer.last_domains, DatabaseChangeDomain::kConnectivity));
  context.removeObserver(observer);
}

TEST_F(DesignStorageTest, FailedServiceMutationDoesNotNotifyOrAdvanceContextRevision)
{
  DatabaseContext context{design};
  ChangeObserver observer;
  context.addObserver(observer);
  ConnectivityService connectivity(context);
  const auto signal = connectivity.createNet(DesignNet{.name = "signal"});
  const auto revision_after_create = context.revision();
  const auto notifications_after_create = observer.after_count;

  EXPECT_THROW(connectivity.renameNet(DesignNetId{}, "missing"), std::invalid_argument);
  EXPECT_EQ(context.revision(), revision_after_create);
  EXPECT_EQ(observer.after_count, notifications_after_create);

  connectivity.renameNet(signal, "signal_eco");
  EXPECT_EQ(context.revision(), revision_after_create + 1u);
  EXPECT_EQ(observer.after_count, notifications_after_create + 1u);
  EXPECT_EQ(design.netlistStorage().findNet("signal_eco"), signal);
  context.removeObserver(observer);
}

}  // namespace
}  // namespace eccdb

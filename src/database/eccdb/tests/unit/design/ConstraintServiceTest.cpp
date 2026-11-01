// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <stdexcept>

#include "DesignTestFixture.h"
#include "context/DatabaseContext.h"
#include "service/constraint/ConstraintService.h"

namespace eccdb {
namespace {

class ConstraintServiceTest : public DesignStorageTest
{
 protected:
  void SetUp() override
  {
    DesignStorageTest::SetUp();
    context = std::make_unique<DatabaseContext>(design);
    service = std::make_unique<ConstraintService>(*context);
  }

  std::unique_ptr<DatabaseContext> context;
  std::unique_ptr<ConstraintService> service;
};

TEST_F(ConstraintServiceTest, DeleteRegionDetachesInstancesAndGroupsBeforeDestroyingGeometry)
{
  auto& netlist = design.netlistStorage();
  auto& constraints = design.constraintStorage();
  const auto region = service->createRegion(
      DesignRegionInput{.name = "FENCE", .type = DesignRegionType::kFence, .rectangles = {{0, 0, 100, 100}}});
  const auto region_shape = constraints.region(region).rectangles.front();
  const auto instance = netlist.createInstance(createInstance("u1"));
  service->assignInstanceRegion(instance, region);
  const auto group = service->createGroup(DesignGroup{.name = "G", .instances = {instance}});
  service->assignGroupRegion(group, region);

  EXPECT_TRUE(service->deleteRegion(region));
  EXPECT_FALSE(constraints.contains(region));
  EXPECT_FALSE(design.geometryRegistry().contains(region_shape));

  const auto& stored_instance = netlist.instance(instance);
  EXPECT_FALSE(stored_instance.region);
  EXPECT_EQ(stored_instance.flags & DesignInstanceFlag::kHasRegion, 0u);

  const auto& stored_group = constraints.group(group);
  EXPECT_FALSE(stored_group.region);
  EXPECT_EQ(stored_group.flags & DesignGroupFlag::kHasRegion, 0u);
}

TEST_F(ConstraintServiceTest, InvalidGroupMemberReplacementLeavesExistingMembershipUntouched)
{
  auto& netlist = design.netlistStorage();
  auto& constraints = design.constraintStorage();
  const auto first = netlist.createInstance(createInstance("u1"));
  const auto second = netlist.createInstance(createInstance("u2"));
  const auto group = service->createGroup(DesignGroup{.name = "G", .instances = {first}});

  const std::array duplicate_members{second, second};
  EXPECT_THROW(service->replaceGroupMembers(group, duplicate_members), std::invalid_argument);
  ASSERT_EQ(constraints.group(group).instances.size(), 1u);
  EXPECT_EQ(constraints.group(group).instances.front(), first);
}

TEST_F(ConstraintServiceTest, DeleteBlockageDestroysOwnedShapesAndPreservesComponent)
{
  auto& netlist = design.netlistStorage();
  auto& constraints = design.constraintStorage();
  const auto instance = netlist.createInstance(createInstance("u1"));
  const auto blockage = service->createBlockage(DesignBlockageInput{.kind = DesignBlockageKind::kPlacement,
                                                                       .flags = DesignBlockageFlag::kHasComponent,
                                                                       .component = instance,
                                                                       .rectangles = {{10, 20, 60, 80}}});
  const auto rectangle = constraints.blockage(blockage).rectangles.front();

  EXPECT_TRUE(service->deleteBlockage(blockage));
  EXPECT_FALSE(constraints.contains(blockage));
  EXPECT_FALSE(design.geometryRegistry().contains(rectangle));
  EXPECT_TRUE(netlist.contains(instance));
}

TEST_F(ConstraintServiceTest, GroupAndRegionDeletionNotifyIndirectlyConstrainedMembers)
{
  struct Observer final : DatabaseObserver {
    std::vector<DesignInstanceId> instances;
    void afterChange(const DatabaseChange& change) noexcept override {
      instances.assign(change.instances.begin(), change.instances.end());
    }
  } observer;
  const auto region = service->createRegion(
      DesignRegionInput{.name = "R", .rectangles = {{0, 0, 100, 100}}});
  const auto other_region = service->createRegion(
      DesignRegionInput{.name = "OTHER", .rectangles = {{100, 0, 200, 100}}});
  const auto instance = design.netlistStorage().createInstance(createInstance("u1"));
  service->assignInstanceRegion(instance, other_region);
  const auto group = service->createGroup(DesignGroup{.name = "G", .instances = {instance}});
  service->assignGroupRegion(group, region);
  context->addObserver(observer);
  ASSERT_TRUE(service->deleteRegion(region));
  EXPECT_EQ(observer.instances, (std::vector<DesignInstanceId>{instance}));
  EXPECT_EQ(design.netlistStorage().instance(instance).region, other_region);
  observer.instances.clear();
  ASSERT_TRUE(service->deleteGroup(group));
  EXPECT_EQ(observer.instances, (std::vector<DesignInstanceId>{instance}));
}

}  // namespace
}  // namespace eccdb

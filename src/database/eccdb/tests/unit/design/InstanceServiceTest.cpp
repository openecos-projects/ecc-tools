// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <array>
#include <memory>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "DesignTestFixture.h"
#include "common/GeometryBounds.h"
#include "design/constraint/storage/DesignConstraintStorage.h"
#include "design/pin_access/storage/DesignPinAccessStorage.h"
#include "service/context/DatabaseContext.h"
#include "service/instance/InstanceService.h"

namespace eccdb {
namespace {

class RecordingObserver final : public DatabaseObserver
{
 public:
  struct Event
  {
    DatabaseChangeDomain domains = DatabaseChangeDomain::kNone;
    std::vector<DesignInstanceId> instances;
    std::vector<DesignNetId> nets;
  };

  void beforeChange(const DatabaseChange& change) noexcept override
  {
    before = Event{.domains = change.domains,
                   .instances = std::vector<DesignInstanceId>{change.instances.begin(), change.instances.end()},
                   .nets = std::vector<DesignNetId>{change.nets.begin(), change.nets.end()}};
  }

  void afterChange(const DatabaseChange& change) noexcept override
  {
    after = Event{.domains = change.domains,
                  .instances = std::vector<DesignInstanceId>{change.instances.begin(), change.instances.end()},
                  .nets = std::vector<DesignNetId>{change.nets.begin(), change.nets.end()}};
  }

  std::optional<Event> before;
  std::optional<Event> after;
};

struct InstanceTermServiceMarker
{
  int value = 0;
};

class InstanceServiceTest : public DesignStorageTest
{
 protected:
  void SetUp() override
  {
    DesignStorageTest::SetUp();
    context = std::make_unique<DatabaseContext>(design);
    service = std::make_unique<InstanceService>(*context);
  }

  [[nodiscard]] LibraryCellMasterId createCompatibleMaster(std::string name, bool reverse_order = false)
  {
    const auto replacement = library.cellMasterStorage().createCellMaster(
        LibraryCellMaster{.name = std::move(name), .site = site, .width = 20, .height = 40});
    const auto add_input = [&] {
      return library.masterTermStorage().createMasterTerm(
          replacement, LibraryMasterTerm{.name = "A", .direction = LibraryMasterTermDirection::kInput});
    };
    const auto add_output = [&] {
      return library.masterTermStorage().createMasterTerm(
          replacement, LibraryMasterTerm{.name = "Y", .direction = LibraryMasterTermDirection::kOutput});
    };
    if (reverse_order) {
      replacement_output = add_output();
      replacement_input = add_input();
    } else {
      replacement_input = add_input();
      replacement_output = add_output();
    }
    return replacement;
  }

  [[nodiscard]] LibraryCellMasterId createIncompatibleMaster()
  {
    const auto replacement = library.cellMasterStorage().createCellMaster(
        LibraryCellMaster{.name = "NANDX1", .site = site, .width = 20, .height = 40});
    static_cast<void>(library.masterTermStorage().createMasterTerm(
        replacement, LibraryMasterTerm{.name = "B", .direction = LibraryMasterTermDirection::kInput}));
    static_cast<void>(library.masterTermStorage().createMasterTerm(
        replacement, LibraryMasterTerm{.name = "Y", .direction = LibraryMasterTermDirection::kOutput}));
    return replacement;
  }

  void configurePinAccess(DesignInstanceId instance, DesignInstanceTermId term)
  {
    master_port = library.masterPortStorage().createMasterPort(input_term);
    design.pinAccessStorage().setMasterPortCandidateGroup(
        master_port, 3u,
        {DesignPinAccessPoint{.point = {10, 20}, .layer = routing_layer,
                              .access_directions = DesignPinAccessDirection::kNorth}});
    design.pinAccessStorage().setInstancePinAccessGroup(instance, 3u);
    design.pinAccessStorage().setPreferredInstancePinAccess(
        term, {{.master_port = master_port, .group_index = 3u, .point_index = 0u}});
  }

  std::unique_ptr<DatabaseContext> context;
  std::unique_ptr<InstanceService> service;
  LibraryMasterTermId replacement_input;
  LibraryMasterTermId replacement_output;
  LibraryMasterPortId master_port;
};

TEST_F(InstanceServiceTest, DestroyDetachesBothNetKindsAndCleansOwnedConstraints)
{
  auto& netlist = design.netlistStorage();
  auto& registry = design.designRegistry().registry();
  const auto instance = netlist.createInstance(createInstance("u1", {100, 200}));
  const auto term = netlist.findInstanceTerm(instance, input_term);
  const auto other_term = netlist.findInstanceTerm(instance, output_term);
  ASSERT_TRUE(term);
  ASSERT_TRUE(other_term);
  const auto neighbor = netlist.createInstance(createInstance("u2", {300, 200}));
  const auto neighbor_term = netlist.findInstanceTerm(neighbor, input_term);
  const auto regular = netlist.createNet(DesignNet{.name = "signal"});
  const auto special = netlist.createSpecialNet(DesignNet{.name = "VDD", .use = DesignSignalUse::kPower});
  netlist.connect(term, regular);
  netlist.connect(term, special);
  netlist.connect(neighbor_term, regular);
  netlist.connect(neighbor_term, special);
  configurePinAccess(instance, term);
  registry.emplace<InstanceTermServiceMarker>(term.entity(), InstanceTermServiceMarker{.value = 9});
  ASSERT_TRUE(registry.all_of<InstanceTermServiceMarker>(term.entity()));

  const auto group = design.constraintStorage().createGroup(DesignGroup{.name = "group", .instances = {instance}});
  const auto blockage = design.constraintStorage().createBlockage(
      DesignBlockageInput{.kind = DesignBlockageKind::kPlacement,
                          .flags = DesignBlockageFlag::kHasComponent,
                          .component = instance,
                          .rectangles = {{10, 20, 30, 40}}});
  RecordingObserver observer;
  context->addObserver(observer);

  EXPECT_TRUE(service->destroy(instance));
  EXPECT_FALSE(netlist.contains(instance));
  EXPECT_FALSE(netlist.contains(term));
  EXPECT_FALSE(netlist.contains(other_term));
  EXPECT_FALSE(registry.valid(term.entity()));
  EXPECT_FALSE(registry.valid(other_term.entity()));
  EXPECT_FALSE(registry.all_of<DesignInstanceTerm>(term.entity()));
  EXPECT_FALSE(registry.all_of<InstanceTermServiceMarker>(term.entity()));
  EXPECT_TRUE(netlist.contains(neighbor));
  ASSERT_EQ(netlist.instanceTerms(regular).size(), 1u);
  ASSERT_EQ(netlist.instanceTerms(special).size(), 1u);
  EXPECT_EQ(netlist.instanceTerms(regular).front(), neighbor_term);
  EXPECT_EQ(netlist.instanceTerms(special).front(), neighbor_term);
  EXPECT_TRUE(design.constraintStorage().group(group).instances.empty());
  EXPECT_FALSE(design.constraintStorage().contains(blockage));
  ASSERT_TRUE(observer.before.has_value());
  ASSERT_TRUE(observer.after.has_value());
  EXPECT_TRUE(includes(observer.before->domains, DatabaseChangeDomain::kInstances));
  EXPECT_TRUE(includes(observer.before->domains, DatabaseChangeDomain::kConnectivity));
  EXPECT_TRUE(includes(observer.before->domains, DatabaseChangeDomain::kGeometry));
  EXPECT_TRUE(includes(observer.before->domains, DatabaseChangeDomain::kConstraints));
  EXPECT_EQ(observer.before->instances, (std::vector<DesignInstanceId>{instance}));
  EXPECT_EQ(observer.before->nets, (std::vector<DesignNetId>{regular, special}));
  EXPECT_EQ(observer.after->nets, observer.before->nets);
}

TEST_F(InstanceServiceTest, MoveInvalidatesBoundsAdvancesGeometryAndClearsPinAccess)
{
  const auto instance = design.netlistStorage().createInstance(createInstance("u1", {100, 200}));
  const auto term = design.netlistStorage().findInstanceTerm(instance, input_term);
  configurePinAccess(instance, term);
  auto& bounds = design.designRegistry().registry().get<GeometryBounds>(instance.entity());
  const auto epoch = design.designRegistry().geometryEpoch();
  bounds.set({100, 200, 120, 240}, BoundsSpace::kPlaced, epoch);

  service->move(instance, 30, -40);

  EXPECT_EQ(design.netlistStorage().instance(instance).origin, (Point{130, 160}));
  EXPECT_FALSE(bounds.usable());
  EXPECT_GT(design.designRegistry().geometryEpoch(), epoch);
  EXPECT_FALSE(design.pinAccessStorage().instancePinAccessGroup(instance).has_value());
  EXPECT_TRUE(design.pinAccessStorage().preferredInstancePinAccess(term).empty());
}

TEST_F(InstanceServiceTest, CompatibleMasterSwapPreservesTermIdsAndConnections)
{
  auto& netlist = design.netlistStorage();
  const auto& registry = design.designRegistry().registry();
  // DesignInstanceTerm caches LibraryMasterTerm addresses. Create alternate
  // masters before materializing the instance so later EnTT growth cannot
  // invalidate the fixture master-term pointers under test.
  const auto replacement = createCompatibleMaster("INVX2", true);
  const auto instance = netlist.createInstance(createInstance("u1"));
  const auto input = netlist.findInstanceTerm(instance, input_term);
  const auto output = netlist.findInstanceTerm(instance, output_term);
  ASSERT_TRUE(input);
  ASSERT_TRUE(output);
  const auto* const input_payload = &registry.get<const DesignInstanceTerm>(input.entity());
  const auto* const output_payload = &registry.get<const DesignInstanceTerm>(output.entity());
  const auto regular = netlist.createNet(DesignNet{.name = "signal"});
  const auto special = netlist.createSpecialNet(DesignNet{.name = "VDD", .use = DesignSignalUse::kPower});
  netlist.connect(input, regular);
  netlist.connect(output, special);
  configurePinAccess(instance, input);

  service->swapMasterCompatible(instance, replacement);

  EXPECT_EQ(netlist.findInstanceTerm(instance, "A"), input);
  EXPECT_EQ(netlist.findInstanceTerm(instance, "Y"), output);
  EXPECT_EQ(&registry.get<const DesignInstanceTerm>(input.entity()), input_payload);
  EXPECT_EQ(&registry.get<const DesignInstanceTerm>(output.entity()), output_payload);
  EXPECT_EQ(netlist.instanceTerm(input).master_term, replacement_input);
  EXPECT_EQ(netlist.instanceTerm(output).master_term, replacement_output);
  EXPECT_EQ(netlist.instanceTerm(input).net, regular);
  EXPECT_EQ(netlist.instanceTerm(output).special_net, special);
  ASSERT_EQ(netlist.instanceTerms(instance).size(), 2u);
  EXPECT_EQ(netlist.instanceTerms(instance).front(), output);
  EXPECT_FALSE(design.pinAccessStorage().instancePinAccessGroup(instance).has_value());
  EXPECT_TRUE(design.pinAccessStorage().preferredInstancePinAccess(input).empty());
}

TEST_F(InstanceServiceTest, IncompatibleMasterSwapFailsWithoutChangingScope)
{
  auto& netlist = design.netlistStorage();
  const auto incompatible = createIncompatibleMaster();
  const auto instance = netlist.createInstance(createInstance("u1"));
  const auto input = netlist.findInstanceTerm(instance, input_term);
  const auto regular = netlist.createNet(DesignNet{.name = "signal"});
  netlist.connect(input, regular);
  configurePinAccess(instance, input);
  const auto old_master = netlist.instance(instance).master;
  const auto revision = context->revision();

  EXPECT_THROW(service->swapMasterCompatible(instance, incompatible), std::invalid_argument);

  EXPECT_EQ(netlist.instance(instance).master, old_master);
  EXPECT_EQ(netlist.findInstanceTerm(instance, "A"), input);
  EXPECT_EQ(netlist.instanceTerm(input).net, regular);
  EXPECT_EQ(design.pinAccessStorage().instancePinAccessGroup(instance), std::optional<uint32_t>{3u});
  EXPECT_EQ(design.pinAccessStorage().preferredInstancePinAccess(input).size(), 1u);
  EXPECT_EQ(context->revision(), revision);
}

TEST_F(InstanceServiceTest, RejectsMalformedReplacementTermRelationsWithoutMutatingInstance)
{
  auto& netlist = design.netlistStorage();
  const auto& registry = design.designRegistry().registry();
  const auto replacement = createCompatibleMaster("INVX2");
  const auto instance = netlist.createInstance(createInstance("u1"));
  const auto input = netlist.findInstanceTerm(instance, input_term);
  const auto output = netlist.findInstanceTerm(instance, output_term);
  ASSERT_TRUE(input);
  ASSERT_TRUE(output);
  const auto* const input_payload = &registry.get<const DesignInstanceTerm>(input.entity());
  const auto* const output_payload = &registry.get<const DesignInstanceTerm>(output.entity());
  const auto regular = netlist.createNet(DesignNet{.name = "signal"});
  const auto special = netlist.createSpecialNet(DesignNet{.name = "VDD", .use = DesignSignalUse::kPower});
  netlist.connect(input, regular);
  netlist.connect(output, special);

  const auto original_master = netlist.instance(instance).master;
  const auto input_master_term = netlist.instanceTerm(input).master_term;
  const auto output_master_term = netlist.instanceTerm(output).master_term;
  const std::array original_terms{input, output};
  const auto revision = context->revision();
  const auto assert_unchanged = [&] {
    EXPECT_EQ(netlist.instance(instance).master, original_master);
    const auto terms = netlist.instanceTerms(instance);
    ASSERT_EQ(terms.size(), original_terms.size());
    EXPECT_EQ(terms[0], original_terms[0]);
    EXPECT_EQ(terms[1], original_terms[1]);
    EXPECT_EQ(netlist.findInstanceTerm(instance, "A"), input);
    EXPECT_EQ(netlist.findInstanceTerm(instance, "Y"), output);
    EXPECT_EQ(&registry.get<const DesignInstanceTerm>(input.entity()), input_payload);
    EXPECT_EQ(&registry.get<const DesignInstanceTerm>(output.entity()), output_payload);
    EXPECT_EQ(netlist.instanceTerm(input).master_term, input_master_term);
    EXPECT_EQ(netlist.instanceTerm(output).master_term, output_master_term);
    EXPECT_EQ(netlist.instanceTerm(input).net, regular);
    EXPECT_EQ(netlist.instanceTerm(input).special_net, DesignNetId{});
    EXPECT_EQ(netlist.instanceTerm(output).net, DesignNetId{});
    EXPECT_EQ(netlist.instanceTerm(output).special_net, special);
    ASSERT_EQ(netlist.instanceTerms(regular).size(), 1u);
    EXPECT_EQ(netlist.instanceTerms(regular).front(), input);
    ASSERT_EQ(netlist.instanceTerms(special).size(), 1u);
    EXPECT_EQ(netlist.instanceTerms(special).front(), output);
    EXPECT_EQ(context->revision(), revision);
  };

  auto& replacement_terms = library.libraryRegistry().registry().get<LibraryCellMaster>(replacement.entity()).terms;
  const auto valid_terms = replacement_terms;

  replacement_terms = {replacement_input, replacement_input};
  EXPECT_ANY_THROW(service->swapMasterCompatible(instance, replacement));
  assert_unchanged();

  replacement_terms = {input_term, replacement_output};
  EXPECT_ANY_THROW(service->swapMasterCompatible(instance, replacement));
  assert_unchanged();

  replacement_terms = {replacement_input, LibraryMasterTermId{}};
  EXPECT_ANY_THROW(service->swapMasterCompatible(instance, replacement));
  assert_unchanged();

  replacement_terms = valid_terms;
}

TEST_F(InstanceServiceTest, RejectsDestroyOfMustJoinEndpointBeforeChangingAnything)
{
  auto& netlist = design.netlistStorage();
  const auto instance = netlist.createInstance(createInstance("joined"));
  const auto term = netlist.findInstanceTerm(instance, input_term);
  const auto net = netlist.createNet(DesignNet{.name = "joined_net", .flags = DesignNetFlag::kMustJoin});
  netlist.connect(term, net);
  const auto revision = context->revision();
  EXPECT_THROW((void) service->destroy(instance), std::invalid_argument);
  EXPECT_TRUE(netlist.contains(instance));
  ASSERT_EQ(netlist.instanceTerms(net).size(), 1u);
  EXPECT_EQ(netlist.instanceTerms(net).front(), term);
  EXPECT_EQ(context->revision(), revision);
}

TEST_F(InstanceServiceTest, TranslationOverflowIsRejectedWithoutMutation)
{
  const auto instance = design.netlistStorage().createInstance(
      createInstance("edge", {std::numeric_limits<int32_t>::max(), 0}));
  const auto revision = context->revision();
  EXPECT_THROW(service->move(instance, 1, 0), std::overflow_error);
  EXPECT_EQ(design.netlistStorage().instance(instance).origin.x, std::numeric_limits<int32_t>::max());
  EXPECT_EQ(context->revision(), revision);
}

TEST_F(InstanceServiceTest, FullValueUpdateClassifiesConstraintAndHaloChanges)
{
  const auto instance = design.netlistStorage().createInstance(createInstance("u1"));
  const auto region = design.constraintStorage().createRegion(
      DesignRegionInput{.name = "region", .rectangles = {{0, 0, 100, 100}}});
  RecordingObserver observer;
  context->addObserver(observer);
  auto value = design.netlistStorage().instance(instance);
  value.flags |= DesignInstanceFlag::kHasRegion | DesignInstanceFlag::kHasHalo;
  value.region = region;
  value.halo = {.left = 1, .bottom = 2, .right = 3, .top = 4};
  const auto epoch = design.geometryStorage().geometryEpoch();
  service->update(instance, std::move(value));
  ASSERT_TRUE(observer.after.has_value());
  EXPECT_TRUE(includes(observer.after->domains, DatabaseChangeDomain::kConstraints));
  EXPECT_TRUE(includes(observer.after->domains, DatabaseChangeDomain::kFloorplan));
  EXPECT_TRUE(includes(observer.after->domains, DatabaseChangeDomain::kGeometry));
  EXPECT_GT(design.geometryStorage().geometryEpoch(), epoch);
}

}  // namespace
}  // namespace eccdb

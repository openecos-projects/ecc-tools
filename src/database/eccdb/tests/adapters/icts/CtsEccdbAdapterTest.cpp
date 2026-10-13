// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// ***************************************************************************************

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "CTSAPI.hh"
#include "Wrapper.hh"
#include "adapter/sdc/SDCClockReader.hh"
#include "context/DatabaseContext.h"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "design/DesignStore.h"
#include "design/netlist/storage/DesignNetlistStorage.h"
#include "library/LibraryStore.h"
#include "library/master_port/storage/MasterPortInput.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

class TemporaryDirectory
{
 public:
  explicit TemporaryDirectory(std::string label)
  {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    _path = std::filesystem::temp_directory_path() / ("eccdb-icts-" + std::move(label) + "-" + std::to_string(suffix));
    std::filesystem::create_directories(_path);
  }

  ~TemporaryDirectory()
  {
    std::error_code error;
    std::filesystem::remove_all(_path, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const { return _path; }

 private:
  std::filesystem::path _path;
};

auto writeTextFile(const std::filesystem::path& path, const std::string& text) -> bool
{
  std::ofstream stream(path);
  stream << text;
  return stream.good();
}

auto makeClockData() -> ::icts::SdcClockData
{
  ::icts::SdcClockData data;
  data.clocks.push_back(::icts::SdcClockDecl{
      .kind = ::icts::SdcClockDecl::Kind::kPrimary,
      .clock_name = "CLK",
      .targets = {{.kind = ::icts::SdcObjectKind::kPort, .pattern = "clk"}},
      .period_ns = 1.0,
      .period_resolved = true,
  });
  return data;
}

auto makeBufferedClockTree(::icts::Wrapper& wrapper, ::icts::Design& cts_design, const std::string& buffer_name) -> ::icts::Clock*
{
  const auto clock_data = makeClockData();
  const auto trace = wrapper.traceSdcClocks(::icts::SdcClockTraceInput{.clock_data = &clock_data, .max_fanout = 32U});
  if (!trace.ok() || !wrapper.readTraceClockTargets(cts_design, trace.output.clock_targets)) {
    return nullptr;
  }
  auto* clock = cts_design.findClock("CLK", "clk_src");
  if (clock == nullptr || clock->get_clock_source_net() == nullptr) {
    return nullptr;
  }
  auto* source = clock->get_clock_source_net();
  const auto original_loads = source->get_loads();
  if (original_loads.size() != 2U) {
    return nullptr;
  }

  auto* buffer = cts_design.makeInst(buffer_name);
  auto* buffer_input = cts_design.makePin("A");
  auto* buffer_output = cts_design.makePin("Y");
  if (buffer == nullptr || buffer_input == nullptr || buffer_output == nullptr) {
    return nullptr;
  }
  buffer->set_cell_master("BUF_X1");
  buffer->set_type(::icts::InstType::kBuffer);
  buffer->set_location(::icts::Point<int>{300, 100});
  buffer_input->set_inst(buffer);
  buffer_input->set_type(::icts::PinType::kIn);
  buffer_input->set_location(::icts::Point<int>{300, 100});
  buffer_output->set_inst(buffer);
  buffer_output->set_type(::icts::PinType::kOut);
  buffer_output->set_location(::icts::Point<int>{400, 100});
  if (!cts_design.indexPin(buffer_input) || !cts_design.indexPin(buffer_output)) {
    return nullptr;
  }
  buffer->add_pin(buffer_input);
  buffer->add_pin(buffer_output);

  source->set_loads({buffer_input});
  buffer_input->set_net(source);
  auto* buffered_net = cts_design.makeNet("clk_" + buffer_name);
  if (buffered_net == nullptr) {
    return nullptr;
  }
  buffered_net->set_driver(buffer_output);
  buffered_net->set_loads(original_loads);
  buffer_output->set_net(buffered_net);
  for (auto* load : original_loads) {
    load->set_net(buffered_net);
  }
  clock->set_loads(original_loads);
  clock->add_net(buffered_net);
  return clock;
}

template <typename Id>
auto containsId(std::span<const Id> values, Id id) -> bool
{
  return std::ranges::find(values, id) != values.end();
}

class NativeCtsEccdbAdapterTest : public testing::Test
{
 protected:
  void SetUp() override
  {
    static_cast<void>(technology.createRoutingLayer(TechLayerInfo{.name = "M1"},
                                                    TechRoutingLayer{.direction = TechRoutingDirection::kHorizontal,
                                                                     .width = 100,
                                                                     .resistance = 0.1,
                                                                     .capacitance = 0.001,
                                                                     .edge_capacitance = 0.0001}));
    design.globalStorage().setInfo(DesignInfo{.name = "native_cts", .database_units_per_micron = 1000});

    const auto site = library.siteStorage().createSite(LibrarySite{.name = "CORE", .width = 100, .height = 100});
    buffer_master = library.cellMasterStorage().createCellMaster(
        LibraryCellMaster{.name = "BUF_X1", .type = LibraryCellMasterType::kCore, .site = site, .width = 100, .height = 100});
    static_cast<void>(library.masterTermStorage().createMasterTerm(
        buffer_master, LibraryMasterTerm{.name = "A", .direction = LibraryMasterTermDirection::kInput}));
    static_cast<void>(library.masterTermStorage().createMasterTerm(
        buffer_master, LibraryMasterTerm{.name = "Y", .direction = LibraryMasterTermDirection::kOutput}));
    static_cast<void>(library.masterTermStorage().createMasterTerm(
        buffer_master, LibraryMasterTerm{.name = "P_SUPPLY", .direction = LibraryMasterTermDirection::kInOut, .use = LibraryMasterTermUse::kPower}));
    static_cast<void>(library.masterTermStorage().createMasterTerm(
        buffer_master, LibraryMasterTerm{.name = "G_RETURN", .direction = LibraryMasterTermDirection::kInOut, .use = LibraryMasterTermUse::kGround}));

    sink_master = library.cellMasterStorage().createCellMaster(
        LibraryCellMaster{.name = "DFF_X1", .type = LibraryCellMasterType::kCore, .site = site, .width = 200, .height = 100});
    static_cast<void>(library.masterTermStorage().createMasterTerm(
        sink_master, LibraryMasterTerm{.name = "CK", .direction = LibraryMasterTermDirection::kInput, .use = LibraryMasterTermUse::kClock}));
    static_cast<void>(library.masterTermStorage().createMasterTerm(
        sink_master, LibraryMasterTerm{.name = "D", .direction = LibraryMasterTermDirection::kInput}));

    auto& netlist = design.netlistStorage();
    source_net = netlist.createNet(DesignNet{.name = "clk_src", .use = DesignSignalUse::kClock});
    unrelated_net = netlist.createNet(DesignNet{.name = "data_keep"});
    power_net = netlist.createSpecialNet(DesignNet{.name = "supply_alpha"});
    ground_net = netlist.createSpecialNet(DesignNet{.name = "return_beta"});
    clock_port = netlist.createBlockTerm(
        DesignBlockTerm{.name = "clk", .direction = DesignIoPinDirection::kInput, .use = DesignSignalUse::kClock});
    sink_one = netlist.createInstance(DesignInstance{.name = "sink_one",
                                                      .master = sink_master,
                                                      .origin = {500, 100},
                                                      .orientation = DesignOrientation::kN,
                                                      .placement_status = DesignPlacementStatus::kPlaced,
                                                      .source = DesignInstanceSource::kNetlist});
    sink_two = netlist.createInstance(DesignInstance{.name = "sink_two",
                                                      .master = sink_master,
                                                      .origin = {700, 100},
                                                      .orientation = DesignOrientation::kN,
                                                      .placement_status = DesignPlacementStatus::kPlaced,
                                                      .source = DesignInstanceSource::kNetlist});
    unrelated_instance = netlist.createInstance(DesignInstance{.name = "unrelated",
                                                                 .master = sink_master,
                                                                 .origin = {900, 100},
                                                                 .orientation = DesignOrientation::kN,
                                                                 .placement_status = DesignPlacementStatus::kPlaced,
                                                                 .source = DesignInstanceSource::kNetlist});
    pg_seed = netlist.createInstance(DesignInstance{.name = "pg_seed",
                                                     .master = buffer_master,
                                                     .origin = {100, 100},
                                                     .orientation = DesignOrientation::kN,
                                                     .placement_status = DesignPlacementStatus::kPlaced,
                                                     .source = DesignInstanceSource::kNetlist});
    sink_one_clock = netlist.findInstanceTerm(sink_one, "CK");
    sink_two_clock = netlist.findInstanceTerm(sink_two, "CK");
    unrelated_data = netlist.findInstanceTerm(unrelated_instance, "D");
    pg_seed_power = netlist.findInstanceTerm(pg_seed, "P_SUPPLY");
    pg_seed_ground = netlist.findInstanceTerm(pg_seed, "G_RETURN");
    ASSERT_TRUE(sink_one_clock);
    ASSERT_TRUE(sink_two_clock);
    ASSERT_TRUE(unrelated_data);
    ASSERT_TRUE(pg_seed_power);
    ASSERT_TRUE(pg_seed_ground);
    netlist.connect(clock_port, source_net);
    netlist.connect(sink_one_clock, source_net);
    netlist.connect(sink_two_clock, source_net);
    netlist.connect(unrelated_data, unrelated_net);
    netlist.connect(pg_seed_power, power_net);
    netlist.connect(pg_seed_ground, ground_net);

    context = std::make_unique<DatabaseContext>(technology, library, design);
  }

  TechStore technology;
  LibraryStore library{technology.techRegistry()};
  DesignStore design{technology.techRegistry(), library.libraryRegistry()};
  std::unique_ptr<DatabaseContext> context;
  LibraryCellMasterId buffer_master;
  LibraryCellMasterId sink_master;
  DesignNetId source_net;
  DesignNetId unrelated_net;
  DesignNetId power_net;
  DesignNetId ground_net;
  DesignBlockTermId clock_port;
  DesignInstanceId sink_one;
  DesignInstanceId sink_two;
  DesignInstanceId unrelated_instance;
  DesignInstanceId pg_seed;
  DesignInstanceTermId sink_one_clock;
  DesignInstanceTermId sink_two_clock;
  DesignInstanceTermId unrelated_data;
  DesignInstanceTermId pg_seed_power;
  DesignInstanceTermId pg_seed_ground;
};

TEST_F(NativeCtsEccdbAdapterTest, TracesSdcPortAndMaterializesWithoutIdbDesign)
{
  ::icts::Wrapper wrapper;
  wrapper.initNative(context.get());
  ASSERT_TRUE(wrapper.is_native());
  ASSERT_TRUE(wrapper.is_design_ready());

  const auto clock_data = makeClockData();
  const auto trace = wrapper.traceSdcClocks(::icts::SdcClockTraceInput{.clock_data = &clock_data, .max_fanout = 32U});
  ASSERT_TRUE(trace.ok()) << trace.message;
  ASSERT_EQ(trace.output.clock_targets.size(), 1U);
  EXPECT_EQ(trace.output.clock_targets.front().clock_name, "CLK");
  EXPECT_EQ(trace.output.clock_targets.front().clock_net_name, "clk_src");

  ::icts::Design cts_design;
  ASSERT_TRUE(wrapper.readTraceClockTargets(cts_design, trace.output.clock_targets));
  ASSERT_TRUE(cts_design.rebuildClockDAG());
  auto* clock = cts_design.findClock("CLK", "clk_src");
  ASSERT_NE(clock, nullptr);
  ASSERT_NE(clock->get_clock_source(), nullptr);
  ASSERT_NE(clock->get_clock_source_net(), nullptr);
  EXPECT_EQ(clock->get_clock_source()->get_name(), "clk");
  EXPECT_EQ(clock->get_clock_source_net()->get_name(), "clk_src");
  EXPECT_EQ(clock->get_loads().size(), 2U);
}

TEST_F(NativeCtsEccdbAdapterTest, TracesOverlappingSdcReferencesOnlyOnceInFirstMatchOrder)
{
  const auto extra_clock_net = design.netlistStorage().createNet(DesignNet{.name = "clk_aux", .use = DesignSignalUse::kClock});
  ::icts::Wrapper wrapper;
  wrapper.initNative(context.get());

  auto clock_data = makeClockData();
  auto& declaration = clock_data.clocks.front();
  declaration.targets = {
      {.kind = ::icts::SdcObjectKind::kNet, .pattern = "clk_src"},
      {.kind = ::icts::SdcObjectKind::kNet, .pattern = "clk_*"},
      {.kind = ::icts::SdcObjectKind::kPort, .pattern = "clk"},
      {.kind = ::icts::SdcObjectKind::kPin, .pattern = "sink_*:CK"},
      {.kind = ::icts::SdcObjectKind::kNet, .pattern = "clk_src"},
  };

  const auto trace = wrapper.traceSdcClocks(::icts::SdcClockTraceInput{.clock_data = &clock_data, .max_fanout = 32U});
  ASSERT_TRUE(trace.ok()) << trace.message;
  ASSERT_EQ(trace.output.clock_targets.size(), 2U);
  EXPECT_EQ(trace.output.clock_targets[0].clock_name, "CLK");
  EXPECT_EQ(trace.output.clock_targets[0].clock_net_name, "clk_src");
  EXPECT_EQ(trace.output.clock_targets[1].clock_name, "CLK");
  EXPECT_EQ(trace.output.clock_targets[1].clock_net_name, design.netlistStorage().net(extra_clock_net).name);
  ASSERT_EQ(trace.summary.records.size(), 2U);
  EXPECT_EQ(trace.summary.records[0].net_name, "clk_src");
  EXPECT_EQ(trace.summary.records[0].status, "accepted");
  EXPECT_EQ(trace.summary.records[1].net_name, design.netlistStorage().net(extra_clock_net).name);
  EXPECT_EQ(trace.summary.records[1].status, "accepted");
}

TEST_F(NativeCtsEccdbAdapterTest, WritesBufferTreeThroughServicesAndPreservesFanout)
{
  ::icts::Wrapper wrapper;
  wrapper.initNative(context.get());
  const auto clock_data = makeClockData();
  const auto trace = wrapper.traceSdcClocks(::icts::SdcClockTraceInput{.clock_data = &clock_data, .max_fanout = 32U});
  ASSERT_TRUE(trace.ok()) << trace.message;

  ::icts::Design cts_design;
  ASSERT_TRUE(wrapper.readTraceClockTargets(cts_design, trace.output.clock_targets));
  auto* clock = cts_design.findClock("CLK", "clk_src");
  ASSERT_NE(clock, nullptr);
  auto* source = clock->get_clock_source_net();
  ASSERT_NE(source, nullptr);
  const auto original_loads = source->get_loads();
  ASSERT_EQ(original_loads.size(), 2U);

  auto* buffer = cts_design.makeInst("cts_buf_0");
  ASSERT_NE(buffer, nullptr);
  buffer->set_cell_master("BUF_X1");
  buffer->set_type(::icts::InstType::kBuffer);
  buffer->set_location(::icts::Point<int>{300, 100});
  auto* buffer_input = cts_design.makePin("A");
  auto* buffer_output = cts_design.makePin("Y");
  ASSERT_NE(buffer_input, nullptr);
  ASSERT_NE(buffer_output, nullptr);
  buffer_input->set_inst(buffer);
  buffer_input->set_type(::icts::PinType::kIn);
  buffer_input->set_location(::icts::Point<int>{300, 100});
  buffer_output->set_inst(buffer);
  buffer_output->set_type(::icts::PinType::kOut);
  buffer_output->set_location(::icts::Point<int>{400, 100});
  ASSERT_TRUE(cts_design.indexPin(buffer_input));
  ASSERT_TRUE(cts_design.indexPin(buffer_output));
  buffer->add_pin(buffer_input);
  buffer->add_pin(buffer_output);

  source->set_loads({buffer_input});
  buffer_input->set_net(source);
  auto* buffered_net = cts_design.makeNet("clk_buf_0");
  ASSERT_NE(buffered_net, nullptr);
  buffered_net->set_driver(buffer_output);
  buffered_net->set_loads(original_loads);
  buffer_output->set_net(buffered_net);
  for (auto* load : original_loads) {
    ASSERT_NE(load, nullptr);
    load->set_net(buffered_net);
  }
  clock->set_loads(original_loads);
  clock->add_net(buffered_net);

  const auto summary = wrapper.writeClocksDetailed(cts_design, {clock});
  ASSERT_TRUE(summary.success) << summary.reason;
  EXPECT_EQ(summary.inserted_inst_count, 1U);
  EXPECT_EQ(summary.inserted_net_count, 1U);

  auto& netlist = design.netlistStorage();
  const auto buffer_id = netlist.findInstance("cts_buf_0");
  const auto buffered_net_id = netlist.findRegularNet("clk_buf_0");
  ASSERT_TRUE(buffer_id);
  ASSERT_TRUE(buffered_net_id);
  EXPECT_EQ(netlist.instance(buffer_id).source, DesignInstanceSource::kTiming);
  EXPECT_EQ(netlist.instance(buffer_id).origin, (Point{300, 100}));
  EXPECT_EQ(netlist.net(buffered_net_id).source, DesignNetSource::kTiming);
  EXPECT_EQ(netlist.net(buffered_net_id).use, DesignSignalUse::kClock);
  EXPECT_EQ(netlist.net(source_net).use, DesignSignalUse::kClock);

  const auto buffer_input_term = netlist.findInstanceTerm(buffer_id, "A");
  const auto buffer_output_term = netlist.findInstanceTerm(buffer_id, "Y");
  const auto buffer_power_term = netlist.findInstanceTerm(buffer_id, "P_SUPPLY");
  const auto buffer_ground_term = netlist.findInstanceTerm(buffer_id, "G_RETURN");
  ASSERT_TRUE(buffer_input_term);
  ASSERT_TRUE(buffer_output_term);
  ASSERT_TRUE(buffer_power_term);
  ASSERT_TRUE(buffer_ground_term);
  EXPECT_EQ(netlist.blockTerm(clock_port).net, source_net);
  EXPECT_EQ(netlist.instanceTerm(buffer_input_term).net, source_net);
  EXPECT_EQ(netlist.instanceTerm(buffer_output_term).net, buffered_net_id);
  EXPECT_EQ(netlist.instanceTerm(buffer_power_term).special_net, power_net);
  EXPECT_EQ(netlist.instanceTerm(buffer_ground_term).special_net, ground_net);
  EXPECT_EQ(netlist.instanceTerm(sink_one_clock).net, buffered_net_id);
  EXPECT_EQ(netlist.instanceTerm(sink_two_clock).net, buffered_net_id);
  EXPECT_EQ(netlist.instanceTerm(unrelated_data).net, unrelated_net);
  EXPECT_TRUE(containsId(netlist.blockTerms(source_net), clock_port));
  EXPECT_TRUE(containsId(netlist.instanceTerms(source_net), buffer_input_term));
  EXPECT_TRUE(containsId(netlist.instanceTerms(buffered_net_id), buffer_output_term));
  EXPECT_TRUE(containsId(netlist.instanceTerms(buffered_net_id), sink_one_clock));
  EXPECT_TRUE(containsId(netlist.instanceTerms(buffered_net_id), sink_two_clock));
}

TEST_F(NativeCtsEccdbAdapterTest, RejectsAmbiguousPowerRailBindingsBeforeWritingClockTree)
{
  auto& netlist = design.netlistStorage();
  const auto alternate_supply = netlist.createSpecialNet(DesignNet{.name = "supply_gamma"});
  const auto conflicting_seed = netlist.createInstance(DesignInstance{.name = "pg_conflicting_seed",
                                                                        .master = buffer_master,
                                                                        .origin = {200, 100},
                                                                        .orientation = DesignOrientation::kN,
                                                                        .placement_status = DesignPlacementStatus::kPlaced,
                                                                        .source = DesignInstanceSource::kNetlist});
  const auto conflicting_power = netlist.findInstanceTerm(conflicting_seed, "P_SUPPLY");
  ASSERT_TRUE(conflicting_power);
  netlist.connect(conflicting_power, alternate_supply);

  ::icts::Wrapper wrapper;
  wrapper.initNative(context.get());
  ::icts::Design cts_design;
  auto* clock = makeBufferedClockTree(wrapper, cts_design, "cts_buf_ambiguous");
  ASSERT_NE(clock, nullptr);

  const auto summary = wrapper.writeClocksDetailed(cts_design, {clock});
  EXPECT_FALSE(summary.success);
  EXPECT_EQ(summary.reason, "ambiguous_cts_pg_binding:cts_buf_ambiguous/P_SUPPLY");
  EXPECT_FALSE(netlist.findInstance("cts_buf_ambiguous"));
  EXPECT_EQ(netlist.instanceTerm(sink_one_clock).net, source_net);
  EXPECT_EQ(netlist.instanceTerm(sink_two_clock).net, source_net);
}

TEST_F(NativeCtsEccdbAdapterTest, MaterializesRectOnlyClockPinLocationsWithIdbCompatibleAnchorArithmetic)
{
  // This is the real H7CR DFF CK geometry reduced to DBU coordinates.  VIA1
  // is deliberately a cut-layer RECT, rather than a VIA placement: iDB's
  // anchor arithmetic includes all three RECTs.
  const auto m1 = technology.findLayer("M1");
  ASSERT_TRUE(m1);
  const auto via1 = technology.createCutLayer(TechLayerInfo{.name = "VIA1"}, TechCutLayer{.width = 90});
  const auto m2 = technology.createRoutingLayer(TechLayerInfo{.name = "M2"}, TechRoutingLayer{.width = 100});
  const auto site = library.siteStorage().createSite(LibrarySite{.name = "WITNESS_SITE", .width = 100, .height = 100});
  const auto master = library.cellMasterStorage().createCellMaster(
      LibraryCellMaster{.name = "DFFQX1H7R_WITNESS", .type = LibraryCellMasterType::kCore, .site = site, .width = 4400, .height = 1400});
  const auto clock_term = library.masterTermStorage().createMasterTerm(
      master, LibraryMasterTerm{.name = "CK", .direction = LibraryMasterTermDirection::kInput, .use = LibraryMasterTermUse::kClock});
  const auto clock_pin = library.masterPortStorage().createMasterPin(
      clock_term,
      LibraryMasterPortInput{.layer_clauses = {{.layer = m1, .geometry = {.rects = {{250, 570, 350, 840}}}},
                                                 {.layer = TechLayerId{m2.entity()}, .geometry = {.rects = {{250, 425, 350, 960}}}},
                                                 {.layer = TechLayerId{via1.entity()}, .geometry = {.rects = {{255, 655, 345, 745}}}}}});
  ASSERT_TRUE(clock_pin);

  struct Witness
  {
    std::string name;
    DesignOrientation orientation;
    Point expected;
  };
  const std::vector<Witness> witnesses{
      {"anchor_n", DesignOrientation::kN, {258100, 374499}},
      {"anchor_w", DesignOrientation::kW, {258501, 374100}},
      {"anchor_s", DesignOrientation::kS, {261900, 374501}},
      {"anchor_e", DesignOrientation::kE, {258499, 377900}},
      {"anchor_fn", DesignOrientation::kFN, {261900, 374499}},
      {"anchor_fs", DesignOrientation::kFS, {258100, 374501}},
      {"anchor_fw", DesignOrientation::kFW, {258499, 374100}},
      {"anchor_fe", DesignOrientation::kFE, {258501, 377900}},
  };

  auto& netlist = design.netlistStorage();
  for (const auto& witness : witnesses) {
    const auto instance = netlist.createInstance(DesignInstance{.name = witness.name,
                                                                  .master = master,
                                                                  .origin = {257800, 373800},
                                                                  .orientation = witness.orientation,
                                                                  .placement_status = DesignPlacementStatus::kPlaced,
                                                                  .source = DesignInstanceSource::kNetlist});
    const auto pin = netlist.findInstanceTerm(instance, "CK");
    ASSERT_TRUE(pin);
    netlist.connect(pin, source_net);
  }

  ::icts::Wrapper wrapper;
  wrapper.initNative(context.get());
  const auto clock_data = makeClockData();
  const auto trace = wrapper.traceSdcClocks(::icts::SdcClockTraceInput{.clock_data = &clock_data, .max_fanout = 32U});
  ASSERT_TRUE(trace.ok()) << trace.message;

  ::icts::Design cts_design;
  ASSERT_TRUE(wrapper.readTraceClockTargets(cts_design, trace.output.clock_targets));
  for (const auto& witness : witnesses) {
    const auto* pin = cts_design.findPin(witness.name + "/CK");
    ASSERT_NE(pin, nullptr) << witness.name;
    EXPECT_EQ(pin->get_location(), (::icts::Point<int>{witness.expected.x, witness.expected.y})) << witness.name;
  }
}

TEST_F(NativeCtsEccdbAdapterTest, AbortReleasesNativeSourcesAndAllowsRestart)
{
  TemporaryDirectory files{"abort-restart"};
  const auto config_path = files.path() / "cts.json";
  const auto sdc_path = files.path() / "cts.sdc";
  const auto work_dir = files.path() / "work";
  ASSERT_TRUE(writeTextFile(config_path, R"json({"buffer_type":["BUF_X1"],"routing_layer":[1]})json"));
  ASSERT_TRUE(writeTextFile(sdc_path, "create_clock -name CLK -period 1 [get_nets clk_src]\n"));

  auto& api = ::icts::CTSAPI::getInst();
  (void) ::icts::CTSAPI::abortCTS();
  api.setDesignSource(*context);
  api.setSdcPath(sdc_path.string());
  api.setLibertyCellLookup([](const std::string&) -> idb::LibCell* { return nullptr; });
  const auto first = ::icts::CTSAPI::init(config_path.string(), work_dir.string());
  ASSERT_TRUE(first.ok()) << first.message;
  EXPECT_TRUE(::icts::CTSAPI::abortCTS().ok());
  EXPECT_EQ(::icts::CTSAPI::runCTS().code, ::icts::CTSStatusCode::kNotInitialized);

  api.setDesignSource(*context);
  api.setSdcPath(sdc_path.string());
  api.setLibertyCellLookup([](const std::string&) -> idb::LibCell* { return nullptr; });
  const auto second = ::icts::CTSAPI::init(config_path.string(), work_dir.string());
  EXPECT_TRUE(second.ok()) << second.message;
  EXPECT_TRUE(::icts::CTSAPI::abortCTS().ok());
}

}  // namespace
}  // namespace eccdb

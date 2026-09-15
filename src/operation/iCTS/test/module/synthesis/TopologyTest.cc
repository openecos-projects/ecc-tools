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
/**
 * @file TopologyTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-17
 * @brief Guard-behavior coverage for Topology invalid inputs.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "data_manager/DataManager.hh"
#include "data_manager/config/Config.hh"
#include "data_manager/design/Clock.hh"
#include "data_manager/design/Design.hh"
#include "data_manager/design/Inst.hh"
#include "data_manager/design/Net.hh"
#include "data_manager/design/Pin.hh"
#include "data_manager/io/Wrapper.hh"
#include "data_manager/spatial/Point.hh"
#include "module/synthesis/realization/ClockTreeRealization.hh"
#include "module/synthesis/topology/SourceTrunkStage.hh"
#include "module/synthesis/topology/Topology.hh"
#include "module/synthesis/topology/trunk/SourceTrunk.hh"

namespace icts_test {
namespace {

class ScopedConfigReset
{
 public:
  ScopedConfigReset()
  {
    CTSDM.getConfig().reset();
    CTSDM.getDesign().reset();
  }
  ~ScopedConfigReset()
  {
    CTSDM.getConfig().reset();
    CTSDM.getDesign().reset();
  }
};

auto MakeUniqueTempPath(const std::string& file_name) -> std::filesystem::path
{
  return std::filesystem::temp_directory_path() / ("icts_topology_test_" + file_name);
}

auto makeDesignInst(const std::string& name, const std::string& cell_master, icts::InstType type, const icts::Point<int>& location) -> icts::Inst*
{
  auto* inst = CTSDM.getDesign().makeInst(name);
  if (inst == nullptr) {
    return nullptr;
  }
  inst->set_name(name);
  inst->set_cell_master(cell_master);
  inst->set_type(type);
  inst->set_location(location);
  return inst;
}

auto makeDesignPin(icts::Inst* inst, const std::string& name, icts::PinType type, const icts::Point<int>& location) -> icts::Pin*
{
  auto* pin = CTSDM.getDesign().makePin(name);
  if (pin == nullptr) {
    return nullptr;
  }
  pin->set_name(name);
  pin->set_type(type);
  pin->set_location(location);
  pin->set_inst(inst);
  pin->set_net(nullptr);
  pin->set_io(false);
  if (inst != nullptr) {
    inst->add_pin(pin);
  }
  (void) CTSDM.getDesign().indexPin(pin);
  return pin;
}

auto makeDesignNet(const std::string& name, icts::Pin* driver = nullptr, const std::vector<icts::Pin*>& loads = {}) -> icts::Net*
{
  auto* net = CTSDM.getDesign().makeNet(name);
  if (net == nullptr) {
    return nullptr;
  }
  net->set_name(name);
  net->set_driver(driver);
  if (driver != nullptr) {
    driver->set_net(net);
  }
  net->set_loads({});
  for (auto* load : loads) {
    if (load == nullptr) {
      continue;
    }
    net->add_load(load);
    load->set_net(net);
  }
  return net;
}

auto BuildTopologyForRootNet(icts::Net& root_net) -> icts::Topology::Build
{
  return icts::Topology::build(
      icts::Topology::Input{
          .config = &CTSDM.getConfig(),
          .design = &CTSDM.getDesign(),
          .wrapper = &CTSDM.getWrapper(),
          .fast_sta = &CTSDM.getFastSTA(),
          .root_net = &root_net,
          .object_name_prefix = {},
          .characterization_library = nullptr,
          .additional_characterization_lengths_um = {},
          .clock_period_ns = 0.0,
          .clock_period_source = {},
          .log_context = {},
      },
      icts::Topology::Config{});
}

TEST(TopologyTest, RootNetWithNullDriverFailsWithoutInsertedObjects)
{
  icts::Pin sink("sink_0", icts::PinType::kClock);
  icts::Net root_net("root_net");
  root_net.add_load(&sink);
  sink.set_net(&root_net);

  const auto result = BuildTopologyForRootNet(root_net);

  EXPECT_FALSE(result.summary.success);
  EXPECT_EQ(result.output.inserted_insts.size(), 0U);
  EXPECT_EQ(result.output.inserted_nets.size(), 0U);
  EXPECT_TRUE(result.output.cluster_buffers.empty());
  EXPECT_FALSE(result.summary.failure_reason.empty());
}

TEST(TopologyTest, RootNetWithEmptyLoadListFailsWithoutInsertedObjects)
{
  icts::Pin source("clk_src", icts::PinType::kClock);
  icts::Net root_net("root_net");
  root_net.set_driver(&source);
  source.set_net(&root_net);

  const auto result = BuildTopologyForRootNet(root_net);

  EXPECT_FALSE(result.summary.success);
  EXPECT_EQ(result.output.inserted_insts.size(), 0U);
  EXPECT_EQ(result.output.inserted_nets.size(), 0U);
  EXPECT_TRUE(result.output.cluster_buffers.empty());
  EXPECT_FALSE(result.summary.failure_reason.empty());
}

TEST(TopologyTest, RootNetWithMissingDriverAndLoadsFailsSafely)
{
  icts::Net root_net("root_net");
  const auto result = BuildTopologyForRootNet(root_net);

  EXPECT_FALSE(result.summary.success);
  EXPECT_EQ(result.output.inserted_insts.size(), 0U);
  EXPECT_EQ(result.output.inserted_nets.size(), 0U);
  EXPECT_TRUE(result.output.cluster_buffers.empty());
  EXPECT_FALSE(result.summary.failure_reason.empty());
}

TEST(TopologyTest, BuildFailurePreservesBorrowedMembership)
{
  const ScopedConfigReset scoped_config_reset;
  icts::Clock invalid_clock("clk", "clk_net");
  auto* stale_net = makeDesignNet("cts_net_0");
  icts::Net invalid_root_net("invalid_root_net");

  invalid_clock.add_net(stale_net);
  ASSERT_TRUE(invalid_clock.get_insts().empty());
  ASSERT_EQ(invalid_clock.get_nets().size(), 1U);

  const auto result = BuildTopologyForRootNet(invalid_root_net);

  EXPECT_FALSE(result.summary.success);
  EXPECT_FALSE(result.summary.failure_reason.empty());
  ASSERT_TRUE(invalid_clock.get_insts().empty());
  ASSERT_EQ(invalid_clock.get_nets().size(), 1U);
  EXPECT_EQ(invalid_clock.get_nets().front(), stale_net);
  EXPECT_EQ(CTSDM.getDesign().findNet("cts_net_0"), stale_net);
}

TEST(TopologyTest, DesignCommitRejectsFinalNameCollisions)
{
  const ScopedConfigReset scoped_config_reset;

  auto* existing_inst = makeDesignInst("u0", "REG_X1", icts::InstType::kFlipFlop, icts::Point<int>(10, 20));
  auto* existing_pin = makeDesignPin(existing_inst, "CLK", icts::PinType::kClock, existing_inst->get_location());
  auto* existing_net = makeDesignNet("clk_net");
  ASSERT_NE(existing_inst, nullptr);
  ASSERT_NE(existing_pin, nullptr);
  ASSERT_NE(existing_net, nullptr);

  auto colliding_inst = std::make_unique<icts::Inst>("u0", "BUF_X1", icts::InstType::kBuffer, icts::Point<int>(0, 0));
  auto colliding_pin = std::make_unique<icts::Pin>("CLK", icts::PinType::kClock, existing_inst->get_location(), existing_inst, nullptr, false);
  auto colliding_net = std::make_unique<icts::Net>("clk_net");

  EXPECT_EQ(CTSDM.getDesign().commitInst(std::move(colliding_inst)), nullptr);
  EXPECT_EQ(CTSDM.getDesign().commitPin(std::move(colliding_pin)), nullptr);
  EXPECT_EQ(CTSDM.getDesign().commitNet(std::move(colliding_net)), nullptr);

  EXPECT_EQ(CTSDM.getDesign().findInst("u0"), existing_inst);
  EXPECT_EQ(CTSDM.getDesign().findPin(icts::Design::getPinFullName(existing_pin)), existing_pin);
  EXPECT_EQ(CTSDM.getDesign().findNet("clk_net"), existing_net);
  EXPECT_EQ(existing_inst->get_cell_master(), "REG_X1");
  EXPECT_EQ(existing_inst->get_type(), icts::InstType::kFlipFlop);
  EXPECT_EQ(CTSDM.getDesign().get_insts().size(), 1U);
  EXPECT_EQ(CTSDM.getDesign().get_pins().size(), 1U);
  EXPECT_EQ(CTSDM.getDesign().get_nets().size(), 1U);
}

TEST(TopologyTest, DesignOwnsFinalObjectsAndClockKeepsMembershipOnly)
{
  const ScopedConfigReset scoped_config_reset;
  icts::Clock clock("clk", "clk_net");

  auto* inst = makeDesignInst("cts_buf_0", "BUF_X1", icts::InstType::kBuffer, icts::Point<int>(10, 20));
  ASSERT_NE(inst, nullptr);
  auto* input_pin = makeDesignPin(inst, "A", icts::PinType::kIn, inst->get_location());
  auto* output_pin = makeDesignPin(inst, "Y", icts::PinType::kOut, inst->get_location());
  ASSERT_NE(input_pin, nullptr);
  ASSERT_NE(output_pin, nullptr);
  inst->add_pin(output_pin);

  auto* net = makeDesignNet("cts_net_0", output_pin, std::vector<icts::Pin*>{input_pin});
  ASSERT_NE(net, nullptr);
  ASSERT_TRUE(clock
                  .addPropagationArc({.inst = inst,
                                      .input_pin = input_pin,
                                      .output_pin = output_pin,
                                      .kind = icts::ClockPropagationKind::kBuffer,
                                      .origin = icts::ClockPropagationOrigin::kSynthesized})
                  .ok());
  clock.add_net(net);

  ASSERT_EQ(clock.get_insts().size(), 1U);
  ASSERT_EQ(clock.get_nets().size(), 1U);
  EXPECT_EQ(clock.get_insts().front()->get_name(), "cts_buf_0");
  EXPECT_EQ(clock.get_nets().front()->get_name(), "cts_net_0");
  EXPECT_EQ(CTSDM.getDesign().get_insts().size(), 1U);
  EXPECT_EQ(CTSDM.getDesign().get_pins().size(), 2U);
  EXPECT_EQ(CTSDM.getDesign().get_nets().size(), 1U);
  EXPECT_EQ(net->get_driver(), output_pin);
  EXPECT_EQ(output_pin->get_net(), net);
  ASSERT_EQ(net->get_loads().size(), 1U);
  EXPECT_EQ(net->get_loads().front(), input_pin);
  EXPECT_EQ(input_pin->get_net(), net);

  CTSDM.getDesign().removeClockMembershipObjects(clock);
  clock.clearMembership();
  EXPECT_TRUE(clock.get_insts().empty());
  EXPECT_TRUE(clock.get_nets().empty());
  EXPECT_TRUE(CTSDM.getDesign().get_insts().empty());
  EXPECT_TRUE(CTSDM.getDesign().get_pins().empty());
  EXPECT_TRUE(CTSDM.getDesign().get_nets().empty());
}

TEST(TopologyTest, InsertedObjectCommitRequiresProducerOwnedPropagationPayload)
{
  const ScopedConfigReset scoped_config_reset;
  icts::Clock clock("clk", "clk_net");
  std::vector<std::unique_ptr<icts::Inst>> inserted_insts;
  std::vector<std::unique_ptr<icts::Pin>> inserted_pins;
  std::vector<std::unique_ptr<icts::Net>> inserted_nets;
  std::vector<icts::ClockPropagationArc> propagation_arcs;

  auto inst = std::make_unique<icts::Inst>("cts_buf", "BUF_X1", icts::InstType::kBuffer, icts::Point<int>(10, 20));
  auto* inst_ptr = inst.get();
  auto input_pin = std::make_unique<icts::Pin>("A", icts::PinType::kIn, inst_ptr->get_location(), inst_ptr, nullptr, false);
  auto* input_pin_ptr = input_pin.get();
  auto output_pin = std::make_unique<icts::Pin>("Y", icts::PinType::kOut, inst_ptr->get_location(), inst_ptr, nullptr, false);
  auto* output_pin_ptr = output_pin.get();
  inst_ptr->add_pin(input_pin_ptr);
  inst_ptr->add_pin(output_pin_ptr);
  inserted_insts.push_back(std::move(inst));
  inserted_pins.push_back(std::move(input_pin));
  inserted_pins.push_back(std::move(output_pin));

  const auto commit = [&]() -> bool {
    return icts::ClockTreeRealization::commitInsertedObjects(icts::InsertedObjectCommitInput{
        .design = &CTSDM.getDesign(),
        .clock = &clock,
        .inserted_insts = &inserted_insts,
        .inserted_pins = &inserted_pins,
        .inserted_nets = &inserted_nets,
        .propagation_arcs = &propagation_arcs,
    });
  };

  EXPECT_FALSE(commit());
  EXPECT_EQ(inserted_insts.size(), 1U);
  EXPECT_EQ(inserted_pins.size(), 2U);
  EXPECT_EQ(CTSDM.getDesign().findInst("cts_buf"), nullptr);
  EXPECT_TRUE(clock.get_propagation_arcs().empty());

  propagation_arcs.push_back(icts::ClockPropagationArc{
      .inst = inst_ptr,
      .input_pin = input_pin_ptr,
      .output_pin = output_pin_ptr,
      .kind = icts::ClockPropagationKind::kBuffer,
      .origin = icts::ClockPropagationOrigin::kSynthesized,
      .path_buffer_weight = 1,
  });

  inst_ptr->set_type(icts::InstType::kFlipFlop);
  EXPECT_FALSE(commit());
  EXPECT_EQ(inserted_insts.size(), 1U);
  EXPECT_EQ(inserted_pins.size(), 2U);
  EXPECT_EQ(propagation_arcs.size(), 1U);
  EXPECT_EQ(CTSDM.getDesign().findInst("cts_buf"), nullptr);
  EXPECT_EQ(CTSDM.getDesign().findPin("cts_buf/A"), nullptr);
  EXPECT_EQ(CTSDM.getDesign().findPin("cts_buf/Y"), nullptr);
  EXPECT_TRUE(clock.get_propagation_arcs().empty());

  inst_ptr->set_type(icts::InstType::kBuffer);
  input_pin_ptr->set_type(icts::PinType::kOut);
  EXPECT_FALSE(commit());
  EXPECT_EQ(inserted_insts.size(), 1U);
  EXPECT_EQ(inserted_pins.size(), 2U);
  EXPECT_EQ(propagation_arcs.size(), 1U);
  EXPECT_EQ(CTSDM.getDesign().findInst("cts_buf"), nullptr);
  EXPECT_EQ(CTSDM.getDesign().findPin("cts_buf/A"), nullptr);
  EXPECT_EQ(CTSDM.getDesign().findPin("cts_buf/Y"), nullptr);
  EXPECT_TRUE(clock.get_propagation_arcs().empty());

  input_pin_ptr->set_type(icts::PinType::kIn);
  ASSERT_TRUE(commit());
  EXPECT_TRUE(inserted_insts.empty());
  EXPECT_TRUE(inserted_pins.empty());
  EXPECT_TRUE(propagation_arcs.empty());
  ASSERT_EQ(clock.get_propagation_arcs().size(), 1U);
  EXPECT_EQ(clock.get_propagation_arcs().front().inst, CTSDM.getDesign().findInst("cts_buf"));
  EXPECT_EQ(clock.get_propagation_arcs().front().input_pin, CTSDM.getDesign().findPin("cts_buf/A"));
  EXPECT_EQ(clock.get_propagation_arcs().front().output_pin, CTSDM.getDesign().findPin("cts_buf/Y"));
}

TEST(TopologyTest, InstPinMembershipIsUniqueAndOrderIndependent)
{
  icts::Inst inst("cts_buf_0", "BUF_X1", icts::InstType::kBuffer, icts::Point<int>(0, 0));
  icts::Pin driver_pin("Y", icts::PinType::kOut);

  inst.add_pin(&driver_pin);

  ASSERT_EQ(inst.get_pins().size(), 1U);
  EXPECT_EQ(inst.get_pins().front(), &driver_pin);

  inst.add_pin(&driver_pin);

  ASSERT_EQ(inst.get_pins().size(), 1U);
  EXPECT_EQ(inst.get_pins().front(), &driver_pin);
}

TEST(TopologyTest, EnableSinkClusteringDefaultsTrueAndParsesConfiguredValues)
{
  const ScopedConfigReset scoped_config_reset;
  EXPECT_TRUE(CTSDM.getConfig().is_enable_sink_clustering());
  EXPECT_DOUBLE_EQ(CTSDM.getConfig().get_htree_topology_tolerance(), 0.1);
  EXPECT_DOUBLE_EQ(CTSDM.getConfig().get_root_input_slew(), 0.0);

  const auto json_path = MakeUniqueTempPath("config.json");
  {
    std::ofstream output_stream(json_path);
    ASSERT_TRUE(output_stream.is_open());
    output_stream
        << R"({"enable_sink_clustering": false, "htree_topology_tolerance": 0.25, "root_input_slew": 0.123, "routing_layer": [5, 6], "wire_width": 0.12, "wirelength_unit_um": 12.5, "wirelength_iterations": 7})";
  }

  CTSDM.getConfig().parse(json_path.string());
  EXPECT_FALSE(CTSDM.getConfig().is_enable_sink_clustering());
  EXPECT_DOUBLE_EQ(CTSDM.getConfig().get_htree_topology_tolerance(), 0.25);
  EXPECT_DOUBLE_EQ(CTSDM.getConfig().get_root_input_slew(), 0.123);
  EXPECT_DOUBLE_EQ(CTSDM.getConfig().get_wirelength_unit_um(), 12.5);
  EXPECT_EQ(CTSDM.getConfig().get_wirelength_iterations(), 7U);

  std::error_code error_code;
  std::filesystem::remove(json_path, error_code);
}

TEST(TopologyTest, MissingConfigFileFailsWithPathDiagnostic)
{
  const ScopedConfigReset scoped_config_reset;
  const auto missing_path = MakeUniqueTempPath("missing_config.json");
  std::error_code error_code;
  std::filesystem::remove(missing_path, error_code);

  EXPECT_FALSE(CTSDM.getConfig().init(missing_path.string()));

  EXPECT_NE(CTSDM.getConfig().get_last_error().find("failed to open iCTS config file"), std::string::npos);
  EXPECT_NE(CTSDM.getConfig().get_last_error().find(missing_path.string()), std::string::npos);
}

TEST(TopologyTest, ConfigBoolParsingAcceptsTypedNumericAndStringTokens)
{
  const ScopedConfigReset scoped_config_reset;
  const auto json_path = MakeUniqueTempPath("bool_config.json");
  {
    std::ofstream output_stream(json_path);
    ASSERT_TRUE(output_stream.is_open());
    output_stream << R"({
      "force_branch_buffer": true,
      "enable_sink_clustering": 0
    })";
  }

  EXPECT_TRUE(CTSDM.getConfig().parse(json_path.string()));

  EXPECT_TRUE(CTSDM.getConfig().is_force_branch_buffer());
  EXPECT_FALSE(CTSDM.getConfig().is_enable_sink_clustering());
  EXPECT_TRUE(CTSDM.getConfig().get_last_error().empty());

  std::error_code error_code;
  std::filesystem::remove(json_path, error_code);
}

TEST(TopologyTest, InvalidStringBoolFailsWithoutSilentFalse)
{
  const ScopedConfigReset scoped_config_reset;
  const auto json_path = MakeUniqueTempPath("invalid_bool_config.json");
  {
    std::ofstream output_stream(json_path);
    ASSERT_TRUE(output_stream.is_open());
    output_stream << R"({"force_branch_buffer": "maybe"})";
  }

  EXPECT_FALSE(CTSDM.getConfig().parse(json_path.string()));

  EXPECT_NE(CTSDM.getConfig().get_last_error().find("invalid boolean value"), std::string::npos);
  EXPECT_NE(CTSDM.getConfig().get_last_error().find("force_branch_buffer"), std::string::npos);
  EXPECT_NE(CTSDM.getConfig().get_last_error().find("maybe"), std::string::npos);

  std::error_code error_code;
  std::filesystem::remove(json_path, error_code);
}

TEST(TopologyTest, SourceTrunkWithEmptyRootsFailsWithoutChangingSourceNet)
{
  icts::Pin source("clk_src", icts::PinType::kOut, icts::Point<int>(0, 0));
  icts::Pin original_load("sink", icts::PinType::kClock, icts::Point<int>(100, 0));
  icts::Net source_net("clk_net");
  source_net.set_driver(&source);
  source.set_net(&source_net);
  source_net.add_load(&original_load);
  original_load.set_net(&source_net);

  const auto result = icts::topology::BuildSourceTrunkTree(icts::topology::SourceTrunkInput{.config = &CTSDM.getConfig(),
                                                                                            .design = &CTSDM.getDesign(),
                                                                                            .wrapper = &CTSDM.getWrapper(),
                                                                                            .fast_sta = &CTSDM.getFastSTA(),
                                                                                            .source_net = &source_net,
                                                                                            .clock_source = &source,
                                                                                            .root_inputs = {},
                                                                                            .object_name_prefix = {},
                                                                                            .characterization_library = nullptr,
                                                                                            .clock_period_ns = 0.0,
                                                                                            .clock_period_source = {},
                                                                                            .log_context = {}});

  EXPECT_FALSE(result.summary.success);
  EXPECT_FALSE(result.summary.failure_reason.empty());
  EXPECT_EQ(result.output.inserted_insts.size(), 0U);
  EXPECT_EQ(result.output.inserted_nets.size(), 0U);
  EXPECT_EQ(source_net.get_driver(), &source);
  ASSERT_EQ(source_net.get_loads().size(), 1U);
  EXPECT_EQ(source_net.get_loads().front(), &original_load);
  EXPECT_EQ(original_load.get_net(), &source_net);
}

TEST(TopologyTest, SourceTrunkSingleRootSameLocationDirectConnectsWithoutInsertedObjects)
{
  icts::Pin source("clk_src", icts::PinType::kOut, icts::Point<int>(100, 200));
  icts::Pin root_input("A", icts::PinType::kIn, icts::Point<int>(100, 200));
  icts::Net source_net("clk_net");
  source_net.set_driver(&source);
  source.set_net(&source_net);

  const auto result = icts::topology::BuildSourceTrunkTree(icts::topology::SourceTrunkInput{.config = &CTSDM.getConfig(),
                                                                                            .design = &CTSDM.getDesign(),
                                                                                            .wrapper = &CTSDM.getWrapper(),
                                                                                            .fast_sta = &CTSDM.getFastSTA(),
                                                                                            .source_net = &source_net,
                                                                                            .clock_source = &source,
                                                                                            .root_inputs = {&root_input},
                                                                                            .object_name_prefix = {},
                                                                                            .characterization_library = nullptr,
                                                                                            .clock_period_ns = 0.0,
                                                                                            .clock_period_source = {},
                                                                                            .log_context = {}});

  EXPECT_TRUE(result.summary.success);
  EXPECT_EQ(result.summary.stage, icts::SourceTrunkStage::kSegment);
  EXPECT_EQ(result.output.inserted_insts.size(), 0U);
  EXPECT_EQ(result.output.inserted_nets.size(), 0U);
  EXPECT_EQ(source_net.get_driver(), &source);
  ASSERT_EQ(source_net.get_loads().size(), 1U);
  EXPECT_EQ(source_net.get_loads().front(), &root_input);
  EXPECT_EQ(root_input.get_net(), &source_net);
  EXPECT_EQ(source.get_net(), &source_net);
}

TEST(TopologyTest, ClockSourceDriveCapUsesRuntimeMaxCapForTopLevelIoPort)
{
  const ScopedConfigReset scoped_config_reset;
  CTSDM.getConfig().set_max_cap(0.23);

  icts::Pin source("clk_i", icts::PinType::kOut, icts::Point<int>(100, 200), nullptr, nullptr, true);

  const auto drive_cap_pf = CTSDM.getWrapper().queryClockSourceDriveCapLimit(CTSDM.getConfig(), &source);
  if (!drive_cap_pf.has_value()) {
    ADD_FAILURE() << "Configured top-level source drive capacitance is unavailable.";
    return;
  }
  EXPECT_DOUBLE_EQ(drive_cap_pf.value(), 0.23);
}

}  // namespace
}  // namespace icts_test

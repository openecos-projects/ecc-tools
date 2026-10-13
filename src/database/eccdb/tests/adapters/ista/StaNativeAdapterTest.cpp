// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory
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

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "DataManager.hpp"
#include "Logger.hpp"
#include "STAInterface.hpp"
#include "context/DatabaseContext.h"
#include "design/DesignStore.h"
#include "design/netlist/model/NetlistComponents.h"
#include "library/LibraryStore.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "service/connectivity/ConnectivityService.h"
#include "service/instance/InstanceService.h"
#include "SpefParser.hh"
#include "tech/TechStore.h"

namespace {

class TemporarySpef
{
 public:
  explicit TemporarySpef(const std::string& contents)
      : _path(std::filesystem::temp_directory_path() / "eccdb-ista-native-adapter.spef")
  {
    std::ofstream output(_path);
    if (!output) {
      throw std::runtime_error("failed to create native iSTA SPEF fixture");
    }
    output << contents;
  }

  ~TemporarySpef()
  {
    std::error_code error;
    std::filesystem::remove(_path, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return _path; }

 private:
  std::filesystem::path _path;
};

constexpr auto kEscapedSpef = R"SPEF(*SPEF "IEEE 1481-1998"
*DIVIDER /
*DELIMITER :
*T_UNIT 1 NS
*C_UNIT 1 PF
*R_UNIT 1 OHM
*NAME_MAP
*1 bus\[0\]
*2 u_src\[0\]
*3 u_sink\[0\]
*D_NET *1 0.03
*CONN
*I *2:Q O *D BUF
*I *3:A I *D BUF
*CAP
1 *2:Q 0.01
2 *1:1 0.02
*RES
1 *2:Q *1:1 10.0
2 *1:1 *3:A 20.0
*END
)SPEF";

constexpr auto kLiteralBackslashSpef = R"SPEF(*SPEF "IEEE 1481-1998"
*DIVIDER /
*DELIMITER :
*T_UNIT 1 NS
*C_UNIT 1 PF
*R_UNIT 1 OHM
*D_NET literal\\leaf 0.01
*CONN
*I literal\\driver:Q O *D BUF
*END
)SPEF";

constexpr auto kMixedTermSpef = R"SPEF(*SPEF "IEEE 1481-1998"
*DIVIDER /
*DELIMITER :
*T_UNIT 1 NS
*C_UNIT 1 PF
*R_UNIT 1 OHM
*D_NET mixed_terms 0.01
*CONN
*I u_plain:A I *D BUF
*I u_plain:B\[0\] I *D BUF
*END
)SPEF";

class StaNativeAdapterTest : public testing::Test
{
 protected:
  StaNativeAdapterTest()
      : library(technology.techRegistry()),
        design(technology.techRegistry(), library.libraryRegistry(), &technology.geometryRegistry()),
        context(technology, library, design)
  {
  }

  void SetUp() override
  {
    master = library.cellMasterStorage().createCellMaster({.name = "BUF", .type = eccdb::LibraryCellMasterType::kCore});
    input = library.masterTermStorage().createMasterTerm(
        master, {.name = "A", .direction = eccdb::LibraryMasterTermDirection::kInput, .use = eccdb::LibraryMasterTermUse::kSignal});
    escaped_input = library.masterTermStorage().createMasterTerm(
        master, {.name = R"(B\[0\])", .direction = eccdb::LibraryMasterTermDirection::kInput, .use = eccdb::LibraryMasterTermUse::kSignal});
    output = library.masterTermStorage().createMasterTerm(
        master, {.name = "Q", .direction = eccdb::LibraryMasterTermDirection::kOutput, .use = eccdb::LibraryMasterTermUse::kSignal});

    eccdb::InstanceService instances{context};
    source = instances.create({.name = R"(u_src\[0\])", .master = master});
    sink = instances.create({.name = R"(u_sink\[0\])", .master = master});

    eccdb::ConnectivityService connectivity{context};
    net = connectivity.createNet({.name = R"(bus\[0\])", .use = eccdb::DesignSignalUse::kSignal});
    connectivity.connect(design.netlistStorage().findInstanceTerm(source, output), net);
    connectivity.connect(design.netlistStorage().findInstanceTerm(sink, input), net);

    ista::DataManager::initInst();
    ista::STAInterface::getInst().setDesignSource(context);
  }

  void TearDown() override
  {
    auto& interface = ista::STAInterface::getInst();
    interface.setDesignSource(static_cast<eccdb::DatabaseContext*>(nullptr));
    interface.setTimingInputs({});
    ista::STAInterface::destroyInst();
    ista::DataManager::destroyInst();
    ista::Logger::destroyInst();
  }

  eccdb::TechStore technology;
  eccdb::LibraryStore library;
  eccdb::DesignStore design;
  eccdb::DatabaseContext context;
  eccdb::LibraryCellMasterId master;
  eccdb::LibraryMasterTermId input;
  eccdb::LibraryMasterTermId escaped_input;
  eccdb::LibraryMasterTermId output;
  eccdb::DesignInstanceId source;
  eccdb::DesignInstanceId sink;
  eccdb::DesignNetId net;
};

TEST_F(StaNativeAdapterTest, EscapedDefNamesMatchActualSpefConnections)
{
  TemporarySpef fixture{kEscapedSpef};
  spef::SpefReader reader;
  ASSERT_TRUE(reader.read(fixture.path().string()));
  reader.expandName();

  auto& interface = ista::STAInterface::getInst();
  interface.setTimingInputs({.spef_reader = &reader, .spef_path = fixture.path().string()});
  ASSERT_NO_THROW(interface.wrapDatabase());

  // The source database remains faithful to DEF spelling for a later export.
  EXPECT_EQ(design.netlistStorage().net(net).name, R"(bus\[0\])");
  EXPECT_EQ(design.netlistStorage().instance(source).name, R"(u_src\[0\])");

  auto& database = ista::DataManager::getInst().getDatabase();
  ASSERT_TRUE(database.get_instance_map().contains("u_src[0]"));
  ASSERT_TRUE(database.get_pin_map().contains("u_src[0]:Q"));
  ASSERT_TRUE(database.get_pin_map().contains("u_sink[0]:A"));
  ASSERT_TRUE(database.get_net_map().contains("bus[0]"));
  EXPECT_FALSE(database.get_net_map().contains(R"(bus\[0\])"));

  auto& timing_net = database.get_net_map().at("bus[0]");
  ASSERT_EQ(timing_net.get_pin_name_list().size(), 2u);
  EXPECT_EQ(timing_net.get_pin_name_list()[0], "u_src[0]:Q");
  EXPECT_EQ(timing_net.get_pin_name_list()[1], "u_sink[0]:A");

  auto& parasitics = database.get_parasitic_library().get_net_map();
  ASSERT_TRUE(parasitics.contains("bus[0]"));
  auto& parasitic_net = parasitics.at("bus[0]");
  EXPECT_TRUE(parasitic_net.get_node_map().contains("u_src[0]:Q"));
  EXPECT_TRUE(parasitic_net.get_node_map().contains("u_sink[0]:A"));
  ASSERT_EQ(parasitic_net.get_resistor_list().size(), 2u);
  EXPECT_EQ(parasitic_net.get_resistor_list()[0].get_source_node(), "u_src[0]:Q");
  EXPECT_EQ(parasitic_net.get_resistor_list()[1].get_sink_node(), "u_sink[0]:A");
}

TEST_F(StaNativeAdapterTest, SpefResolvedNetAliasCollisionFailsBeforeObjectsAreDropped)
{
  eccdb::ConnectivityService connectivity{context};
  static_cast<void>(connectivity.createNet({.name = "bus[0]", .use = eccdb::DesignSignalUse::kSignal}));

  TemporarySpef fixture{kEscapedSpef};
  spef::SpefReader reader;
  ASSERT_TRUE(reader.read(fixture.path().string()));
  reader.expandName();
  ista::STAInterface::getInst().setTimingInputs({.spef_reader = &reader, .spef_path = fixture.path().string()});

  EXPECT_THROW(ista::STAInterface::getInst().wrapDatabase(), std::runtime_error);
}

TEST_F(StaNativeAdapterTest, BlockTerminalCannotOverwriteAnInstancePinProjection)
{
  eccdb::ConnectivityService connectivity{context};
  static_cast<void>(connectivity.createBlockTerm({.name = "u_src[0]:Q", .use = eccdb::DesignSignalUse::kSignal}));

  TemporarySpef fixture{kEscapedSpef};
  spef::SpefReader reader;
  ASSERT_TRUE(reader.read(fixture.path().string()));
  reader.expandName();
  ista::STAInterface::getInst().setTimingInputs({.spef_reader = &reader, .spef_path = fixture.path().string()});

  EXPECT_THROW(ista::STAInterface::getInst().wrapDatabase(), std::runtime_error);
}

TEST_F(StaNativeAdapterTest, SemanticLiteralBackslashWinsOverEscapeAliasWhenSpefMatchesExactly)
{
  eccdb::ConnectivityService connectivity{context};
  eccdb::InstanceService instances{context};
  const auto literal_source = instances.create({.name = R"(literal\driver)", .master = master});
  const auto literal_net = connectivity.createNet({.name = R"(literal\leaf)", .use = eccdb::DesignSignalUse::kSignal});
  connectivity.connect(design.netlistStorage().findInstanceTerm(literal_source, output), literal_net);

  TemporarySpef fixture{kLiteralBackslashSpef};
  spef::SpefReader reader;
  ASSERT_TRUE(reader.read(fixture.path().string()));
  reader.expandName();
  ista::STAInterface::getInst().setTimingInputs({.spef_reader = &reader, .spef_path = fixture.path().string()});
  ASSERT_NO_THROW(ista::STAInterface::getInst().wrapDatabase());

  const auto& timing_nets = ista::DataManager::getInst().getDatabase().get_net_map();
  EXPECT_TRUE(timing_nets.contains(R"(literal\leaf)"));
  EXPECT_FALSE(timing_nets.contains("literalleaf"));
  const auto& timing_instances = ista::DataManager::getInst().getDatabase().get_instance_map();
  EXPECT_TRUE(timing_instances.contains(R"(literal\driver)"));
  EXPECT_FALSE(timing_instances.contains("literaldriver"));
  const auto& timing_pins = ista::DataManager::getInst().getDatabase().get_pin_map();
  EXPECT_TRUE(timing_pins.contains(R"(literal\driver:Q)"));
}

TEST_F(StaNativeAdapterTest, MixedTermSpellingsDoNotRejectAnUnescapedInstance)
{
  eccdb::InstanceService instances{context};
  static_cast<void>(instances.create({.name = "u_plain", .master = master}));

  TemporarySpef fixture{kMixedTermSpef};
  spef::SpefReader reader;
  ASSERT_TRUE(reader.read(fixture.path().string()));
  reader.expandName();
  ista::STAInterface::getInst().setTimingInputs({.spef_reader = &reader, .spef_path = fixture.path().string()});
  ASSERT_NO_THROW(ista::STAInterface::getInst().wrapDatabase());

  auto& database = ista::DataManager::getInst().getDatabase();
  EXPECT_TRUE(database.get_instance_map().contains("u_plain"));
  EXPECT_TRUE(database.get_pin_map().contains("u_plain:A"));
  EXPECT_TRUE(database.get_pin_map().contains("u_plain:B[0]"));
}

TEST_F(StaNativeAdapterTest, MissingSpefPreservesRawDefSpellings)
{
  ASSERT_NO_THROW(ista::STAInterface::getInst().wrapDatabase());

  auto& database = ista::DataManager::getInst().getDatabase();
  EXPECT_TRUE(database.get_instance_map().contains(R"(u_src\[0\])"));
  EXPECT_TRUE(database.get_pin_map().contains(R"(u_src\[0\]:Q)"));
  EXPECT_TRUE(database.get_net_map().contains(R"(bus\[0\])"));
  EXPECT_FALSE(database.get_net_map().contains("bus[0]"));
}

}  // namespace

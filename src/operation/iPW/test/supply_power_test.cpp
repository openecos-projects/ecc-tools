// SPDX-License-Identifier: MulanPSL-2.0
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "DataManager.hpp"
#include "Lib.hh"
#include "LibParserCpp.hh"
#include "LibertySupplies.hpp"
#include "Logger.hpp"
#include "PowerAnalyzer.hpp"
#include "PowerReporter.hpp"
#include "PWInterface.hpp"
#include "SupplyPower.hpp"
#include "Utility.hpp"

namespace {

class SupplyPowerTest : public testing::Test
{
 protected:
  std::filesystem::path directory;
  std::unique_ptr<idb::LibLibrary> library;

  void SetUp() override
  {
    char pattern[] = "/tmp/ipw-supply-XXXXXX";
    directory = mkdtemp(pattern);
    const auto path = directory / "supply.lib";
    std::ofstream(path) << R"(library(test) {
      time_unit : "1ns"; capacitive_load_unit(1, pf);
      voltage_unit : "1V"; leakage_power_unit : "1mW"; nom_voltage : 3.3;
      voltage_map(CORE, 1.2); voltage_map(IO, 3.3); voltage_map(GND, 0);
      cell(PAD) {
        area : 1;
        pg_pin(VC) { pg_type : primary_power; voltage_name : CORE; }
        pg_pin(VI) { pg_type : primary_power; voltage_name : IO; }
        pg_pin(GC) { pg_type : primary_ground; voltage_name : GND; }
        pg_pin(GI) { pg_type : primary_ground; voltage_name : GND; }
        pin(C) { direction : input; related_power_pin : VC; related_ground_pin : GC; }
        pin(PAD) { direction : input; related_power_pin : VI; related_ground_pin : GI; }
        leakage_power() { related_pg_pin : VC; value : 3.0; }
        leakage_power() { related_pg_pin : VI; value : 7.0; }
      }
    })";
    idb::LibertyReader reader(path.c_str());
    ASSERT_TRUE(reader.readLib());
    ASSERT_TRUE(reader.linkLib());
    library = reader.takeLib();
    ipw::DataManager::initInst();
    ipw::PowerAnalyzer::initInst();
    ipw::PowerReporter::initInst();
    ipw::Utility::initInst();
  }

  void TearDown() override
  {
    ipw::PowerReporter::destroyInst();
    ipw::PowerAnalyzer::destroyInst();
    ipw::DataManager::destroyInst();
    ipw::Utility::destroyInst();
    std::filesystem::remove_all(directory);
  }
};

TEST_F(SupplyPowerTest, LibertyRailsDriveVoltageAndPerRailLeakageExport)
{
  auto* raw = library->findCell("PAD");
  ASSERT_NE(raw, nullptr);
  ASSERT_EQ(raw->get_cell_ports().size(), 2U);  // PG pins never enter signal topology.
  ipw::TimingCell cell;
  ipw::wrapLibertySupplies(cell, *raw);
  EXPECT_DOUBLE_EQ(cell.get_power_supplies().at("VC").voltage, 1.2);
  EXPECT_EQ(cell.get_power_supplies().at("VI").ground_pin, "GI");
  cell.get_port_map()["C"].set_related_power_pin("VC");
  EXPECT_EQ(ipw::resolvePowerSupply(cell, "", "C"), "VC");
  EXPECT_DOUBLE_EQ(ipw::supplyVoltage(cell, "VC"), 1.2);
  EXPECT_THROW(ipw::resolvePowerSupply(cell, "", "unknown"), std::runtime_error);

  PWI.wrapTimingCell(raw);
  auto& database = PWDM.getDatabase();
  auto& instance = database.get_instance_map()["io"];
  instance.set_instance_name("io");
  instance.set_cell_name("PAD");
  database.resizePowerWorkspace(database.get_workspace());
  PWPA.analyze();
  auto& result = database.get_instance_power_list().at(0);
  ASSERT_EQ(result.get_supply_power().size(), 2U);
  EXPECT_DOUBLE_EQ(result.get_supply_power().at("VC").power.get_leakage_power(), 0.003);
  EXPECT_DOUBLE_EQ(result.get_supply_power().at("VI").power.get_leakage_power(), 0.007);
  EXPECT_DOUBLE_EQ(result.get_power_value().get_total_power(), 0.01);
  PWDM.getConfig().pr_temp_directory_path = directory.string() + "/";
  PWPR.report();
  std::ifstream input(directory / "instance_power.tsv");
  std::string line;
  std::getline(input, line);
  EXPECT_EQ(line, "# iEMIR_INSTANCE_POWER_V2");
  std::getline(input, line);
  EXPECT_TRUE(line.ends_with("\tpower_pin\tground_pin"));
  double sum = 0.0;
  std::set<std::string> rails;
  while (std::getline(input, line)) {
    std::istringstream row(line);
    std::string name, pin, ground;
    double voltage, internal, switching, leakage, total, current;
    ASSERT_TRUE(
        static_cast<bool>(
            row >> name >> voltage >> internal >> switching >> leakage >> total >> current >> pin >> ground
        )
    );
    EXPECT_NEAR(voltage * current, total, 1E-15);
    sum += total;
    rails.insert(pin + "/" + ground);
  }
  EXPECT_EQ(rails, (std::set<std::string>{"VC/GC", "VI/GI"}));
  EXPECT_DOUBLE_EQ(sum, result.get_power_value().get_total_power());
  database.get_peak_power_summary().set_is_enabled(true);
  EXPECT_THROW(PWPR.report(), std::runtime_error);
}

TEST_F(SupplyPowerTest, RejectMissingVoltageAndPreserveUnknownGround)
{
  auto* raw = library->findCell("PAD");
  ipw::TimingCell cell;
  library->get_voltage_map().erase("CORE");
  EXPECT_THROW(ipw::wrapLibertySupplies(cell, *raw), std::runtime_error);
  library->get_voltage_map()["CORE"] = 1.2;
  raw->get_cell_ports().front()->set_related_ground_pin("");
  ipw::wrapLibertySupplies(cell, *raw);
  EXPECT_TRUE(cell.get_power_supplies().at("VC").ground_pin.empty());
}

TEST_F(SupplyPowerTest, VoltageUnitsAndPortMetadataSurviveMovesAndBusInheritance)
{
  library->set_voltage_unit_name("1mV");
  library->get_voltage_map()["CORE"] = 1200;
  library->get_voltage_map()["IO"] = 3300;
  ipw::TimingCell cell;
  ipw::wrapLibertySupplies(cell, *library->findCell("PAD"));
  EXPECT_DOUBLE_EQ(cell.get_power_supplies().at("VC").voltage, 1.2);
  idb::LibPort bus("B"), bit("B[0]");
  bus.set_related_power_pin("VC");
  bus.set_related_ground_pin("GC");
  bit.inheritBusAttributes(bus);
  idb::LibPort moved(std::move(bit));
  EXPECT_EQ(moved.get_related_power_pin(), "VC");
  EXPECT_EQ(moved.get_related_ground_pin(), "GC");
  idb::LibLibrary legacy("legacy");
  legacy.set_nom_voltage(1.0);
  idb::LibCell legacy_cell("gate", &legacy);
  ipw::TimingCell single;
  ipw::wrapLibertySupplies(single, legacy_cell);
  EXPECT_TRUE(single.get_power_supplies().empty());
  EXPECT_DOUBLE_EQ(ipw::supplyVoltage(single, ipw::resolvePowerSupply(single, "")), 1.0);
}

}  // namespace

int main(int argc, char** argv)
{
  ipw::Logger::initInst();
  testing::InitGoogleTest(&argc, argv);
  const int status = RUN_ALL_TESTS();
  ipw::PWInterface::destroyInst();
  ipw::Logger::destroyInst();
  return status;
}

// SPDX-License-Identifier: MulanPSL-2.0
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "DeviceLayerPins.hpp"
#include "InstancePowerReader.hpp"
#include "Logger.hpp"
#include "SupplyCurrent.hpp"

namespace {
using namespace iemir;

PowerGraph graph(const std::string& name, PowerNetType type, const std::string& pin)
{
  PowerGraph result;
  result.set_net_name(name);
  result.set_net_type(type);
  result.get_instance_node_id_list_map()[0] = {0, 1};
  result.get_instance_pin_node_id_list_map()[{0, pin}] = {0, 1};
  result.get_instance_pin_node_weight_map()[{0, pin}] = {{0, 1.0}, {1, 3.0}};
  return result;
}

TEST(SupplyCurrent, MultiVoltagePadSharesCoreRailWithoutDuplicatingCurrent)
{
  std::map<uint64_t, InstancePower> powers;
  powers[0].get_supply_currents() = {{"VC", {"GC", 1.2, 0.002}}, {"VI", {"GI", 3.3, 0.003}}};
  powers[0].set_average_current(0.005);
  powers[1].set_voltage(1.2);  // Ordinary V1 core cell shares VC, not VI.
  auto core = graph("core", PowerNetType::kPower, "VC");
  core.get_instance_node_id_list_map()[1] = {2};
  auto io = graph("io", PowerNetType::kPower, "VI");
  auto core_ground = graph("core_gnd", PowerNetType::kGround, "GC");
  auto io_ground = graph("io_gnd", PowerNetType::kGround, "GI");
  EXPECT_DOUBLE_EQ(graphSourceVoltage(core, powers), 1.2);
  EXPECT_DOUBLE_EQ(graphSourceVoltage(io, powers), 3.3);
  EXPECT_DOUBLE_EQ(graphSourceVoltage(core_ground, powers), 0.0);
  IAModel core_model, io_model, core_return, io_return;
  buildSupplyCurrent(core, 0, powers[0], core_model);
  buildSupplyCurrent(io, 0, powers[0], io_model);
  buildSupplyCurrent(core_ground, 0, powers[0], core_return);
  buildSupplyCurrent(io_ground, 0, powers[0], io_return);
  EXPECT_EQ(core_model.get_node_current_map(), (std::map<std::size_t, double>{{0, -0.0005}, {1, -0.0015}}));
  EXPECT_DOUBLE_EQ(core_model.get_pin_mapped_current() + io_model.get_pin_mapped_current(), 0.005);
  EXPECT_DOUBLE_EQ(core_return.get_pin_mapped_current() + io_return.get_pin_mapped_current(), 0.005);
  for (auto node : {0U, 1U}) {
    EXPECT_DOUBLE_EQ(core_model.get_node_current_map().at(node), -core_return.get_node_current_map().at(node));
    EXPECT_DOUBLE_EQ(io_model.get_node_current_map().at(node), -io_return.get_node_current_map().at(node));
  }
  EXPECT_DOUBLE_EQ(core_model.get_generic_current(), 0.0);
  powers[1].set_voltage(3.3);
  EXPECT_THROW(graphSourceVoltage(core, powers), std::runtime_error);  // Real short/mismatched rail still fails.
  auto absent = graph("unknown", PowerNetType::kPower, "other");
  EXPECT_THROW(graphSourceVoltage(absent, powers), std::runtime_error);
}

class SupplyFileTest : public testing::Test
{
 protected:
  std::filesystem::path directory;
  const std::string header = "instance_name\tvoltage_v\tinternal_power_w\tswitching_power_w\tleakage_power_w\ttotal_power_w\taverage_current_a";
  void SetUp() override
  {
    char pattern[] = "/tmp/iemir-supply-XXXXXX";
    directory = mkdtemp(pattern);
  }
  void TearDown() override { std::filesystem::remove_all(directory); }
  auto read(const std::string& data, bool v2 = true)
  {
    const auto path = directory / "power.tsv";
    std::ofstream(path) << header << (v2 ? "\tpower_pin\tground_pin\n" : "\n") << data;
    return InstancePowerReader::read(path.string());
  }
};

TEST_F(SupplyFileTest, ReadsPerSupplyAndLegacyRowsAndChecksConservation)
{
  const std::string core = "IO\t1.2\t0\t0\t0.0024\t0.0024\t0.002\tVC\tGC\n";
  const std::string io = "IO\t3.3\t0\t0\t0.0099\t0.0099\t0.003\tVI\tGI\n";
  auto records = read(core + io + "gate\t1.2\t0\t0\t0.0012\t0.0012\t0.001\t\t\n");
  ASSERT_EQ(records.size(), 3U);
  EXPECT_EQ(records[0].power_pin, "VC");
  EXPECT_EQ(records[1].ground_pin, "GI");
  EXPECT_TRUE(records[2].power_pin.empty());
  EXPECT_NEAR(records[0].total_power + records[1].total_power, 0.0123, 1E-15);
  EXPECT_THROW(read(core + core), std::runtime_error);
  EXPECT_THROW(read(core + "IO\t1.2\t0\t0\t0\t0\t0\t\t\n"), std::runtime_error);
  EXPECT_THROW(read("IO\t1.2\t0\t0\t0.0024\t0.0024\t0.002\tVC\t\n"), std::runtime_error);
  EXPECT_THROW(read("IO\t1.2\t0\t0\t0.0024\t0.0024\t0.003\tVC\tGC\n"), std::runtime_error);
  EXPECT_THROW(read("IO\tnan\t0\t0\t0\t0\t0\tVC\tGC\n"), std::runtime_error);
  auto legacy = read("gate\t1.2\t0\t0\t0.0012\t0.0012\t0.001\n", false);
  EXPECT_TRUE(legacy.front().power_pin.empty());
  EXPECT_DOUBLE_EQ(legacy.front().average_current, 0.001);
}

TEST_F(SupplyFileTest, ExplicitReturnsFillOnlyMissingLibertyMetadata)
{
  const auto path = directory / "power.tsv";
  std::ofstream(path) << header << "\tpower_pin\tground_pin\n"
                      << "pad\t1.2\t0\t0\t0.0024\t0.0024\t0.002\tVC\t\n"
                      << "io\t1.2\t0\t0\t0.0024\t0.0024\t0.002\tVC\tlocal_ground\n";
  auto rows = InstancePowerReader::read(path.string(), {{"VC", "GC"}});
  ASSERT_EQ(rows.size(), 2U);
  EXPECT_EQ(rows[0].ground_pin, "GC");
  EXPECT_EQ(rows[1].ground_pin, "local_ground");
  EXPECT_THROW(InstancePowerReader::read(path.string(), {{"VC", ""}}), std::runtime_error);
}

TEST_F(SupplyFileTest, UnselectedRailsAreReportedButMissingDefContactsFail)
{
  Database database;
  database.get_instance_power_map()[0].get_supply_currents() = {{"VC", {"GC", 1.2, 0.002}}, {"VI", {"GI", 3.3, 0.003}}};
  for (const auto& pin_name : {"VC", "GC", "VI", "GI"}) {
    const auto type = pin_name[0] == 'V' ? PowerNetType::kPower : PowerNetType::kGround;
    auto& net = database.get_power_net_map()[pin_name];
    net.set_type(type);
    PowerPin pin;
    pin.set_pin_name(std::string("io:") + pin_name);
    net.get_pin_list().push_back(pin);
    net.get_pin_has_geometry()[{0, pin_name}] = true;
    if (pin_name[1] == 'C') {
      database.get_power_graph_map()[pin_name] = graph(pin_name, type, pin_name);
    }
  }
  const auto report = directory / "coverage.tsv";
  EXPECT_NO_THROW(reportSupplyCoverage(database, report.string()));
  std::ifstream input(report);
  std::string contents((std::istreambuf_iterator<char>(input)), {});
  EXPECT_NE(contents.find("\tVI\tGI\t0\t0\n"), std::string::npos);
  database.get_power_net_map().erase("VI");
  EXPECT_THROW(reportSupplyCoverage(database, report.string()), std::runtime_error);
  EXPECT_THROW(reportSupplyCoverage(database, report.string(), true), std::runtime_error);
}

TEST_F(SupplyFileTest, ExploratoryModeReportsUnmappedPinsWithoutInventingCurrentContacts)
{
  for (const auto& missing : {"VC", "GC"}) {
    SCOPED_TRACE(missing);
    Database database;
    auto& power = database.get_instance_power_map()[0];
    power.get_supply_currents() = {{"VC", {"GC", 1.2, 0.002}}};
    for (const auto& pin : {"VC", "GC"}) {
      const auto type = pin[0] == 'V' ? PowerNetType::kPower : PowerNetType::kGround;
      auto& net = database.get_power_net_map()[pin];
      net.set_type(type);
      net.get_pin_has_geometry()[{0, pin}] = true;
      database.get_power_graph_map()[pin] = graph(pin, type, pin);
    }
    auto& unmapped_graph = database.get_power_graph_map().at(missing);
    unmapped_graph.get_instance_pin_node_id_list_map().clear();
    const auto report = directory / "supply_coverage.tsv";
    EXPECT_THROW(reportSupplyCoverage(database, report.string()), std::runtime_error);
    EXPECT_NO_THROW(reportSupplyCoverage(database, report.string(), true));

    std::ifstream input(directory / "supply_coverage.rpt");
    std::map<std::string, double> summary;
    for (std::string line; std::getline(input, line);) {
      if (line.starts_with('#')) continue;
      std::istringstream row(line);
      std::string key;
      double value;
      ASSERT_TRUE(row >> key >> value);
      summary[key] = value;
    }
    const bool missing_power = missing[0] == 'V';
    EXPECT_EQ(summary, (std::map<std::string, double>{
                           {"allow_unmapped_pg_pins", 1},
                           {"total_explicit_supply_power_w", 1.2 * 0.002},
                           {"power_side_analyzed_w", missing_power ? 0.0 : 1.2 * 0.002},
                           {"incomplete_supply_power_w", 1.2 * 0.002},
                           {"unmapped_power_current_a", missing_power ? 0.002 : 0.0},
                           {"unmapped_ground_current_a", missing_power ? 0.0 : 0.002},
                           {"supplies_without_both_contacts", 1}}));
    std::ifstream coverage(report);
    std::string rows((std::istreambuf_iterator<char>(coverage)), {});
    EXPECT_NE(rows.find(missing_power ? "\tVC\tGC\t0\t1\n" : "\tVC\tGC\t1\t0\n"), std::string::npos);
    EXPECT_TRUE(unmapped_graph.get_instance_pin_node_id_list_map().empty());
    IAModel omitted, retained;
    buildSupplyCurrent(unmapped_graph, 0, power, omitted);
    buildSupplyCurrent(database.get_power_graph_map().at(missing_power ? "GC" : "VC"), 0, power, retained);
    EXPECT_TRUE(omitted.get_node_current_map().empty());
    EXPECT_DOUBLE_EQ(omitted.get_pin_mapped_current(), 0.0);
    EXPECT_DOUBLE_EQ(retained.get_pin_mapped_current(), 0.002);
    EXPECT_DOUBLE_EQ(power.get_supply_currents().at("VC").current, 0.002);
  }
}

TEST(SupplyCurrent, DeviceLayerPinUsesOnlyDefConnectedSameNetContacts)
{
  auto core = graph("core", PowerNetType::kPower, "VC");
  PowerNet net;
  net.get_pin_has_geometry() = {{{0, "VC"}, true}, {{0, "BIAS"}, false}, {{0, "disconnected_metal"}, true}, {{1, "isolated"}, false}};
  connectDeviceLayerPins(core, net);
  EXPECT_EQ(core.get_instance_pin_node_id_list_map().at({0, "BIAS"}), (std::vector<std::size_t>{0, 1}));
  EXPECT_FALSE(core.get_instance_pin_node_id_list_map().contains({0, "disconnected_metal"}));
  EXPECT_FALSE(core.get_instance_pin_node_id_list_map().contains({1, "isolated"}));
  EXPECT_FALSE(core.get_instance_pin_node_id_list_map().contains({0, "other_net"}));
}

}  // namespace

int main(int argc, char** argv)
{
  iemir::Logger::initInst();
  testing::InitGoogleTest(&argc, argv);
  const int status = RUN_ALL_TESTS();
  iemir::Logger::destroyInst();
  return status;
}

// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

#include "gtest/gtest.h"
#include "liberty/Lib.hh"
#include "log/Log.hh"
#include "netlist/Instance.hh"
#include "netlist/Net.hh"
#include "netlist/Netlist.hh"
#include "netlist/Port.hh"
#include "sta/Sta.hh"
#include "sta/StaClusterTiming.hh"

using namespace ista;

namespace {

namespace fs = std::filesystem;

LibCell* addCell(
    LibLibrary& lib, const char* cell_name,
    std::initializer_list<std::pair<const char*, LibPort::LibertyPortType>>
        ports) {
  auto lib_cell = std::make_unique<LibCell>(cell_name, &lib);
  auto* lib_cell_ptr = lib_cell.get();

  for (const auto& [port_name, port_type] : ports) {
    auto lib_port = std::make_unique<LibPort>(port_name);
    lib_port->set_port_type(port_type);
    lib_port->set_ower_cell(lib_cell_ptr);
    lib_cell_ptr->addLibertyPort(std::move(lib_port));
  }

  lib.addLibertyCell(std::move(lib_cell));
  return lib.findCell(cell_name);
}

std::size_t countPortsByName(Netlist& netlist, const char* port_name) {
  auto& ports = netlist.get_ports();
  return std::count_if(ports.begin(), ports.end(),
                       [port_name](const Port& port) {
                         return std::string(port.get_name()) == port_name;
                       });
}

std::string readText(const fs::path& file_path) {
  std::ifstream input(file_path);
  return std::string((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
}

class RestoredClusterTimingTest : public testing::Test {
 protected:
  void SetUp() override {
    if (!Log::isInit()) {
      char config[] = "test";
      char* argv[] = {config};
      Log::init(argv);
    }
    Sta::destroySta();
  }

  void TearDown() override {
    Sta::destroySta();
    if (Log::isInit()) {
      Log::end();
    }
  }
};

TEST_F(RestoredClusterTimingTest,
       subnetlist_reuses_shared_top_ports_and_keeps_boundary_ports_connected) {
  LibLibrary lib("cluster_restore_test");
  auto* boundary_cell = addCell(
      lib, "boundary_cell",
      {{"A", LibPort::LibertyPortType::kInput},
       {"Y", LibPort::LibertyPortType::kOutput}});
  auto* shared_cell = addCell(
      lib, "shared_cell",
      {{"C", LibPort::LibertyPortType::kInput},
       {"D", LibPort::LibertyPortType::kInput},
       {"Z", LibPort::LibertyPortType::kOutput}});
  auto* sink_cell = addCell(
      lib, "sink_cell", {{"I", LibPort::LibertyPortType::kInput}});

  auto* ista = Sta::getOrCreateSta();
  ASSERT_NE(ista, nullptr);
  auto* netlist = ista->get_netlist();
  ASSERT_NE(netlist, nullptr);
  netlist->set_name("top");

  auto& bound_in = netlist->addPort(Port("BOUND_IN", PortDir::kIn));
  auto& shared_in = netlist->addPort(Port("SHARED_IN", PortDir::kIn));
  auto& top_out = netlist->addPort(Port("TOP_OUT", PortDir::kOut));

  auto& bound_in_net = netlist->addNet(Net("bound_in_net"));
  auto& shared_in_net = netlist->addNet(Net("shared_in_net"));
  auto& boundary_net = netlist->addNet(Net("boundary_net"));
  auto& top_out_net = netlist->addNet(Net("top_out_net"));
  bound_in_net.addPinPort(&bound_in);
  shared_in_net.addPinPort(&shared_in);
  top_out_net.addPinPort(&top_out);

  auto& boundary_inst = netlist->addInstance(Instance("u1", boundary_cell));
  auto* boundary_in_pin =
      boundary_inst.addPin("A", boundary_cell->get_cell_port_or_port_bus("A"));
  auto* boundary_out_pin =
      boundary_inst.addPin("Y", boundary_cell->get_cell_port_or_port_bus("Y"));
  bound_in_net.addPinPort(boundary_in_pin);
  boundary_net.addPinPort(boundary_out_pin);

  auto& shared_inst = netlist->addInstance(Instance("u2", shared_cell));
  auto* shared_in_pin_c =
      shared_inst.addPin("C", shared_cell->get_cell_port_or_port_bus("C"));
  auto* shared_in_pin_d =
      shared_inst.addPin("D", shared_cell->get_cell_port_or_port_bus("D"));
  auto* shared_out_pin =
      shared_inst.addPin("Z", shared_cell->get_cell_port_or_port_bus("Z"));
  shared_in_net.addPinPort(shared_in_pin_c);
  shared_in_net.addPinPort(shared_in_pin_d);
  top_out_net.addPinPort(shared_out_pin);

  auto& outside_sink = netlist->addInstance(Instance("u3", sink_cell));
  auto* outside_sink_pin =
      outside_sink.addPin("I", sink_cell->get_cell_port_or_port_bus("I"));
  boundary_net.addPinPort(outside_sink_pin);

  std::vector<std::set<std::string>> clusters = {{"u1", "u2"}};
  StaClusterTiming sta_cluster_timing(std::move(clusters));
  sta_cluster_timing.addHierSubNetlist();

  auto hier_sub_netlists = netlist->get_hier_sub_netlists();
  ASSERT_EQ(hier_sub_netlists.size(), 1U);
  auto* subnetlist = hier_sub_netlists.front();
  ASSERT_NE(subnetlist, nullptr);

  auto* copied_boundary_port = subnetlist->findPort("BOUND_IN");
  auto* copied_boundary_net = subnetlist->findNet("bound_in_net");
  ASSERT_NE(copied_boundary_port, nullptr);
  ASSERT_NE(copied_boundary_net, nullptr);
  EXPECT_TRUE(copied_boundary_net->isNetPinPort(copied_boundary_port))
      << "expected copied boundary port to stay electrically connected in the "
         "subnetlist";

  auto* copied_shared_port = subnetlist->findPort("SHARED_IN");
  auto* copied_shared_net = subnetlist->findNet("shared_in_net");
  ASSERT_NE(copied_shared_port, nullptr);
  ASSERT_NE(copied_shared_net, nullptr);
  EXPECT_TRUE(copied_shared_net->isNetPinPort(copied_shared_port));
  EXPECT_EQ(countPortsByName(*subnetlist, "SHARED_IN"), 1U)
      << "expected shared top-level ports to be emitted once per subnetlist";
}

TEST_F(RestoredClusterTimingTest,
       build_subnetlist_to_inst_reuses_shared_top_ports_across_clusters) {
  auto lib = std::make_unique<LibLibrary>("cluster_rebuild_shared_port_test");
  addCell(*lib, "cluster1",
          {{"SHARED_CLK", LibPort::LibertyPortType::kInput},
           {"OUT_A", LibPort::LibertyPortType::kOutput}});
  addCell(*lib, "cluster2",
          {{"SHARED_CLK", LibPort::LibertyPortType::kInput},
           {"OUT_B", LibPort::LibertyPortType::kOutput}});

  auto* ista = Sta::getOrCreateSta();
  ASSERT_NE(ista, nullptr);
  ista->addLib(std::move(lib));
  auto* netlist = ista->get_netlist();
  ASSERT_NE(netlist, nullptr);
  netlist->set_name("top");

  auto* subnetlist1 = new Netlist();
  subnetlist1->set_name("cluster1");
  subnetlist1->addPort(Port("SHARED_CLK", PortDir::kIn));
  subnetlist1->addPort(Port("OUT_A", PortDir::kOut));

  auto* subnetlist2 = new Netlist();
  subnetlist2->set_name("cluster2");
  subnetlist2->addPort(Port("SHARED_CLK", PortDir::kIn));
  subnetlist2->addPort(Port("OUT_B", PortDir::kOut));

  netlist->set_hier_sub_netlists({subnetlist1, subnetlist2});

  StaClusterTiming sta_cluster_timing({});
  sta_cluster_timing.buildSubnetlistToInst();

  EXPECT_EQ(countPortsByName(*netlist, "SHARED_CLK"), 1U)
      << "expected rebuilt top netlist to reuse the shared top-level port once";
}

TEST_F(RestoredClusterTimingTest,
       build_subnetlist_to_inst_preserves_original_boundary_net_names) {
  auto lib = std::make_unique<LibLibrary>("cluster_rebuild_boundary_net_test");
  addCell(*lib, "cluster1",
          {{"BOUND_IN", LibPort::LibertyPortType::kInput},
           {"TOP_OUT", LibPort::LibertyPortType::kOutput}});

  auto* ista = Sta::getOrCreateSta();
  ASSERT_NE(ista, nullptr);
  ista->addLib(std::move(lib));

  auto* netlist = ista->get_netlist();
  ASSERT_NE(netlist, nullptr);
  netlist->set_name("top");

  auto* subnetlist = new Netlist();
  subnetlist->set_name("cluster1");
  auto& bound_in = subnetlist->addPort(Port("BOUND_IN", PortDir::kIn));
  auto& top_out = subnetlist->addPort(Port("TOP_OUT", PortDir::kOut));
  auto& bound_in_net = subnetlist->addNet(Net("bound_in_net"));
  auto& top_out_net = subnetlist->addNet(Net("top_out_net"));
  bound_in_net.addPinPort(&bound_in);
  top_out_net.addPinPort(&top_out);

  netlist->set_hier_sub_netlists({subnetlist});

  StaClusterTiming sta_cluster_timing({});
  sta_cluster_timing.buildSubnetlistToInst();

  auto* rebuilt_bound_in_net = netlist->findNet("bound_in_net");
  ASSERT_NE(rebuilt_bound_in_net, nullptr);
  EXPECT_EQ(netlist->findNet("BOUND_IN"), nullptr)
      << "expected rebuild to reuse the copied port's original net name "
         "instead of inventing a new net named after the port";

  auto* rebuilt_bound_in_port = netlist->findPort("BOUND_IN");
  ASSERT_NE(rebuilt_bound_in_port, nullptr);
  EXPECT_TRUE(rebuilt_bound_in_net->isNetPinPort(rebuilt_bound_in_port));

  auto* rebuilt_cluster = netlist->findInstance("cluster1");
  ASSERT_NE(rebuilt_cluster, nullptr);
  auto rebuilt_inst_pin = rebuilt_cluster->getPin("BOUND_IN");
  ASSERT_TRUE(rebuilt_inst_pin.has_value());
  ASSERT_NE(*rebuilt_inst_pin, nullptr);
  EXPECT_TRUE(rebuilt_bound_in_net->isNetPinPort(*rebuilt_inst_pin))
      << "expected rebuilt ETM instance pins to reconnect to the original "
         "boundary net name";
}

TEST_F(RestoredClusterTimingTest,
       build_subnetlist_to_inst_preserves_copied_top_port_metadata) {
  auto lib = std::make_unique<LibLibrary>("cluster_rebuild_port_metadata_test");
  addCell(*lib, "cluster1",
          {{"BUS_IN[0]", LibPort::LibertyPortType::kInput}});

  auto* ista = Sta::getOrCreateSta();
  ASSERT_NE(ista, nullptr);
  ista->addLib(std::move(lib));

  auto* netlist = ista->get_netlist();
  ASSERT_NE(netlist, nullptr);
  netlist->set_name("top");

  auto* subnetlist = new Netlist();
  subnetlist->set_name("cluster1");
  auto& bus_port = subnetlist->addPort(Port("BUS_IN[0]", PortDir::kIn));
  bus_port.set_cap(AnalysisMode::kMax, TransType::kRise, 3.5);
  bus_port.set_cap(AnalysisMode::kMin, TransType::kFall, 1.25);
  PortBus bus("BUS_IN", 0, 0, 1, PortDir::kIn);
  bus.addPort(0, &bus_port);
  subnetlist->addPortBus(std::move(bus));
  auto& bus_net = subnetlist->addNet(Net("bus_in_net"));
  bus_net.addPinPort(&bus_port);

  netlist->set_hier_sub_netlists({subnetlist});

  StaClusterTiming sta_cluster_timing({});
  sta_cluster_timing.buildSubnetlistToInst();

  auto* rebuilt_port = netlist->findPort("BUS_IN[0]");
  ASSERT_NE(rebuilt_port, nullptr);
  EXPECT_DOUBLE_EQ(rebuilt_port->cap(AnalysisMode::kMax, TransType::kRise),
                   3.5);
  EXPECT_DOUBLE_EQ(rebuilt_port->cap(AnalysisMode::kMin, TransType::kFall),
                   1.25);
  ASSERT_NE(rebuilt_port->get_port_bus(), nullptr)
      << "expected rebuild to preserve copied PortBus ownership";
  EXPECT_EQ(std::string(rebuilt_port->get_port_bus()->get_name()), "BUS_IN");
  ASSERT_NE(netlist->findPortBus("BUS_IN"), nullptr);
}

TEST_F(RestoredClusterTimingTest,
       build_subnetlist_to_inst_clears_stale_rc_net_cache_before_reset) {
  auto lib = std::make_unique<LibLibrary>("cluster_rebuild_rc_reset_test");
  addCell(*lib, "cluster1",
          {{"BOUND_IN", LibPort::LibertyPortType::kInput}});

  auto* ista = Sta::getOrCreateSta();
  ASSERT_NE(ista, nullptr);
  ista->addLib(std::move(lib));

  auto* netlist = ista->get_netlist();
  ASSERT_NE(netlist, nullptr);
  netlist->set_name("top");

  auto& stale_net = netlist->addNet(Net("stale_rc_net"));
  auto* stale_net_ptr = &stale_net;
  ista->addRcNet(stale_net_ptr, std::make_unique<RcNet>(stale_net_ptr));
  ASSERT_NE(ista->getRcNet(stale_net_ptr), nullptr);

  auto* subnetlist = new Netlist();
  subnetlist->set_name("cluster1");
  auto& bound_in = subnetlist->addPort(Port("BOUND_IN", PortDir::kIn));
  auto& bound_in_net = subnetlist->addNet(Net("bound_in_net"));
  bound_in_net.addPinPort(&bound_in);
  netlist->set_hier_sub_netlists({subnetlist});

  StaClusterTiming sta_cluster_timing({});
  sta_cluster_timing.buildSubnetlistToInst();

  EXPECT_EQ(ista->getRcNet(stale_net_ptr), nullptr)
      << "expected buildSubnetlistToInst() to clear stale RcNet mappings "
         "before destroying the old design netlist";
}

TEST_F(RestoredClusterTimingTest,
       hier_subnetlist_cluster_ports_do_not_dereference_null_instances) {
  auto run_cluster_port_case = []() {
    LibLibrary lib("cluster_port_guard_test");
    auto* cluster_cell = addCell(
        lib, "cluster_cell",
        {{"A", LibPort::LibertyPortType::kInput},
         {"Y", LibPort::LibertyPortType::kOutput}});

    auto* ista = Sta::getOrCreateSta();
    auto* netlist = ista->get_netlist();
    netlist->set_name("top");

    auto& shared_a = netlist->addPort(Port("SHARED_A", PortDir::kIn));
    auto& shared_b = netlist->addPort(Port("SHARED_B", PortDir::kOut));
    auto& shared_net = netlist->addNet(Net("shared_net"));
    shared_net.addPinPort(&shared_a);
    shared_net.addPinPort(&shared_b);

    auto& inst_input_net = netlist->addNet(Net("inst_input_net"));
    auto& cluster_inst =
        netlist->addInstance(Instance("u_cluster", cluster_cell));
    auto* cluster_pin =
        cluster_inst.addPin("A", cluster_cell->get_cell_port_or_port_bus("A"));
    inst_input_net.addPinPort(cluster_pin);

    std::vector<std::set<std::string>> clusters = {{"SHARED_A", "u_cluster"}};
    StaClusterTiming sta_cluster_timing(std::move(clusters));
    sta_cluster_timing.addHierSubNetlist();

    auto hier_sub_netlists = netlist->get_hier_sub_netlists();
    if (hier_sub_netlists.size() != 1U) {
      std::_Exit(1);
    }

    std::_Exit(0);
  };

  ASSERT_EXIT(run_cluster_port_case(), ::testing::ExitedWithCode(0), "");
}

TEST_F(RestoredClusterTimingTest,
       copied_bus_ports_rebuild_bus_membership_in_subnetlists) {
  LibLibrary lib("cluster_bus_port_writer_test");
  auto* chain_cell = addCell(
      lib, "chain_cell",
      {{"A", LibPort::LibertyPortType::kInput},
       {"Y", LibPort::LibertyPortType::kOutput}});

  auto* ista = Sta::getOrCreateSta();
  ASSERT_NE(ista, nullptr);
  auto* netlist = ista->get_netlist();
  ASSERT_NE(netlist, nullptr);
  netlist->set_name("top");

  auto& bus_port = netlist->addPort(Port("BUS_IN[0]", PortDir::kIn));
  PortBus bus("BUS_IN", 0, 0, 1, PortDir::kIn);
  bus.addPort(0, &bus_port);
  netlist->addPortBus(std::move(bus));

  auto& bus_in_net = netlist->addNet(Net("bus_in_net"));
  auto& cluster_mid_net = netlist->addNet(Net("cluster_mid_net"));
  bus_in_net.addPinPort(&bus_port);

  auto& u1 = netlist->addInstance(Instance("u1", chain_cell));
  auto* u1_a = u1.addPin("A", chain_cell->get_cell_port_or_port_bus("A"));
  auto* u1_y = u1.addPin("Y", chain_cell->get_cell_port_or_port_bus("Y"));
  bus_in_net.addPinPort(u1_a);
  cluster_mid_net.addPinPort(u1_y);

  auto& u2 = netlist->addInstance(Instance("u2", chain_cell));
  auto* u2_a = u2.addPin("A", chain_cell->get_cell_port_or_port_bus("A"));
  cluster_mid_net.addPinPort(u2_a);

  std::vector<std::set<std::string>> clusters = {{"u1", "u2"}};
  StaClusterTiming sta_cluster_timing(std::move(clusters));
  sta_cluster_timing.addHierSubNetlist();

  auto hier_sub_netlists = netlist->get_hier_sub_netlists();
  ASSERT_EQ(hier_sub_netlists.size(), 1U);
  auto* subnetlist = hier_sub_netlists.front();
  ASSERT_NE(subnetlist, nullptr);

  auto* copied_bus_port = subnetlist->findPort("BUS_IN[0]");
  ASSERT_NE(copied_bus_port, nullptr);
  ASSERT_NE(copied_bus_port->get_port_bus(), nullptr)
      << "expected copied bus-member ports to be rebound to a subnet PortBus";
  EXPECT_EQ(std::string(copied_bus_port->get_port_bus()->get_name()), "BUS_IN");

  const fs::path output_path =
      fs::temp_directory_path() / "ista_restored_cluster_bus_port.v";
  std::set<std::string> exclude_cell_names;
  subnetlist->writeVerilog(output_path.c_str(), exclude_cell_names, false);

  const auto verilog_text = readText(output_path);
  EXPECT_NE(verilog_text.find("module cluster1 (\nBUS_IN[0]"), std::string::npos)
      << "expected copied bus-member ports to stay in the module header";
  EXPECT_NE(verilog_text.find("input [0:0] BUS_IN ;"), std::string::npos)
      << "expected subnet writer to rebuild the bus declaration";
  EXPECT_EQ(verilog_text.find("input BUS_IN[0] ;"), std::string::npos)
      << "did not expect copied bus-member ports to degrade into scalar "
         "declarations once the subnet bus is rebuilt";

  std::error_code ec;
  fs::remove(output_path, ec);
}

TEST_F(RestoredClusterTimingTest,
       boundary_outputs_keep_top_level_output_ports_connected_in_subnetlists) {
  LibLibrary lib("cluster_top_output_restore_test");
  auto* driver_cell = addCell(
      lib, "driver_cell",
      {{"A", LibPort::LibertyPortType::kInput},
       {"Y", LibPort::LibertyPortType::kOutput}});
  auto* helper_cell = addCell(
      lib, "helper_cell", {{"I", LibPort::LibertyPortType::kInput}});
  auto* sink_cell = addCell(
      lib, "sink_cell", {{"I", LibPort::LibertyPortType::kInput}});

  auto* ista = Sta::getOrCreateSta();
  ASSERT_NE(ista, nullptr);
  auto* netlist = ista->get_netlist();
  ASSERT_NE(netlist, nullptr);
  netlist->set_name("top");

  auto& top_in = netlist->addPort(Port("TOP_IN", PortDir::kIn));
  auto& top_out = netlist->addPort(Port("TOP_OUT", PortDir::kOut));

  auto& input_net = netlist->addNet(Net("input_net"));
  auto& boundary_net = netlist->addNet(Net("boundary_net"));
  auto& helper_net = netlist->addNet(Net("helper_net"));
  input_net.addPinPort(&top_in);
  boundary_net.addPinPort(&top_out);

  auto& driver_inst = netlist->addInstance(Instance("u_driver", driver_cell));
  auto* driver_in =
      driver_inst.addPin("A", driver_cell->get_cell_port_or_port_bus("A"));
  auto* driver_out =
      driver_inst.addPin("Y", driver_cell->get_cell_port_or_port_bus("Y"));
  input_net.addPinPort(driver_in);
  boundary_net.addPinPort(driver_out);

  auto& helper_inst = netlist->addInstance(Instance("u_helper", helper_cell));
  auto* helper_pin =
      helper_inst.addPin("I", helper_cell->get_cell_port_or_port_bus("I"));
  helper_net.addPinPort(helper_pin);

  auto& outside_sink = netlist->addInstance(Instance("u_sink", sink_cell));
  auto* outside_sink_pin =
      outside_sink.addPin("I", sink_cell->get_cell_port_or_port_bus("I"));
  boundary_net.addPinPort(outside_sink_pin);

  std::vector<std::set<std::string>> clusters = {{"u_driver", "u_helper"}};
  StaClusterTiming sta_cluster_timing(std::move(clusters));
  sta_cluster_timing.addHierSubNetlist();

  auto hier_sub_netlists = netlist->get_hier_sub_netlists();
  ASSERT_EQ(hier_sub_netlists.size(), 1U);
  auto* subnetlist = hier_sub_netlists.front();
  ASSERT_NE(subnetlist, nullptr);

  auto* copied_top_out = subnetlist->findPort("TOP_OUT");
  auto* copied_boundary_net = subnetlist->findNet("boundary_net");
  ASSERT_NE(copied_top_out, nullptr);
  ASSERT_NE(copied_boundary_net, nullptr);
  EXPECT_TRUE(copied_boundary_net->isNetPinPort(copied_top_out))
      << "expected copied top-level outputs to remain connected to the "
         "boundary net inside the subnetlist";
}

TEST_F(RestoredClusterTimingTest,
       moved_bus_ports_without_rebuilt_bus_drop_stale_bus_ownership) {
  Netlist netlist;
  netlist.set_name("moved_bus_port_top");

  Port source_port("BUS_OUT[0]", PortDir::kOut);
  PortBus source_bus("BUS_OUT", 0, 0, 1, PortDir::kOut);
  source_bus.addPort(0, &source_port);

  auto& moved_port = netlist.addPort(std::move(source_port));
  EXPECT_EQ(moved_port.get_port_bus(), nullptr)
      << "expected moved standalone bus-member ports to drop stale PortBus "
         "ownership when the destination netlist does not rebuild the bus";

  const fs::path output_path =
      fs::temp_directory_path() / "ista_restored_moved_bus_port.v";
  std::set<std::string> exclude_cell_names;
  netlist.writeVerilog(output_path.c_str(), exclude_cell_names, false);

  const auto verilog_text = readText(output_path);
  EXPECT_NE(verilog_text.find("module moved_bus_port_top (\nBUS_OUT[0]"),
            std::string::npos);
  EXPECT_NE(verilog_text.find("output BUS_OUT[0] ;"), std::string::npos);

  std::error_code ec;
  fs::remove(output_path, ec);
}

TEST_F(RestoredClusterTimingTest,
       instance_copy_rebinds_cloned_pin_owners_to_the_new_instance) {
  LibLibrary lib("instance_copy_owner_test");
  auto* lib_cell = addCell(
      lib, "copy_cell", {{"A", LibPort::LibertyPortType::kInput}});

  Instance original("u_original", lib_cell);
  auto* original_pin =
      original.addPin("A", lib_cell->get_cell_port_or_port_bus("A"));
  ASSERT_NE(original_pin, nullptr);
  ASSERT_EQ(original_pin->get_own_instance(), &original);

  Instance copied(original);
  auto copied_pin = copied.getPin("A");
  ASSERT_TRUE(copied_pin.has_value());
  ASSERT_NE(*copied_pin, nullptr);
  EXPECT_EQ((*copied_pin)->get_own_instance(), &copied);
  EXPECT_NE((*copied_pin)->get_own_instance(), &original);
}

TEST_F(RestoredClusterTimingTest, moved_ports_preserve_capacitance_data) {
  Port original("LOAD_IN", PortDir::kIn);
  original.set_cap(AnalysisMode::kMax, TransType::kRise, 3.5);
  original.set_cap(AnalysisMode::kMin, TransType::kFall, 1.25);

  Port moved(std::move(original));
  EXPECT_DOUBLE_EQ(moved.cap(AnalysisMode::kMax, TransType::kRise), 3.5);
  EXPECT_DOUBLE_EQ(moved.cap(AnalysisMode::kMin, TransType::kFall), 1.25);

  Port assigned("ASSIGNED_LOAD", PortDir::kOut);
  assigned = std::move(moved);
  EXPECT_DOUBLE_EQ(assigned.cap(AnalysisMode::kMax, TransType::kRise), 3.5);
  EXPECT_DOUBLE_EQ(assigned.cap(AnalysisMode::kMin, TransType::kFall), 1.25);
}

}  // namespace

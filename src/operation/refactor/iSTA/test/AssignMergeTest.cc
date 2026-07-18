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
#include "gtest/gtest.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#include <unistd.h>

#include "log/Log.hh"
#include "netlist/Net.hh"
#include "sta/Sta.hh"

using namespace ista;

namespace {

std::set<std::string> collectPinPortNames(Net* net) {
  std::set<std::string> pin_port_names;
  for (auto* pin_port : net->get_pin_ports()) {
    pin_port_names.emplace(pin_port->get_name());
  }
  return pin_port_names;
}

class AssignMergeTest : public testing::Test {
 protected:
  void SetUp() override {
    char config[] = "test";
    char* argv[] = {config};
    Log::init(argv);
  }

  void TearDown() override {
    Sta::destroySta();
    Log::end();

    if (!_verilog_path.empty()) {
      std::error_code ec;
      std::filesystem::remove(_verilog_path, ec);
    }
  }

  std::string writeTempVerilog(const std::string& contents) {
    auto path =
        std::filesystem::temp_directory_path() /
        ("ista_assign_merge_" + std::to_string(::getpid()) + ".v");
    std::ofstream output(path);
    output << contents;
    output.close();
    _verilog_path = path.string();
    return _verilog_path;
  }

  Netlist* linkTop(const std::string& contents) {
    const auto verilog_path = writeTempVerilog(contents);

    auto* ista = Sta::getOrCreateSta();
    EXPECT_NE(ista, nullptr);
    if (!ista) {
      return nullptr;
    }

    EXPECT_TRUE(ista->readVerilogWithRustParser(verilog_path.c_str()));
    ista->set_top_module_name("top");
    ista->linkDesignWithRustParser("top");

    auto* design_netlist = ista->get_netlist();
    EXPECT_NE(design_netlist, nullptr);
    return design_netlist;
  }

  void expectLinkFatal(const std::string& contents, const char* error_pattern) {
    const auto verilog_path = writeTempVerilog(contents);

    EXPECT_DEATH(
        {
          auto* ista = Sta::getOrCreateSta();
          if (!ista) {
            std::abort();
          }
          ista->readVerilogWithRustParser(verilog_path.c_str());
          ista->set_top_module_name("top");
          ista->linkDesignWithRustParser("top");
        },
        error_pattern);
    Sta::destroySta();
  }

  void expectNetPins(Netlist* design_netlist, const char* net_name,
                     const std::set<std::string>& expected_pin_names) {
    ASSERT_NE(design_netlist, nullptr);
    auto* net = design_netlist->findNet(net_name);
    ASSERT_NE(net, nullptr) << "expected net missing: " << net_name;
    EXPECT_EQ(collectPinPortNames(net), expected_pin_names);
  }

 private:
  std::string _verilog_path;
};

TEST_F(AssignMergeTest, assign_net_equals_input_port_attaches_input_port) {
  auto* design_netlist = linkTop(R"(module top(a);
input a;
wire n1;
assign n1 = a;
endmodule
)");

  expectNetPins(design_netlist, "n1", {"a"});
}

TEST_F(AssignMergeTest, assign_output_port_equals_net_attaches_output_port) {
  auto* design_netlist = linkTop(R"(module top(y);
output y;
wire n1;
assign y = n1;
endmodule
)");

  expectNetPins(design_netlist, "n1", {"y"});
}

TEST_F(AssignMergeTest, assign_output_port_equals_input_port_creates_shared_net) {
  auto* design_netlist = linkTop(R"(module top(a, y);
input a;
output y;
assign y = a;
endmodule
)");

  expectNetPins(design_netlist, "a", {"a", "y"});
  EXPECT_EQ(design_netlist->findNet("y"), nullptr);
}

TEST_F(AssignMergeTest, id_to_concat_assign_expands_bus_indices) {
  auto* design_netlist = linkTop(R"(module top(a);
input [1:0] a;
wire [1:0] n;
assign n = {a[1], a[0]};
endmodule
)");

  expectNetPins(design_netlist, "n[0]", {"a[1]"});
  expectNetPins(design_netlist, "n[1]", {"a[0]"});
}

TEST_F(AssignMergeTest, id_to_concat_assign_with_slice_range_matches_donor_fatal_boundary) {
  expectLinkFatal(R"(module top(a);
input [1:0] a;
wire [1:0] n;
assign n = {a[1:0]};
endmodule
)",
                  "the left port is not exist");
}

TEST_F(AssignMergeTest, direct_net_merge_moves_existing_connections_to_target_net) {
  auto* design_netlist = linkTop(R"(module top(a, y);
input a;
output y;
wire n1;
wire n2;
assign n1 = a;
assign y = n2;
assign n1 = n2;
endmodule
)");

  expectNetPins(design_netlist, "n2", {"a", "y"});
  EXPECT_EQ(design_netlist->findNet("n1"), nullptr);
}

TEST_F(AssignMergeTest, alias_chain_reuses_merged_net_for_later_assigns) {
  auto* design_netlist = linkTop(R"(module top(a, y);
input a;
output y;
wire n1;
wire n2;
assign n1 = a;
assign n2 = n1;
assign y = n2;
endmodule
)");

  auto* n1_net = design_netlist->findNet("n1");
  ASSERT_NE(n1_net, nullptr);
  EXPECT_EQ(design_netlist->findNet("n2"), nullptr);
  EXPECT_EQ(collectPinPortNames(n1_net), (std::set<std::string>{"a", "y"}));
}

TEST_F(AssignMergeTest, merged_alias_net_is_removed_after_merge) {
  auto* design_netlist = linkTop(R"(module top(a);
input a;
wire n1;
wire n2;
assign n1 = a;
assign n2 = n1;
endmodule
)");

  expectNetPins(design_netlist, "n1", {"a"});
  EXPECT_EQ(design_netlist->findNet("n2"), nullptr);
}

TEST_F(AssignMergeTest, concat_seeded_slice_alias_chain_matches_donor_fatal_boundary) {
  expectLinkFatal(R"(module top(a, y);
input [1:0] a;
output [1:0] y;
wire [1:0] alias;
wire [1:0] mid;
assign mid = {a[1], a[0]};
assign alias[1:0] = mid[1:0];
assign y[1:0] = alias[1:0];
endmodule
)",
                  "the left port is not exist");
}

TEST_F(AssignMergeTest, bus_slice_assign_matches_donor_fatal_boundary) {
  expectLinkFatal(R"(module top(a, y);
input [3:0] a;
output [3:0] y;
wire [3:0] stage;
wire [3:0] alias;
assign stage = {a[3], a[2], a[1], a[0]};
assign alias[3:0] = stage[3:0];
assign y[3:0] = alias[3:0];
endmodule
)",
                  "the left port is not exist");
}

}  // namespace

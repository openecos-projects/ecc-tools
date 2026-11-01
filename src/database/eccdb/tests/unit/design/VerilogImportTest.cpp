// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "database/Database.h"
#include "design/DesignStore.h"
#include "import/verilog/VerilogNetlistImportService.h"

namespace eccdb {
namespace {

[[nodiscard]] std::filesystem::path testLef()
{
  return std::filesystem::path(ECCDB_TEST_DATA_ROOT) / "reference/ispd2019/ispd19_test1/ispd19_test1.input.lef";
}

class TemporaryVerilog
{
 public:
  TemporaryVerilog(std::string_view filename, std::string_view contents)
      : _path(std::filesystem::temp_directory_path() / filename)
  {
    std::ofstream output(_path);
    if (!output) {
      throw std::runtime_error("failed to create temporary Verilog fixture");
    }
    output << contents;
  }

  ~TemporaryVerilog()
  {
    std::error_code ignored;
    std::filesystem::remove(_path, ignored);
  }

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return _path; }

 private:
  std::filesystem::path _path;
};

[[nodiscard]] Database openLefVerilog(const std::filesystem::path& verilog, std::string_view top_module = {})
{
  const auto lef = testLef();
  if (!std::filesystem::exists(lef)) {
    throw std::runtime_error("missing LEF fixture: " + lef.string());
  }
  return Database::open(Config{.input = LefVerilogInput{.lef_files = std::vector<std::filesystem::path>{lef},
                                                        .verilog_file = verilog,
                                                        .top_module = std::string(top_module)}});
}

TEST(VerilogImportTest, ImportsInstancesTermsAndConnectivity)
{
  const auto lef = testLef();
  ASSERT_TRUE(std::filesystem::exists(lef));

  const auto verilog = std::filesystem::temp_directory_path() / "eccdb-verilog-import-test.v";
  {
    std::ofstream output(verilog);
    ASSERT_TRUE(output);
    output << "module top (A, Y);\n"
              "input A;\n"
              "output Y;\n"
              "wire N;\n"
              "XOR2X1 u0 (.A(A), .B(A), .Y(N));\n"
              "XOR2X1 u1 (.A(N), .B(A), .Y(Y));\n"
              "endmodule\n";
  }

  const auto cleanup = [&] { std::error_code ignored; std::filesystem::remove(verilog, ignored); };
  try {
    auto database = Database::open(Config{.input = LefVerilogInput{.lef_files = std::vector<std::filesystem::path>{lef},
                                                                    .verilog_file = verilog,
                                                                    .top_module = "top"}});
    const auto result = database.context().design().netlistStorage();
    EXPECT_EQ(result.instances().size(), 2u);
    EXPECT_EQ(result.blockTerms().size(), 2u);
    EXPECT_EQ(result.nets().size(), 3u);

    const auto net_a = result.findNet("A");
    ASSERT_TRUE(net_a);
    EXPECT_EQ(result.instanceTerms(net_a).size(), 3u);
    EXPECT_EQ(result.blockTerms(net_a).size(), 1u);
  } catch (...) {
    cleanup();
    throw;
  }
  cleanup();
}

TEST(VerilogImportTest, SelectsSingleEscapedTopModuleAutomatically)
{
  TemporaryVerilog verilog{"eccdb-verilog-escaped-auto-top.v", R"(module \top ;
endmodule
)"};

  auto database = openLefVerilog(verilog.path());
  ASSERT_TRUE(database.context().design().globalStorage().hasInfo());
  EXPECT_EQ(database.context().design().globalStorage().info().name, "top");
}

TEST(VerilogImportTest, SelectsEscapedTopModuleExplicitlyAndFlattensByRawSpelling)
{
  TemporaryVerilog verilog{"eccdb-verilog-escaped-explicit-top.v", R"(module \top ;
  LEAF u0 ();
endmodule
module LEAF ;
endmodule
)"};

  auto database = openLefVerilog(verilog.path(), "top");
  ASSERT_TRUE(database.context().design().globalStorage().hasInfo());
  EXPECT_EQ(database.context().design().globalStorage().info().name, "top");
  EXPECT_TRUE(database.context().design().netlistStorage().instances().empty());
}

TEST(VerilogImportTest, PreservesLiteralLeadingSlashInEscapedTopModuleName)
{
  TemporaryVerilog verilog{"eccdb-verilog-escaped-literal-slash-top.v", R"(module \\literal_top ;
endmodule
)"};
  constexpr std::string_view semantic_top = R"(\literal_top)";

  auto automatic = openLefVerilog(verilog.path());
  ASSERT_TRUE(automatic.context().design().globalStorage().hasInfo());
  EXPECT_EQ(automatic.context().design().globalStorage().info().name, semantic_top);

  auto explicit_top = openLefVerilog(verilog.path(), semantic_top);
  ASSERT_TRUE(explicit_top.context().design().globalStorage().hasInfo());
  EXPECT_EQ(explicit_top.context().design().globalStorage().info().name, semantic_top);
}

TEST(VerilogImportTest, PreservesAliasGroupWithPhysicalRepresentative)
{
  for (const auto* assignments : {"assign OUT=alias; assign alias=physical; assign SECOND=physical;",
                                  "assign SECOND=physical; assign alias=physical; assign OUT=alias;"}) {
    TemporaryVerilog verilog{"eccdb-verilog-alias-group.v",
                              std::string("module top(input A, output OUT, output SECOND); wire alias, physical; ")
                                  + assignments + " XOR2X1 u0(.A(A),.B(A),.Y(physical)); endmodule"};
    auto database = openLefVerilog(verilog.path());
    const auto& netlist = database.context().design().netlistStorage();
    const auto net = netlist.findNet("physical");
    ASSERT_TRUE(net);
    const auto* aliases = netlist.netAliases(net);
    ASSERT_NE(aliases, nullptr);
    EXPECT_EQ(std::set<std::string>(aliases->names.begin(), aliases->names.end()),
              (std::set<std::string>{"OUT", "SECOND", "alias", "physical"}));
    EXPECT_EQ(netlist.blockTerms(net).size(), 2u);
    EXPECT_EQ(netlist.instanceTerms(net).size(), 1u);
    EXPECT_FALSE(netlist.findNet("OUT"));
    EXPECT_EQ(netlist.netAliases(netlist.findNet("A")), nullptr);
  }
}

TEST(VerilogImportTest, SharesInputAliasAndInputWidthRules)
{
  TemporaryVerilog verilog{"eccdb-verilog-input-alias-width.v", R"(module top(input [1:0] A, output Y);
wire internal;
assign internal=A[0];
XOR2X1 u0(.A(A),.B(internal),.Y(Y));
endmodule
)"};
  auto database = openLefVerilog(verilog.path());
  const auto& netlist = database.context().design().netlistStorage();
  const auto net = netlist.findNet("internal");
  ASSERT_TRUE(net);
  EXPECT_EQ(netlist.instanceTerms(net).size(), 2u);
  EXPECT_EQ(netlist.blockTerms(net).size(), 1u);
  const auto* aliases = netlist.netAliases(net);
  ASSERT_NE(aliases, nullptr);
  EXPECT_EQ(std::set<std::string>(aliases->names.begin(), aliases->names.end()),
            (std::set<std::string>{"A[0]", "internal"}));
}

TEST(VerilogImportTest, RejectsAssignmentDriverConflictsBeforeMutation)
{
  TemporaryVerilog empty{"eccdb-verilog-empty-base.v", "module top; endmodule"};
  auto database = openLefVerilog(empty.path());
  TemporaryVerilog invalid{"eccdb-verilog-driver-conflict.v", R"(module top(input A, output Y);
assign Y=A;
XOR2X1 u0(.A(A),.B(A),.Y(Y));
endmodule
)"};
  VerilogNetlistImportService importer(database.context());
  EXPECT_THROW(static_cast<void>(importer.import(invalid.path(), "top")), VerilogNetlistImportError);
  const auto& netlist = database.context().design().netlistStorage();
  EXPECT_TRUE(netlist.nets().empty());
  EXPECT_TRUE(netlist.instances().empty());
  EXPECT_TRUE(netlist.blockTerms().empty());
}

}  // namespace
}  // namespace eccdb

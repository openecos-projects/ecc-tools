// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2. This file is a regression test.
// ***************************************************************************************

#include "verilog_read.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void testEscapedConcatIdentifiersRemainDistinctScalarNets()
{
  const auto verilog_path = std::filesystem::temp_directory_path() / "verilog_reader_escaped_identifier_test.v";
  {
    std::ofstream stream(verilog_path);
    require(stream.good(), "failed to create test Verilog file");
    stream << "module top;\n"
           << "  wire \\hierarchy[0].wdata_i[0] ;\n"
           << "  wire \\hierarchy[0].wdata_i[1] ;\n"
           << "  wire \\hierarchy[0].wdata_i[2] ;\n"
           << "  TEST_CELL \\hierarchy[0].u_mem (\n"
           << "      .D({ \\hierarchy[0].wdata_i[2] , \\hierarchy[0].wdata_i[1] , \\hierarchy[0].wdata_i[0] })\n"
           << "  );\n"
           << "endmodule\n";
  }

  idb::IdbLayout layout;
  idb::IdbDefService service(&layout);
  auto* master = layout.get_cell_master_list()->set_cell_master("TEST_CELL");
  require(master != nullptr, "failed to create test cell master");
  for (int index = 0; index != 3; ++index) {
    require(master->add_term("D[" + std::to_string(index) + "]") != nullptr, "failed to create test cell pin");
  }

  idb::VerilogRead reader(&service);
  require(reader.createDb(verilog_path.string(), "top"), "failed to import test Verilog");
  std::filesystem::remove(verilog_path);

  auto* net_list = service.get_design()->get_net_list();
  require(net_list != nullptr, "net list was not created");
  require(net_list->find_net("hierarchy[0].wdata_i") == nullptr, "concat must not create an unindexed base net");

  for (int index = 0; index != 3; ++index) {
    const std::string net_name = "hierarchy[0].wdata_i[" + std::to_string(index) + "]";
    auto* net = net_list->find_net(net_name);
    require(net != nullptr, "escaped scalar net is missing: " + net_name);
    require(net->get_instance_pin_list()->get_pin_num() == 1, "each escaped scalar net must connect one cell pin");
    require(net->get_instance_pin_list()->get_pin_list().front()->get_pin_name() == "D[" + std::to_string(index) + "]",
            "escaped scalar net connected to the wrong cell pin");
  }
}

void testEscapedIdentifiersKeepLiteralInternalBackslashes()
{
  const auto verilog_path = std::filesystem::temp_directory_path() / "verilog_reader_escaped_backslash_test.v";
  {
    std::ofstream stream(verilog_path);
    require(stream.good(), "failed to create test Verilog file");
    stream << "module top;\n"
           << R"(  wire \foo\bar ;)" << '\n'
           << R"(  wire \foobar ;)" << '\n'
           << "endmodule\n";
  }

  idb::IdbLayout layout;
  idb::IdbDefService service(&layout);
  idb::VerilogRead reader(&service);
  require(reader.createDb(verilog_path.string(), "top"), "failed to import escaped-backslash Verilog");
  std::filesystem::remove(verilog_path);

  auto* net_list = service.get_design()->get_net_list();
  require(net_list != nullptr, "net list was not created");
  auto* literal_backslash_net = net_list->find_net(R"(foo\bar)");
  auto* plain_net = net_list->find_net("foobar");
  require(literal_backslash_net != nullptr, "escaped identifier lost its literal internal backslash");
  require(plain_net != nullptr, "plain escaped identifier is missing");
  require(literal_backslash_net != plain_net, "distinct escaped identifiers were merged");
}

void testEscapedNamesMayBeginWithLiteralBackslashAcrossIoAssignAndInstanceConnections()
{
  const auto verilog_path = std::filesystem::temp_directory_path() / "verilog_reader_escaped_connection_test.v";
  {
    std::ofstream stream(verilog_path);
    require(stream.good(), "failed to create test Verilog file");
    stream << "module top;\n"
           // Every first identifier below has two leading slashes in the
           // Verilog source: one escape marker and one semantic slash.
           << R"(  input \\input ;)" << '\n'
           << R"(  output \\output ;)" << '\n'
           << R"(  wire \\foo ;)" << '\n'
           << R"(  wire \foo ;)" << '\n'
           << R"(  assign \\output = \\foo ;)" << '\n'
           << R"(  \\CELL \\instance ()" << '\n'
           << R"(      .\\A ( \\foo ))" << '\n'
           << "  );\n"
           << "endmodule\n";
  }

  idb::IdbLayout layout;
  idb::IdbDefService service(&layout);
  auto* master = layout.get_cell_master_list()->set_cell_master(R"(\CELL)");
  require(master != nullptr, "failed to create escaped-name test cell master");
  require(master->add_term(R"(\A)") != nullptr, "failed to create escaped-name test cell pin");

  idb::VerilogRead reader(&service);
  require(reader.createDb(verilog_path.string(), "top"), "failed to import escaped connection Verilog");
  std::filesystem::remove(verilog_path);

  auto* design = service.get_design();
  auto* net_list = design->get_net_list();
  auto* io_pin_list = design->get_io_pin_list();
  require(net_list != nullptr && io_pin_list != nullptr, "test design connectivity was not created");

  auto* literal_backslash_net = net_list->find_net(R"(\foo)");
  auto* plain_net = net_list->find_net("foo");
  require(literal_backslash_net != nullptr && plain_net != nullptr, "escaped connection nets are missing");
  require(literal_backslash_net != plain_net, "escaped connection nets were merged");

  auto* input_pin = io_pin_list->find_pin(R"(\input)");
  auto* output_pin = io_pin_list->find_pin(R"(\output)");
  require(input_pin != nullptr && output_pin != nullptr, "escaped IO pin names were not preserved");
  require(output_pin->get_net() == literal_backslash_net, "escaped assign did not connect the output pin to its literal-backslash net");

  auto* instance = design->get_instance_list()->find_instance(R"(\instance)");
  require(instance != nullptr, "escaped instance name was not preserved");
  require(instance->get_cell_master()->get_name() == R"(\CELL)", "escaped cell master name was not preserved");
  auto* instance_pin = instance->get_pin(R"(\A)");
  require(instance_pin != nullptr, "escaped cell port name was not preserved");
  require(instance_pin->get_net() == literal_backslash_net,
          "escaped named-port connection did not connect to its literal-backslash net");
}

void testMissingMasterFailsDesignRead()
{
  const auto verilog_path = std::filesystem::temp_directory_path() / "verilog_reader_missing_master_test.v";
  {
    std::ofstream stream(verilog_path);
    require(stream.good(), "failed to create missing-master Verilog file");
    stream << "module top;\n"
           << "  MISSING_MASTER missing_instance ();\n"
           << "endmodule\n";
  }

  idb::IdbLayout layout;
  idb::IdbDefService service(&layout);
  idb::VerilogRead reader(&service);
  require(!reader.createDb(verilog_path.string(), "top"), "Verilog reader must fail for a missing master");

  idb::IdbLayout auto_layout;
  idb::IdbDefService auto_service(&auto_layout);
  idb::VerilogRead auto_reader(&auto_service);
  require(!auto_reader.createDbAutoTop(verilog_path.string()), "auto-top Verilog reader must fail for a missing master");
  std::filesystem::remove(verilog_path);
}

}  // namespace

int main()
{
  try {
    testEscapedConcatIdentifiersRemainDistinctScalarNets();
    testEscapedIdentifiersKeepLiteralInternalBackslashes();
    testEscapedNamesMayBeginWithLiteralBackslashAcrossIoAssignAndInstanceConnections();
    testMissingMasterFailsDesignRead();
  } catch (const std::exception& error) {
    std::cout << error.what() << '\n';
    return 1;
  }
  return 0;
}

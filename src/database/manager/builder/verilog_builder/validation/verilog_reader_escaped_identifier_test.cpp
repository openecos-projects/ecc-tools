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

}  // namespace

int main()
{
  try {
    testEscapedConcatIdentifiersRemainDistinctScalarNets();
  } catch (const std::exception& error) {
    std::cout << error.what() << '\n';
    return 1;
  }
  return 0;
}

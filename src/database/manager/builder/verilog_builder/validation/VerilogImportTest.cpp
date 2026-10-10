// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2. This file is a regression test.
// ***************************************************************************************

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "VerilogConstantNet.hh"
#include "VerilogImportPlan.hh"
#include "VerilogLibrary.hh"
#include "def_service.h"
#include "verilog/VerilogFrontend.hh"
#include "verilog/VerilogSyntax.hh"
#include "verilog_read.h"
#include "verilog_write.h"

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

std::string readError(const std::filesystem::path& path, bool auto_top)
{
  idb::IdbLayout layout;
  idb::IdbDefService service(&layout);
  idb::VerilogRead reader(&service);
  try {
    if (auto_top) {
      reader.createDbAutoTop(path.string());
    } else {
      reader.createDb(path.string(), "top");
    }
  } catch (const std::runtime_error& exception) {
    return exception.what();
  }
  return {};
}

void testMissingMasterRaisesError()
{
  const auto verilog_path = std::filesystem::temp_directory_path() / "verilog_reader_missing_master_test.v";
  {
    std::ofstream stream(verilog_path);
    require(stream.good(), "failed to create missing-master Verilog file");
    stream << "module top;\n"
           << "  MISSING_MASTER missing_instance ();\n"
           << "endmodule\n";
  }

  for (bool auto_top : {false, true}) {
    const auto error = readError(verilog_path, auto_top);
    require(error.find(verilog_path.string()) != std::string::npos && error.find("missing_instance") != std::string::npos
                && error.find("MISSING_MASTER") != std::string::npos,
            "missing master must raise an error identifying the input, instance and master");
  }
  std::filesystem::remove(verilog_path);
}

void testInvalidInputRaisesError()
{
  const auto path = std::filesystem::temp_directory_path() / "verilog_reader_invalid_input_test.v";
  for (const auto* text :
       {"module top; wire ; endmodule\n", "`ifdef FLAG\nmodule top; endmodule\n`endif\n", "module top; wire a = missing; endmodule\n"}) {
    {
      std::ofstream stream(path);
      stream << text;
      require(stream.good(), "failed to write invalid test netlist");
    }
    for (bool auto_top : {false, true}) {
      require(readError(path, auto_top).find(path.string()) != std::string::npos,
              "invalid input must raise an error identifying the input");
    }
  }
  {
    std::ofstream stream(path);
    stream << "module another; endmodule\nmodule second; endmodule\n";
    require(stream.good(), "failed to write top-selection test netlist");
  }
  for (bool auto_top : {false, true}) {
    require(readError(path, auto_top).find(path.string()) != std::string::npos,
            "failed top selection must raise an error identifying the input");
  }
  std::filesystem::remove(path);
  for (bool auto_top : {false, true}) {
    require(readError(path, auto_top).find(path.string()) != std::string::npos,
            "unreadable input must raise an error identifying the input");
  }
}

void testConstantsAndPreflight()
{
  const auto path = std::filesystem::temp_directory_path() / "verilog_reader_typed_test.v";
  const auto output = std::filesystem::temp_directory_path() / "verilog_reader_typed_saved.v";
  idb::IdbLayout layout;
  auto* master = layout.get_cell_master_list()->set_cell_master("CELL");
  master->add_term("A")->set_direction(idb::IdbConnectDirection::kInput);
  master->add_term("B")->set_direction(idb::IdbConnectDirection::kInput);
  master->add_term("Y")->set_direction(idb::IdbConnectDirection::kOutput);
  auto* bus_master = layout.get_cell_master_list()->set_cell_master("BUS");
  bus_master->add_term("D[0]")->set_direction(idb::IdbConnectDirection::kInput);
  bus_master->add_term("D[1]")->set_direction(idb::IdbConnectDirection::kInput);
  auto* huge_master = layout.get_cell_master_list()->set_cell_master("HUGE");
  huge_master->add_term("D[2147483647]")->set_direction(idb::IdbConnectDirection::kInput);
  {
    idb::IdbDefService service(&layout);
    idb::VerilogRead reader(&service);
    {
      std::ofstream stream(path);
      stream << "module child(output y); CELL u(.A(1'b1),.B(1'b0),.Y(y)); endmodule module top(output y); child c(y); endmodule";
    }
    require(reader.createDbAutoTop(path.string()), "auto-top hierarchy import failed");
    auto* cell = service.get_design()->get_instance_list()->find_instance("c/u");
    require(cell && cell->get_pin("A")->get_net()->is_power() && cell->get_pin("B")->get_net()->is_ground(),
            "constant pins were dropped or changed");
    require(cell->get_pin("Y")->get_net() == service.get_design()->get_io_pin_list()->find_pin("y")->get_net(), "output binding lost");
    {
      std::set<std::string> excluded;
      idb::VerilogWriter writer(output.c_str(), excluded, *service.get_design(), true);
      writer.writeModule();
    }
    auto parsed = idb::verilog::readFile(output.string());
    require(bool(parsed), parsed ? "" : parsed.diagnostics.front().text());
    idb::IdbDefService reloaded(&layout);
    idb::VerilogRead reload(&reloaded);
    require(reload.createDbAutoTop(output.string()), "constant net roundtrip failed");
    auto* saved = reloaded.get_design()->get_instance_list()->find_instance("c/u");
    require(saved && saved->get_pin("A")->get_net()->is_power() && saved->get_pin("B")->get_net()->is_ground(),
            "constant values lost on export/reimport");
  }
  for (const auto* text :
       {"module top(output y); CELL u(.MISSING(y)); endmodule", "module top(output y); CELL u(.A(1'bx),.Y(y)); endmodule",
        "module top(input a,output y); assign y=a; CELL u(.Y(y)); endmodule",
        "module child(inout p); CELL u(.Y(p)); endmodule module top(input x); wire a; child c(a); assign a=x; endmodule",
        "module top(input [1:0] a,input b); BUS u(.D(a),.\\D[0] (b)); endmodule", "module top(input a); HUGE u(.D(a)); endmodule",
        "module top; MISSING u(); endmodule", "module top(input a,input b,output y); assign y=a; assign y=b; endmodule",
        "module top(input b,inout a,output y); assign a=b; assign y=b; endmodule",
        "module top(input b,output y); supply0 a; assign a=b; assign y=b; endmodule",
        "module power(inout supply0 p); endmodule module top(input b,output y); wire a; assign a=b; power u(a); assign y=b; endmodule",
        "module power(inout supply0 p); endmodule module top(input b,output y); wire a; power u(a); assign a=b; assign y=b; endmodule"}) {
    idb::IdbDefService service(&layout);
    service.get_design()->set_design_name("unchanged");
    idb::VerilogRead reader(&service);
    {
      std::ofstream stream(path);
      stream << text;
    }
    bool failed = false;
    try {
      reader.createDb(path.string(), "top");
    } catch (const std::runtime_error&) {
      failed = true;
    }
    require(failed, "invalid physical mapping accepted");
    require(service.get_design()->get_design_name() == "unchanged" && service.get_design()->get_net_list()->get_num() == 0
                && service.get_design()->get_instance_list()->get_num() == 0,
            "preflight failure partially mutated database");
  }
  std::filesystem::remove(path);
  std::filesystem::remove(output);
}

void testLibraryModuleInterfaces()
{
  const auto path = std::filesystem::temp_directory_path() / "verilog_library_interface_test.v";
  {
    std::ofstream stream(path);
    stream << "module BUS(input [0:1] D,output Y);endmodule\n"
              "module top(input [1:0] a,output y);BUS u(a,y);endmodule\n";
  }
  idb::IdbLayout layout;
  auto* master = layout.get_cell_master_list()->set_cell_master("BUS");
  master->add_term("Y")->set_direction(idb::IdbConnectDirection::kOutput);
  master->add_term("D[1]")->set_direction(idb::IdbConnectDirection::kInput);
  master->add_term("D[0]")->set_direction(idb::IdbConnectDirection::kInput);
  idb::IdbDefService service(&layout);
  idb::VerilogRead reader(&service);
  require(reader.createDb(path.string(), "top"), "library interface import failed");
  auto* cell = service.get_design()->get_instance_list()->find_instance("u");
  require(cell != nullptr, "empty library module disappeared during hierarchy expansion");
  auto* io = service.get_design()->get_io_pin_list();
  require(cell->get_pin("D[0]")->get_net() == io->find_pin("a[1]")->get_net(), "ascending interface reversed D[0]");
  require(cell->get_pin("D[1]")->get_net() == io->find_pin("a[0]")->get_net(), "ascending interface reversed D[1]");
  require(cell->get_pin("Y")->get_net() == io->find_pin("y")->get_net(), "positional output used LEF order");
  require(service.get_design()->get_net_list()->get_num() == 3, "library interface created phantom internal nets");
  const auto declarations = std::filesystem::temp_directory_path() / "verilog_library_interface_declarations.sv";
  {
    std::ofstream stream(declarations);
    stream << "`define WIDTH 2\n(* blackbox *) module BUS(input logic [0:`WIDTH-1] D,output logic Y);endmodule\n";
  }
  {
    std::ofstream stream(path);
    stream << "module top(input logic [`WIDTH-1:0] D,output logic Y);BUS u(.*);endmodule\n";
  }
  idb::verilog::SourceOptions options;
  options.language = idb::verilog::LanguageMode::systemVerilog;
  idb::IdbDefService sv_service(&layout);
  idb::VerilogRead sv_reader(&sv_service);
  require(sv_reader.createDb(std::vector<std::string>{declarations.string(), path.string()}, "top", options),
          "SV multi-file library import failed");
  auto* sv_cell = sv_service.get_design()->get_instance_list()->find_instance("u");
  require(sv_cell && sv_cell->get_pin("D[0]")->get_net() == sv_service.get_design()->get_io_pin_list()->find_pin("D[1]")->get_net(),
          "SV wildcard or ascending library index mapping changed");
  for (const char* declaration : {"module BUS(input [1:0] D,input Y);endmodule", "module BUS(input [2:0] D,output Y);endmodule",
                                  "module BUS(input [2:1] D,output Y);endmodule", "(* blackbox *) module MISSING(input A);endmodule"}) {
    {
      std::ofstream stream(path);
      stream << declaration << "\n";
      if (std::string(declaration).find("MISSING") != std::string::npos)
        stream << "module top(input a);MISSING u(a);endmodule";
      else
        stream << "module top(input [1:0] a,output y);BUS u(a,y);endmodule";
    }
    idb::IdbDefService invalid(&layout);
    invalid.get_design()->set_design_name("unchanged");
    bool failed = false;
    try {
      idb::VerilogRead(&invalid).createDb(path.string(), "top");
    } catch (const std::runtime_error&) {
      failed = true;
    }
    require(failed && invalid.get_design()->get_design_name() == "unchanged" && invalid.get_design()->get_net_list()->get_num() == 0,
            "invalid library interface partially imported");
  }
  std::filesystem::remove(declarations);
  std::filesystem::remove(path);
}

void testPlanOwnsResolvedMappings()
{
  std::unique_ptr<idb::verilog_import::VerilogImportPlan> plan;
  {
    idb::IdbLayout layout;
    auto* cell = layout.get_cell_master_list()->set_cell_master("BUS");
    cell->add_term("D[0]")->set_direction(idb::IdbConnectDirection::kInput);
    cell->add_term("D[1]")->set_direction(idb::IdbConnectDirection::kInput);
    idb::verilog_import::VerilogLibrary library(layout);
    auto ast = idb::verilog::parse("module top(input a); BUS u(.D({a,1'b1})); endmodule");
    require(bool(ast), "plan lifetime fixture parse failed");
    auto flat = idb::verilog::compile(*ast.design, "top", [&](auto cell) { return library.lookup(cell); });
    require(bool(flat), "plan lifetime fixture elaborate failed");
    auto result = idb::verilog_import::makeImportPlan(*flat.design, library);
    require(bool(result), "plan creation failed");
    plan = std::move(result.plan);
  }
  require(plan->top == "top" && plan->instances.size() == 1, "plan borrowed top or instance storage");
  const auto& instance = plan->instances.front();
  require(instance.master == "BUS" && instance.pins.size() == 2, "plan borrowed LEF metadata");
  require(instance.pins[0].name == "D[1]" && instance.pins[0].net == plan->ports[0].net, "planned bus pin order changed");
  require(plan->nets.at(instance.pins[1].net).type == idb::verilog::NetType::supply1, "planned constant pin lost");
  require(instance.buses[0].members[0].index == 1 && instance.buses[0].members[0].member == 0, "plan must own explicit bus member indices");
}

}  // namespace

int main()
{
  try {
    require(setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1) == 0, "failed to enable embedded error handling");
    testEscapedConcatIdentifiersRemainDistinctScalarNets();
    testMissingMasterRaisesError();
    testInvalidInputRaisesError();
    testConstantsAndPreflight();
    testLibraryModuleInterfaces();
    testPlanOwnsResolvedMappings();
  } catch (const std::exception& error) {
    std::cout << error.what() << '\n';
    return 1;
  }
  return 0;
}

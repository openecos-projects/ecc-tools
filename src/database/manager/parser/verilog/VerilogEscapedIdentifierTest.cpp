// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2. This file is a regression test.
// ***************************************************************************************

#include "VerilogReader.hh"

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

void testEscapedIdentifierWithBusCharactersStaysScalar()
{
  const auto verilog_path = std::filesystem::temp_directory_path() / "verilog_escaped_identifier_test.v";
  {
    std::ofstream stream(verilog_path);
    require(stream.good(), "failed to create test Verilog file");
    stream << "module leaf(A);\n"
           << "  input A;\n"
           << "endmodule\n"
           << "module top;\n"
           << "  wire \\hierarchy[0].wdata_i[0] ;\n"
           << "  leaf \\hierarchy[0].instance ( .A(\\hierarchy[0].wdata_i[0] ) );\n"
           << "endmodule\n";
  }

  idb::VerilogReader reader;
  require(reader.readVerilog(verilog_path.c_str()) != 0, "failed to parse test Verilog file");

  ParsedVerilogModule* top_module = nullptr;
  for (auto* module : reader.get_verilog_modules()) {
    if (module != nullptr && std::string(module->module_name) == "top") {
      top_module = module;
      break;
    }
  }
  require(top_module != nullptr, "top module was not parsed");

  ParsedVerilogInst* instance = nullptr;
  void* statement = nullptr;
  FOREACH_VERILOG_VEC_ELEM(&top_module->module_stmts, void, statement)
  {
    if (verilog_is_module_inst_stmt(statement)) {
      instance = verilog_convert_inst(statement);
      break;
    }
  }
  require(instance != nullptr, "test instance was not parsed");
  require(instance->port_connections.len == 1, "test instance must have one connection");

  void* connection_handle = GetVerilogVecElem<void>(&instance->port_connections, 0);
  auto* connection = verilog_convert_port_ref_port_connect(connection_handle);
  require(connection != nullptr && verilog_is_id_expr(connection->net_expr), "escaped net must be an identifier expression");

  auto* expression = verilog_convert_net_id_expr(connection->net_expr);
  void* identifier = const_cast<void*>(expression->verilog_id);
  require(verilog_is_id(identifier), "escaped identifier with brackets must not become a bus index");
  require(std::string(verilog_convert_id(identifier)->id) == "\\hierarchy[0].wdata_i[0]", "escaped identifier text was not preserved");

  verilog_free_file(reader.get_verilog_file_ptr());
  std::filesystem::remove(verilog_path);
}

}  // namespace

int main()
{
  try {
    testEscapedIdentifierWithBusCharactersStaysScalar();
  } catch (const std::exception& error) {
    std::cout << error.what() << '\n';
    return 1;
  }
  return 0;
}

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
#include <memory>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void testStringParameter(const std::filesystem::path& path, const std::string& value)
{
  {
    std::ofstream stream(path);
    require(stream.good(), "failed to create test Verilog file");
    stream << "module top(A);\n"
           << "  input A;\n"
           << "  leaf #(.INIT_FILE(" << value << "), .WIDTH(8)) u0 (.A(A));\n"
           << "  leaf u1 (.A(A));\n"
           << "endmodule\n";
  }

  idb::VerilogReader reader;
  require(reader.readVerilog(path.c_str()) != 0, "failed to parse string parameter: " + value);
  const std::unique_ptr<void, decltype(&verilog_free_file)> file(reader.get_verilog_file_ptr(), verilog_free_file);
  const auto& modules = reader.get_verilog_modules();
  require(modules.size() == 1 && std::string(modules[0]->module_name) == "top", "top module was not preserved");

  int instance_count = 0;
  void* statement = nullptr;
  FOREACH_VERILOG_VEC_ELEM(&modules[0]->module_stmts, void, statement)
  {
    if (!verilog_is_module_inst_stmt(statement)) {
      continue;
    }
    auto* instance = verilog_convert_inst(statement);
    require(std::string(instance->cell_name) == "leaf", "cell type was not preserved");
    require(std::string(instance->inst_name) == "u" + std::to_string(instance_count), "instance name was not preserved");
    require(instance->port_connections.len == 1, "instance connection was not preserved");
    auto* connection = verilog_convert_port_ref_port_connect(GetVerilogVecElem<void>(&instance->port_connections, 0));
    require(connection != nullptr && verilog_is_id_expr(connection->net_expr), "connection must remain an identifier");
    auto* expression = verilog_convert_net_id_expr(connection->net_expr);
    auto* identifier = verilog_convert_id(const_cast<void*>(expression->verilog_id));
    require(std::string(identifier->id) == "A", "connection name was not preserved");
    ++instance_count;
  }
  require(instance_count == 2, "parser lost an instance after the parameter block");
}

}  // namespace

int main(int argc, char** argv)
{
  try {
    require(argc == 2, "expected an isolated fixture path");
    for (const auto* value : {R"("rom.bin")", R"("bank (0), [rom].bin")", R"("bank \"(quoted]\" \\rom.bin")", R"("")"}) {
      testStringParameter(argv[1], value);
    }
    std::filesystem::remove(argv[1]);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}

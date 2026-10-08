// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "VerilogReader.hh"

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct Fixture
{
  Fixture()
  {
    auto pattern = (std::filesystem::temp_directory_path() / "verilog-large-XXXXXX").string();
    const int fd = mkstemp(pattern.data());
    require(fd >= 0, "could not create temporary netlist");
    close(fd);
    path = pattern;
  }
  ~Fixture()
  {
    verilog_free_file(reader.get_verilog_file_ptr());
    std::error_code error;
    std::filesystem::remove(path, error);
  }
  ParsedVerilogModule* read(const char* top)
  {
    require(reader.readVerilog(path.c_str()), "parse failed");
    for (auto* module : reader.get_verilog_modules()) {
      if (std::string(module->module_name) == top) {
        return module;
      }
    }
    throw std::runtime_error("missing top module");
  }
  std::filesystem::path path;
  idb::VerilogReader reader;
};

std::string expressionName(const void* handle)
{
  auto* expr = const_cast<void*>(handle);
  require(verilog_is_id_expr(expr), "expected identifier expression");
  auto* id = const_cast<void*>(verilog_convert_net_id_expr(expr)->verilog_id);
  require(verilog_is_id(id), "escaped scalar identifier became an index");
  return verilog_convert_id(id)->id;
}

std::string describeExpression(const void* handle)
{
  if (!handle) {
    return "<open>";
  }
  auto* expr = const_cast<void*>(handle);
  if (verilog_is_constant(expr)) {
    auto* id = const_cast<void*>(verilog_convert_constant_expr(expr)->verilog_id);
    return verilog_convert_id(id)->id;
  }
  if (verilog_is_concat_expr(expr)) {
    auto* concat = verilog_convert_net_concat_expr(expr);
    std::string result = "{";
    for (uintptr_t i = 0; i < concat->verilog_id_concat.len; ++i) {
      if (i != 0) {
        result += ",";
      }
      result += describeExpression(GetVerilogVecElem<void>(&concat->verilog_id_concat, i));
    }
    return result + "}";
  }
  require(verilog_is_id_expr(expr), "expected net expression");
  auto* id = const_cast<void*>(verilog_convert_net_id_expr(expr)->verilog_id);
  if (verilog_is_bus_index_id(id)) {
    return verilog_convert_index_id(id)->id;
  }
  return expressionName(handle);
}

void testDeclarationRangesAndDuplicatePrecedence()
{
  Fixture fixture;
  {
    std::ofstream out(fixture.path);
    // Redundant declarations pin the existing parser's first-declaration
    // behavior; this performance change must not introduce new validation rules.
    out << "module child(A); input [3:0] A; wire A; wire [0:3] asc, peer;\n"
        << "wire shadow; wire [1:0] shadow;\n"
        << "CELL bit_cell(.A(A[2]), .Y(asc[1]));\n"
        << "CELL slice_cell(.A(A[3:2]), .Y(asc));\n"
        << "CELL scalar_cell(.A(shadow), .Y(peer)); endmodule\n"
        << "module other(A); input [0:3] A; CELL bit_cell(.A(A[1])); endmodule\n"
        << "module top; wire [7:4] bus; child u(.A(bus)); other v(.A(bus)); endmodule\n";
  }
  auto* top = fixture.read("top");
  verilog_flatten_module(fixture.reader.get_verilog_file_ptr(), "top");
  const char* names[] = {"u/bit_cell", "u/slice_cell", "u/scalar_cell", "v/bit_cell"};
  const char* inputs[] = {"bus[6]", "{bus[7],bus[6]}", "u/shadow", "bus[6]"};
  const char* outputs[] = {"u/asc[1]", "{u/asc[0],u/asc[1],u/asc[2],u/asc[3]}", "{u/peer[0],u/peer[1],u/peer[2],u/peer[3]}"};
  int found = 0;
  void* stmt = nullptr;
  FOREACH_VERILOG_VEC_ELEM(&top->module_stmts, void, stmt)
  {
    if (!verilog_is_module_inst_stmt(stmt)) {
      continue;
    }
    require(found < 4, "unexpected flattened cell");
    auto* inst = verilog_convert_inst(stmt);
    require(std::string(inst->inst_name) == names[found], "flattened cell order changed");
    auto* input = verilog_convert_port_ref_port_connect(GetVerilogVecElem<void>(&inst->port_connections, 0));
    require(describeExpression(input->net_expr) == inputs[found], "port range or duplicate precedence changed");
    if (found < 3) {
      auto* output = verilog_convert_port_ref_port_connect(GetVerilogVecElem<void>(&inst->port_connections, 1));
      require(describeExpression(output->net_expr) == outputs[found], "ascending or grouped declaration range changed");
    }
    ++found;
  }
  require(found == 4, "missing flattened cells");
}

void testLargeFlatModulePreservesEveryStatement()
{
  Fixture fixture;
  constexpr int count = 100000;
  {
    std::ofstream out(fixture.path);
    out << "module top(A);\ninput A;\n";
    for (int i = 0; i < count; ++i) {
      out << "wire \\n[" << i << "] ;\n"
          << "CELL u" << i << " (.A(A), .Y(\\n[" << i << "] ));\n"
          << "assign \\n[" << i << "] = A;\n";
    }
    out << "endmodule\nmodule wrapper(A); input A; top u(.A(A)); endmodule\n";
    require(out.good(), "could not write large netlist");
  }
  auto* top = fixture.read("top");
  require(top->module_stmts.len == 300001, "lost or duplicated module statements");
  require(top->port_list.len == 1, "lost module port during statement append");
  auto& stmts = top->module_stmts;
  for (int i = 0; i < count; ++i) {
    const auto name = "\\n[" + std::to_string(i) + "]";
    auto* dcl_handle = GetVerilogVecElem<void>(&stmts, 1 + 3 * i);
    require(verilog_is_dcls_stmt(dcl_handle), "declaration order changed");
    auto* dcls = verilog_convert_dcls(dcl_handle);
    require(dcls->verilog_dcls.len == 1, "unexpected declaration count");
    auto* dcl = verilog_convert_dcl(GetVerilogVecElem<void>(&dcls->verilog_dcls, 0));
    require(dcl->dcl_name == name && dcl->dcl_type == DclType::KWire, "declaration changed");

    auto* inst_handle = GetVerilogVecElem<void>(&stmts, 2 + 3 * i);
    require(verilog_is_module_inst_stmt(inst_handle), "instance order changed");
    auto* inst = verilog_convert_inst(inst_handle);
    require(inst->inst_name == "u" + std::to_string(i) && std::string(inst->cell_name) == "CELL", "instance changed");
    require(inst->port_connections.len == 2, "connection count changed");
    auto* connection = verilog_convert_port_ref_port_connect(GetVerilogVecElem<void>(&inst->port_connections, 1));
    require(expressionName(connection->net_expr) == name, "instance connected to wrong net");

    auto* assign_handle = GetVerilogVecElem<void>(&stmts, 3 + 3 * i);
    require(verilog_is_module_assign_stmt(assign_handle), "assignment order changed");
    auto* assign = verilog_convert_assign(assign_handle);
    require(expressionName(assign->left_net_expr) == name && expressionName(assign->right_net_expr) == "A", "assignment changed");
  }
  fixture.reader.flattenModule("top");
  require(top->module_stmts.len == 300001 && top->port_list.len == 1, "flatten changed an already flat module");
  fixture.reader.flattenModule("wrapper");
  auto* wrapper = fixture.reader.get_top_module();
  require(wrapper->module_stmts.len == 300001, "hierarchy expansion lost statements");
  for (int i = 0; i < count; ++i) {
    auto* inst = verilog_convert_inst(GetVerilogVecElem<void>(&wrapper->module_stmts, 2 + 3 * i));
    require(inst->inst_name == "u/u" + std::to_string(i), "hierarchy prefix changed");
    auto* input = verilog_convert_port_ref_port_connect(GetVerilogVecElem<void>(&inst->port_connections, 0));
    auto* output = verilog_convert_port_ref_port_connect(GetVerilogVecElem<void>(&inst->port_connections, 1));
    require(expressionName(input->net_expr) == "A", "child input was not mapped to parent");
    require(expressionName(output->net_expr) == "u/\\n[" + std::to_string(i) + "]", "internal net mapping changed");
  }
}

void testExportedModuleViewAfterFlatten()
{
  Fixture fixture;
  {
    std::ofstream out(fixture.path);
    out << "module child(A); input A; wire internal; CELL g(.A(A), .Y(internal)); endmodule\n"
        << "module top(A); input A; child c0(.A(A)); child c1(.A(A)); endmodule\n";
  }
  auto* top = fixture.read("top");
  require(top->module_stmts.len == 3, "incorrect pre-flatten module");
  verilog_flatten_module(fixture.reader.get_verilog_file_ptr(), "top");
  require(top->module_stmts.len == 5 && top->port_list.len == 1, "exported module view is stale after flatten");
  const char* names[] = {"c0/internal", "c0/g", "c1/internal", "c1/g"};
  for (int i = 0; i < 4; ++i) {
    auto* handle = GetVerilogVecElem<void>(&top->module_stmts, i + 1);
    if (i % 2 == 0) {
      require(verilog_is_dcls_stmt(handle), "flattened wire missing");
      auto* dcls = verilog_convert_dcls(handle);
      auto* dcl = verilog_convert_dcl(GetVerilogVecElem<void>(&dcls->verilog_dcls, 0));
      require(std::string(dcl->dcl_name) == names[i], "flattened declaration order changed");
    } else {
      require(verilog_is_module_inst_stmt(handle), "child instance not replaced");
      auto* inst = verilog_convert_inst(handle);
      require(std::string(inst->inst_name) == names[i], "flattened instance order changed");
    }
  }
  fixture.reader.flattenModule("top");
  require(top->module_stmts.len == 5, "repeated flatten duplicated statements");
}

void testManyChildInstancesPreserveConnectionsAndOrder()
{
  Fixture fixture;
  constexpr int count = 150000;
  {
    std::ofstream out(fixture.path);
    out << "module child(A); input A; wire inner; CELL g(.A(A), .Y(inner)); endmodule\n"
        << "module top(A,B); input A,B; CELL first(.A(A));\n";
    for (int i = 0; i < count; ++i) {
      out << "child c" << i << "(.A(" << (i % 2 ? "B" : "A") << "));\n";
    }
    out << "CELL last(.A(B)); endmodule\n";
  }
  auto* top = fixture.read("top");
  verilog_flatten_module(fixture.reader.get_verilog_file_ptr(), "top");
  require(top->module_stmts.len == 3 + 2 * count, "lost statements while expanding many children");
  require(std::string(verilog_convert_inst(GetVerilogVecElem<void>(&top->module_stmts, 1))->inst_name) == "first",
          "first original leaf moved");
  require(std::string(verilog_convert_inst(GetVerilogVecElem<void>(&top->module_stmts, 2))->inst_name) == "last",
          "last original leaf moved");
  for (int i = 0; i < count; ++i) {
    auto* inst = verilog_convert_inst(GetVerilogVecElem<void>(&top->module_stmts, 4 + 2 * i));
    require(inst->inst_name == "c" + std::to_string(i) + "/g", "child expansion order changed");
    auto* a = verilog_convert_port_ref_port_connect(GetVerilogVecElem<void>(&inst->port_connections, 0));
    auto* y = verilog_convert_port_ref_port_connect(GetVerilogVecElem<void>(&inst->port_connections, 1));
    require(expressionName(a->net_expr) == (i % 2 ? "B" : "A"), "binding reused from a different instance");
    require(expressionName(y->net_expr) == "c" + std::to_string(i) + "/inner", "child internal net lost its scope");
  }
  fixture.reader.flattenModule("top");
  require(top->module_stmts.len == 3 + 2 * count, "repeated wide hierarchy expansion duplicated statements");
}

void testNestedHierarchyConcatenationsAndFourStateConstants()
{
  Fixture fixture;
  {
    std::ofstream out(fixture.path);
    out << "module leaf(P); input [3:0] P; CELL g(.HI(P[3]), .LO(P[0]), .MID(P[2:1]), .OPEN()); endmodule\n"
        << "module branch(A); input [7:0] A; leaf l(.P({A[7:6],A[1:0]})); leaf h(.P(A[5:2])); endmodule\n"
        << "module top; wire [15:8] bus; branch b0(.A(bus)); branch b1(.A(8'b10xz01z0)); endmodule\n";
  }
  auto* top = fixture.read("top");
  verilog_flatten_module(fixture.reader.get_verilog_file_ptr(), "top");
  const char* names[] = {"b0/l/g", "b0/h/g", "b1/l/g", "b1/h/g"};
  const char* expected[][3] = {{"bus[15]", "bus[8]", "{bus[14],bus[9]}"},
                               {"bus[13]", "bus[10]", "{bus[12],bus[11]}"},
                               {"1'b1", "1'b0", "{1'b0,1'bz}"},
                               {"1'bx", "1'b1", "{1'bz,1'b0}"}};
  require(top->module_stmts.len == 5, "nested hierarchy lost or duplicated statements");
  for (int i = 0; i < 4; ++i) {
    auto* inst = verilog_convert_inst(GetVerilogVecElem<void>(&top->module_stmts, i + 1));
    require(std::string(inst->inst_name) == names[i], "nested hierarchy ordering or prefix changed");
    for (int j = 0; j < 4; ++j) {
      auto* port = verilog_convert_port_ref_port_connect(GetVerilogVecElem<void>(&inst->port_connections, j));
      require(describeExpression(port->net_expr) == (j < 3 ? expected[i][j] : "<open>"),
              "nested port mapping changed bit order, four-state value or open connection");
    }
  }
}

void testLexicalBoundariesAndLocations()
{
  Fixture fixture;
  {
    std::ofstream out(fixture.path);
    out << "/* module ignored;\n endmodule */\n"
        << "module top(A); // header\n"
        << "input A;\n"
        << "(* keep = 1 *) wire \\n[0] \t;\n"
        << "CELL u(.A(A), .B(4'b10xz), .C(8'hAf), .D(6'o17), .E(8'd25), .Y(\\n[0] \t));\n"
        << "endmodule\n";
  }
  auto* top = fixture.read("top");
  require(top->line_no == 3 && top->module_stmts.len == 3, "comments or attributes changed token locations");
  auto* inst = verilog_convert_inst(GetVerilogVecElem<void>(&top->module_stmts, 2));
  require(inst->line_no == 6, "instance location changed");
  const char* expected[] = {"A", "4'b10xz", "8'hAf", "6'o17", "8'd25", "\\n[0]"};
  for (int i = 0; i < 6; ++i) {
    auto* port = verilog_convert_port_ref_port_connect(GetVerilogVecElem<void>(&inst->port_connections, i));
    require(describeExpression(port->net_expr) == expected[i], "lexer changed an identifier or constant token");
  }
  {
    std::ofstream out(fixture.path);
    out << "module invalid; wire [3:] x; endmodule\n";
  }
  auto* invalid = verilog_parse_file(fixture.path.c_str());
  const bool rejected = invalid == nullptr;
  verilog_free_file(invalid);
  require(rejected, "invalid range syntax was accepted");
}

}  // namespace

int main()
{
  try {
    testDeclarationRangesAndDuplicatePrecedence();
    testLargeFlatModulePreservesEveryStatement();
    testExportedModuleViewAfterFlatten();
    testNestedHierarchyConcatenationsAndFourStateConstants();
    testLexicalBoundariesAndLocations();
    testManyChildInstancesPreserveConnectionsAndOrder();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}

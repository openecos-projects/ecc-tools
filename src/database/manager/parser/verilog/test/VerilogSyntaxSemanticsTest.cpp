// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include <algorithm>
#include <iostream>
#include <stdexcept>

#include "VerilogElaborator.hh"
#include "VerilogParser.hh"

using namespace idb::verilog;
namespace {
void require(bool value, const std::string& message)
{
  if (!value)
    throw std::runtime_error(message);
}
std::unique_ptr<FlatDesign> read(std::string_view source, std::string_view top = "top")
{
  auto parsed = parse(source, "fixture.v");
  require(bool(parsed), parsed ? "" : parsed.diagnostics.front().text());
  auto flat = elaborate(*parsed.design, top);
  require(bool(flat), flat ? "" : flat.diagnostics.front().text());
  return std::move(flat.design);
}
std::string bit(const FlatDesign& flat, NetId id)
{
  for (size_t depth = 0; !isConstant(id) && depth < flat.nets.size(); ++depth) {
    auto found = std::find_if(flat.assignments.begin(), flat.assignments.end(), [=](const auto& edge) { return edge.target == id; });
    if (found == flat.assignments.end())
      break;
    id = found->source;
  }
  if (id == zeroBit)
    return "0";
  if (id == oneBit)
    return "1";
  if (id == xBit)
    return "x";
  if (id == zBit)
    return "z";
  return flat.nets.at(id).name;
}
void bad(std::string_view source, std::string_view needle)
{
  auto parsed = parse(source, "bad.v");
  std::vector<Diagnostic> diagnostics;
  if (!parsed)
    diagnostics = parsed.diagnostics;
  else {
    auto flat = elaborate(*parsed.design, "top");
    require(!flat, "invalid source accepted: " + std::string(source));
    diagnostics = flat.diagnostics;
  }
  require(!diagnostics.empty() && diagnostics.front().message.find(needle) != std::string::npos,
          "wrong diagnostic for " + std::string(source) + ": " + (diagnostics.empty() ? "<none>" : diagnostics.front().text()));
  require(diagnostics.front().file == "bad.v" && diagnostics.front().location.line > 0 && diagnostics.front().location.column > 0,
          "diagnostic must retain source location");
}
void legalDriverRelations()
{
  auto parsed = parse("module top(input a,input b,output y); assign y=a; assign y=b; endmodule");
  require(bool(parsed), "legal multi-driver syntax rejected");
  auto result = elaborate(*parsed.design, "top");
  require(bool(result), "language elaboration must retain legal multiple drivers for backend capability checking");
  const auto& edges = result.design->assignments;
  require(edges.size() == 2 && edges[0].target == edges[1].target && edges[0].source != edges[1].source,
          "distinct directed drivers were collapsed");
  auto supply = read("module top(input b); supply0 a; assign a=b; endmodule");
  require(supply->nets[0].type == NetType::wire && supply->assignments[0].source == 0,
          "assignment into a supply must not turn its source into a constant");
}
void hierarchy()
{
  auto flat = read(
      "module child(input signed [0:3] a, output [1:0] y); assign y=a[1:2]; CELL c(.A(a),.Y(y)); endmodule\n"
      "module top; wire [7:4] n; wire [1:0] r; child u(n,r); endmodule");
  require(flat->instances.size() == 1 && flat->instances[0].name == "u/c", "hierarchy was not expanded");
  const auto& ports = flat->instances[0].ports;
  require(ports[0].signal.bits.size() == 4, "port width lost");
  require(bit(*flat, ports[0].signal.bits[0]) == "n[7]" && bit(*flat, ports[0].signal.bits[3]) == "n[4]",
          "ascending input mapping reversed");
  require(bit(*flat, ports[1].signal.bits[0]) == "n[6]" && bit(*flat, ports[1].signal.bits[1]) == "n[5]", "slice significance reversed");
}
void values()
{
  auto flat = read("module top; wire signed [3:0] a=4'shf; wire [7:0] b=a; CELL c(.A(b),.B({2{2'bxz}})); endmodule");
  const auto& ports = flat->instances[0].ports;
  for (auto id : ports[0].signal.bits)
    require(bit(*flat, id) == "1", "signed net extension lost");
  require(ports[1].signal.bits == std::vector<NetId>({xBit, zBit, xBit, zBit}), "repeat/four-state bits lost");
  auto parameters = read(
      "module child #(parameter W=2, parameter signed [3:0] V=-1)(output [W-1:0] y); assign y=V; endmodule\n"
      "module top; wire [5:0] a; child #(.W(6)) c(.y(a)); CELL u(.A(a)); endmodule");
  for (auto id : parameters->instances[0].ports[0].signal.bits)
    require(bit(*parameters, id) == "1", "parameter override/extension failed");
  auto indexed = read("module top; wire [0:7] a; CELL u(.A(a[2 +: 3]),.B(a[5 -: 2])); endmodule");
  require(bit(*indexed, indexed->instances[0].ports[0].signal.bits[0]) == "a[2]", "ascending indexed select reversed");
  require(bit(*indexed, indexed->instances[0].ports[1].signal.bits[0]) == "a[4]", "descending indexed select reversed");
}
void contextValues()
{
  auto flat = read(
      "module top; wire [8:0] a=(8'd255+8'd1); wire [15:0] b=4'hf**6'ha; wire [63:0] c='hx; CELL "
      "u(.A(a),.B(b),.C(c),.D((4'd15+4'd1)+32'd0)); endmodule");
  const auto& ports = flat->instances[0].ports;
  require(bit(*flat, ports[0].signal.bits[0]) == "1", "assignment width must propagate before addition");
  std::string power;
  for (auto id : ports[1].signal.bits)
    power += bit(*flat, id);
  require(power == "1010110001100001", "power context width wrong");
  for (auto id : ports[2].signal.bits)
    require(bit(*flat, id) == "x", "unsized x must pad with x even unsigned");
  require(bit(*flat, ports[3].signal.bits[27]) == "1", "nested operand context width wrong");
  auto replicate = read("module top; CELL u(.A({((4'd15+4'd1)+32'd0){1'b1}})); endmodule");
  require(replicate->instances[0].ports[0].signal.bits.size() == 16, "repeat count uses incorrectly truncated constant");
  auto override_value = read(
      "module child #(parameter [8:0] P=0)(output [8:0] y); assign y=P; endmodule module top; wire [8:0] a; child #(.P(8'd255+8'd1)) c(a); "
      "CELL u(.A(a)); endmodule");
  require(bit(*override_value, override_value->instances[0].ports[0].signal.bits[0]) == "1", "override width context lost");
  auto port_value = read("module child(input [8:0] a); CELL u(.A(a)); endmodule module top; child c(8'd255+8'd1); endmodule");
  require(bit(*port_value, port_value->instances[0].ports[0].signal.bits[0]) == "1", "input port width context lost");
  auto parsed = parse("module top; CELL u(.A(8'd255+8'd1)); endmodule");
  auto library = elaborate(*parsed.design, "top", [](std::string_view, std::string_view) { return PortShape{Direction::input, 9}; });
  require(library && library.design->instances[0].ports[0].signal.bits.size() == 9
              && library.design->instances[0].ports[0].signal.bits[0] == oneBit,
          "library input port width context lost");
  bad("module top; CELL u(.A(u)); endmodule", "conflict");
  bad("module top(a); input wire a; wire a; endmodule", "duplicate");
  bad("module child #(parameter A=1)(output y); parameter B=1; assign y=B; endmodule module top; wire y; child #(.B(0)) u(y); endmodule",
      "parameter");
  std::string deep = "module top; parameter P=1";
  for (int i = 0; i < 1000; ++i)
    deep += "+1";
  deep += "; endmodule";
  bad(deep, "depth");
}

void identifiersAndOwnership()
{
  auto parsed = parse(
      "module child(input \\a ); CELL \\x\\y (.A(a)); endmodule module top; wire \\literal[0] ; child c(.a(\\literal[0] )); endmodule");
  require(bool(parsed), "escaped identifiers rejected");
  const auto modules = parsed.design->modules.size();
  auto first = elaborate(*parsed.design, "top");
  auto second = elaborate(*parsed.design);
  require(first && second && parsed.design->modules.size() == modules, "elaboration mutated AST or auto-top failed");
  require(first.design->instances[0].name == "c/x\\y", "internal backslash lost");
  require(bit(*first.design, first.design->instances[0].ports[0].signal.bits[0]) == "literal[0]", "escaped scalar became bus select");
  auto moved = std::move(parsed);
  require(moved.design != nullptr, "AST ownership cannot move");
}
void diagnostics()
{
  bad("module top; wire a; wire a; endmodule", "duplicate");
  bad("module top(a); input [1:0] a; wire [2:0] a; endmodule", "range");
  bad("module top(a); endmodule", "port");
  bad("module top; top u(); endmodule", "recursive");
  bad("module top; wire [3:0] a; CELL u(.A(a[0:2])); endmodule", "direction");
  bad("`default_nettype none\nmodule top; CELL u(.A(missing)); endmodule", "undeclared");
  bad("module top; wire a=b; endmodule", "undeclared");
  bad("module top; CELL u(.A(a),.A(b)); endmodule", "duplicate");
  bad("module top; assign 1'b0=1'b1; endmodule", "lvalue");
  bad("module top; always begin end endmodule", "unsupported");
  bad("`ifdef X\nmodule top; endmodule\n`endif", "directive");
  bad("module top; wire signedreg; /*", "unterminated");
  bad("module top; wire \\unterminated", "whitespace");
  bad("module top; CELL u(.A(4'b2)); endmodule", "digit");
  bad("module child(input a); endmodule module top; child u(.bad(a)); endmodule", "port");
  bad("module child #(parameter P=1)(); endmodule module top; child #(.X(1)) u(); endmodule", "parameter");
  bad("module top; wire a; wire b; CELL u(.A(a+b)); endmodule", "nonconstant");
  auto comments = read("(* keep = \"true\" *) module top; CELL u(.A(8 /*size*/ 'h /*value*/ ff)); endmodule");
  require(comments->instances[0].ports[0].signal.bits == std::vector<NetId>(8, oneBit), "comments between number tokens changed value");
  auto implicit = read("module top; CELL u(.A(a)); endmodule");
  require(implicit->nets.size() == 1, "implicit scalar net missing");
}
}  // namespace
int main()
{
  try {
    legalDriverRelations();
    hierarchy();
    values();
    identifiersAndOwnership();
    diagnostics();
    contextValues();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

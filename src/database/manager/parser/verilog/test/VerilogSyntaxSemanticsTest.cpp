// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include <algorithm>
#include <iostream>
#include <stdexcept>

#include "VerilogFrontend.hh"
#include "VerilogHierarchy.hh"
#include "VerilogSyntax.hh"

using namespace idb::verilog;
namespace {
void require(bool value, const std::string& message)
{
  if (!value)
    throw std::runtime_error(message);
}
std::unique_ptr<Netlist> read(std::string_view source, std::string_view top = "top")
{
  auto parsed = parse(source, "fixture.v");
  require(bool(parsed), parsed ? "" : parsed.diagnostics.front().text());
  auto flat = compile(*parsed.design, top);
  require(bool(flat), flat ? "" : flat.diagnostics.front().text());
  return std::move(flat.design);
}
std::string bit(const Netlist& flat, NetId id)
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
    auto flat = compile(*parsed.design, "top");
    require(!flat, "invalid source accepted: " + std::string(source));
    diagnostics = flat.diagnostics;
  }
  require(!diagnostics.empty() && diagnostics.front().message.find(needle) != std::string::npos,
          "wrong diagnostic for " + std::string(source) + ": " + (diagnostics.empty() ? "<none>" : diagnostics.front().text()));
  require(diagnostics.front().file == "bad.v" && diagnostics.front().location.line > 0 && diagnostics.front().location.column > 0,
          "diagnostic must retain source location");
}
ParseResult parseSv(std::string_view source)
{
  SourceOptions options;
  options.language = LanguageMode::systemVerilog;
  return parse(source, "fixture.sv", options);
}
std::unique_ptr<Netlist> readSv(std::string_view source)
{
  auto parsed = parseSv(source);
  require(bool(parsed), parsed ? "" : parsed.diagnostics.front().text());
  auto result = compile(*parsed.design, "top");
  require(bool(result), result ? "" : result.diagnostics.front().text());
  return std::move(result.design);
}
void badSv(std::string_view source, std::string_view needle)
{
  auto parsed = parseSv(source);
  const auto diagnostics = [&] {
    if (!parsed)
      return parsed.diagnostics;
    auto flat = compile(*parsed.design, "top");
    require(!flat, "invalid SystemVerilog accepted: " + std::string(source));
    return flat.diagnostics;
  }();
  require(!diagnostics.empty() && diagnostics.front().message.find(needle) != std::string::npos,
          "unexpected SystemVerilog diagnostic: " + (diagnostics.empty() ? "<none>" : diagnostics.front().text()));
}
void systemVerilogValuesAndTypes()
{
  // Hand-derived bit patterns catch treating a fill literal as an ordinary one-bit number.
  const auto flat = readSv(R"V(module top(output [7:0] y);
    assign y='1;
    CELL u(.A({'1,'0}), .B(8'b0 | '1), .C(8'hff == '1), .D(1'b1 ? '1 : 8'b0));
  endmodule)V");
  for (auto id : flat->ports[0].signal.bits)
    require(bit(*flat, id) == "1", "fill literal did not fill assignment width");
  const auto& ports = flat->instances[0].ports;
  require(ports[0].signal.bits == std::vector<NetId>{oneBit, zeroBit}, "fill literal concat is not self determined");
  require(ports[1].signal.bits == std::vector<NetId>(8, oneBit), "fill literal lost binary context");
  require(ports[2].signal.bits == std::vector<NetId>{oneBit}, "fill literal lost comparison context");
  require(ports[3].signal.bits == std::vector<NetId>(8, oneBit), "fill literal lost conditional context");
  auto types = readSv(R"V(module top #(parameter int unsigned N=3, parameter int X='x,
      parameter logic [7:0] ONES='1)(input logic [N-1:0] a, output logic [N-1:0] y);
    wire logic [N-1:0] w;
    logic [N-1:0] v;
    assign w=a; assign v=w; assign y=v;
    CELL u(.A(ONES),.B(X));
  endmodule)V");
  require(types->ports[1].signal.bits.size() == 3 && bit(*types, types->ports[1].signal.bits[0]) == "a[2]",
          "logic declarations changed connectivity");
  require(types->instances[0].ports[0].signal.bits == std::vector<NetId>(8, oneBit), "typed parameter lost fill width");
  require(types->instances[0].ports[1].signal.bits == std::vector<NetId>(32, zeroBit), "two-state int retained X bits");
  auto unsigned_parameter = readSv("module top #(parameter int unsigned N=-1)(output [63:0] y); assign y=N; endmodule");
  require(bit(*unsigned_parameter, unsigned_parameter->ports[0].signal.bits[0]) == "0"
              && bit(*unsigned_parameter, unsigned_parameter->ports[0].signal.bits[32]) == "1",
          "int unsigned sign extended");
  require(bool(parse("module top; wire logic,int; endmodule")), "SV mode changed Verilog-2005 identifiers");
  require(!parse("module top(output y);assign y='1;endmodule"), "SV literal accepted in Verilog-2005 mode");
  badSv("module top; wire int; endmodule", "identifier");
  badSv("module top(input a,b,output logic y);assign y=a;assign y=b;endmodule", "multiple");
  badSv("module top;logic y=1'b0;endmodule", "initializer");
  badSv("module top(inout var logic p);endmodule", "inout");
  auto inherited = readSv("module top(input logic a, logic b, output logic y);assign y=b;endmodule");
  require(inherited->ports[1].direction == Direction::input && bit(*inherited, inherited->ports[2].signal.bits[0]) == "b",
          "ANSI declaration failed to inherit direction");
  require(readSv("module top(logic a);endmodule")->ports[0].direction == Direction::inout,
          "first typed ANSI port did not default to inout");
  badSv("module top;assign v=1'b0;logic v;endmodule", "prior");
  badSv("module top #(parameter int logic N=0)();endmodule", "identifier");
  auto net = readSv("module top(input a,b,output wire logic y);assign y=a;assign y=b;endmodule");
  require(net->assignments.size() == 2, "wire logic lost legal multiple drivers");
}
void implicitConnections()
{
  require(readSv("module child;endmodule module top;child u(.*);endmodule")->instances.empty(), "empty interface wildcard failed");
  auto flat = readSv(R"V(module child(input [2:0] a, input clk, output [2:0] y, output spare);
    assign y=a; CELL u(.A(clk));
  endmodule
  module top(input [2:0] a,input clk,output logic [2:0] y);
    child c(.*, .spare());
  endmodule)V");
  require(flat->instances.size() == 1 && bit(*flat, flat->instances[0].ports[0].signal.bits[0]) == "clk",
          "wildcard scalar connection lost");
  require(bit(*flat, flat->ports[2].signal.bits[0]) == "a[2]", "wildcard vector connection lost");
  auto named = readSv("module child(input a,output y);assign y=a;endmodule module top(input a,output y);child u(.a,.y);endmodule");
  require(bit(*named, named->ports[1].signal.bits[0]) == "a", "implicit named connection lost");
  const std::string child = "module child(input [1:0] a);endmodule ";
  badSv(child + "module top;child u(.a);endmodule", "declared");
  badSv(child + "module top;child u(.a);wire [1:0] a;endmodule", "prior");
  badSv(child + "module top(input a);child u(.*);endmodule", "equivalent");
  badSv(child + "module top(input signed [1:0] a);child u(.a);endmodule", "equivalent");
  badSv(child + "module top(input [1:0] a);child u(.*,.*);endmodule", "duplicate");
  badSv(child + "module top(input [1:0] a);child u(.a,.a());endmodule", "duplicate");
  badSv("module top(input a);UNKNOWN u(.*);endmodule", "interface");
  badSv("module top(input a);UNKNOWN u(.a);endmodule", "interface");
  require(!parse("module top;CELL u(.a);endmodule"), "SV shorthand accepted in V2005");
  auto leaf = parseSv("module top(input a);CELL u(.a);endmodule");
  require(bool(leaf), "cannot parse leaf shorthand");
  const LibraryCell cell_interface{{{"a", PortShape{Direction::input, 1}}}};
  auto result = compile(*leaf.design, "top", [&](auto cell) -> const LibraryCell* { return cell == "CELL" ? &cell_interface : nullptr; });
  require(result && result.design->instances[0].ports[0].name == "a"
              && bit(*result.design, result.design->instances[0].ports[0].signal.bits[0]) == "a",
          "library shorthand not bound");
}
void blackboxInterfaces()
{
  const auto flat = read(R"V((* blackbox *) module CELL #(parameter W=3)(input [W-1:0] A,output Y);endmodule
    (* blackbox *) module UNUSED(input A);endmodule
    module top(input [2:0] a,output y);CELL u(a,y);endmodule)V");
  require(flat->instances.size() == 1 && flat->instances[0].type == "CELL", "blackbox was expanded away");
  require(flat->instances[0].ports[0].name == "A" && flat->instances[0].ports[0].signal.bits.size() == 3,
          "blackbox interface did not resolve positional connections");
  auto parsed = parse("(* blackbox *) module UNUSED;endmodule module top;endmodule");
  require(parsed && compile(*parsed.design), "unused blackbox made auto-top ambiguous");
  require(read("(* blackbox=0 *) module child(input a,output y);assign y=a;endmodule module top(input a,output y);child u(a,y);endmodule")
              ->instances.empty(),
          "disabled blackbox attribute suppressed real hierarchy");
}
void variablePortSemantics()
{
  badSv(
      "module child(input var logic a,output y);assign a=1'b1;assign y=a;endmodule "
      "module top(output y);child u(.a(),.y(y));endmodule",
      "input variable");
  badSv("module child(input var logic a);assign a=1'b1;endmodule module top;child u();endmodule", "input variable");
  auto open = readSv(
      "module child(input var logic a,output y);assign y=a;endmodule "
      "module top(output y);child u(.a(),.y(y));endmodule");
  require(bit(*open, open->ports[0].signal.bits[0]) == "x", "unconnected input variable lost its default X value");
  auto omitted = readSv(
      "module child(input var logic a,output y);assign y=a;endmodule "
      "module top(output y);child u(.y(y));endmodule");
  require(bit(*omitted, omitted->ports[0].signal.bits[0]) == "x", "omitted input variable lost its default X value");
  auto undriven = readSv("module top(output logic [1:0] y);assign y[0]=1'b1;endmodule");
  require(bit(*undriven, undriven->ports[0].signal.bits[0]) == "x" && bit(*undriven, undriven->ports[0].signal.bits[1]) == "1",
          "partially driven variable lost per-bit initialization");
  auto connected = readSv(
      "module child(input var logic a,output logic y);assign y=a;endmodule "
      "module top(input var logic a,output logic y);child u(.*);endmodule");
  require(bit(*connected, connected->ports[1].signal.bits[0]) == "a", "input variable binding was treated as an illegal assignment");
  for (const auto* tail : {"wire y;", "logic y;"})
    badSv(std::string("module top(y);output logic y;") + tail + "endmodule", "duplicate");
  badSv("module top(a);input logic a;wire a;endmodule", "duplicate");
  badSv("module child(a);logic a;input a;assign a=1'b1;endmodule module top;child u();endmodule", "input variable");
  badSv("module top(p);logic p;inout p;endmodule", "inout");
  badSv("(* blackbox *) module CELL(p);inout p;logic p;endmodule module top(inout p);CELL u(p);endmodule", "inout");
  auto split = readSv("module top(y);output y;logic y;assign y=1'b1;endmodule");
  require(bit(*split, split->ports[0].signal.bits[0]) == "1", "valid split variable port declaration rejected");
  badSv("module top #(parameter integer int X=0)();endmodule", "identifier");
}
void legalDriverRelations()
{
  auto parsed = parse("module top(input a,input b,output y); assign y=a; assign y=b; endmodule");
  require(bool(parsed), "legal multi-driver syntax rejected");
  auto result = compile(*parsed.design, "top");
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
void generatedStructures()
{
  auto flat = read(R"V(module child #(parameter W=1)(input [W-1:0] a); CELL u(.A(a)); endmodule
module top(input [7:0] a);
  parameter N=4;
  genvar i;
  generate for(i=0;i<N;i=i+1) begin:g
    localparam K=i*2;
    wire [1:0] lane=a[K+:2];
    if(i==0) begin:first child #(.W(2)) c(lane); end
    else begin:rest child #(.W(2)) c(lane); end
  end endgenerate
  case(N) 4: begin:chosen CELL k(.A(a[0])); end default: begin:unused MISSING k(); end endcase
endmodule)V");
  require(flat->instances.size() == 5, "generate selected wrong number of instances");
  require(flat->instances[0].name == "g[0]/first/c/u" && flat->instances[3].name == "g[3]/rest/c/u", "generate scope name lost");
  require(
      bit(*flat, flat->instances[0].ports[0].signal.bits[0]) == "a[1]" && bit(*flat, flat->instances[3].ports[0].signal.bits[1]) == "a[6]",
      "genvar/localparam selected wrong bits");
  auto array = read(
      "module c(input [1:0] a,input en); CELL leaf(.A(a),.E(en)); endmodule "
      "module top(input [7:0] a,input en);c u[0:3](a,en); endmodule");
  require(array->instances.size() == 4 && array->instances[0].name == "u[0]/leaf", "ascending array expansion failed");
  require(bit(*array, array->instances[0].ports[0].signal.bits[0]) == "a[7]"
              && bit(*array, array->instances[3].ports[0].signal.bits[1]) == "a[0]",
          "array port distribution reversed");
  require(bit(*array, array->instances[2].ports[1].signal.bits[0]) == "en", "array broadcast failed");
  auto overrides = read(
      "module c #(parameter W=1)(input [W-1:0] a); CELL u(.A(a)); endmodule "
      "module top(input [3:0] a); c #(.W(2)) inst(a); defparam inst.W=4; endmodule");
  require(overrides->instances[0].ports[0].signal.bits.size() == 4, "defparam must override instance parameter before port widths");
  auto direct = read("module top;if(0) begin:g CELL c();end else if(1) begin:g CELL c();end endmodule");
  require(direct->instances[0].name == "g/c", "direct conditional generate introduced an extra scope");
  auto numbering = read("module top;if(0) begin:unused CELL a();end if(0) CELL b();else if(1) CELL c();endmodule");
  require(numbering->instances[0].name == "genblk2/c", "direct conditional lost its enclosing generate number");
  bad("module top;wire g;if(0) begin:a end else if(0) begin:g end endmodule", "conflicting");
  auto collision = read(
      "module sub #(parameter N=1)();wire [N-1:0] y;endmodule "
      "module top;wire genblk1;if(1) begin sub s();defparam s.N=2;end endmodule");
  require(collision->buses[0].name == "genblk01/s/y" && collision->buses[0].bits.size() == 2,
          "unnamed block collision changed between parameter discovery and final elaboration");
  auto case_width = read("module top;case(4'd15+4'd1) 5'd16:begin:yes CELL c();end default:begin:no CELL c();end endcase endmodule");
  require(case_width->instances[0].name == "yes/c", "generate case lost expression width context");
  auto own_override = read("module top;parameter N=0;wire [8/N:0] y;defparam N=2;endmodule");
  require(own_override->nets.size() == 5, "defparam applied after net range evaluation");
  auto sibling = read(
      "module sub #(parameter N=0)();if(N==0) BAD #(.P(1)) bad();else CELL good();endmodule "
      "module setter;defparam s.N=1;endmodule module top;sub s();setter t();endmodule");
  require(sibling->instances.size() == 1 && sibling->instances[0].type == "CELL", "sibling defparam was not resolved before generate");
  auto redefined_genvar = read(
      "module top;genvar i;for(i=0;i<1;i=i+1) begin:g if(1) begin:h genvar i;"
      "for(i=0;i<1;i=i+1) begin:j CELL c();end end end endmodule");
  require(redefined_genvar->instances[0].name == "g[0]/h/j[0]/c", "fresh local genvar failed to shadow inherited localparam");
  bad("module top;genvar i;if(1) begin:g wire i;for(i=0;i<1;i=i+1) begin:h CELL c();end end endmodule", "genvar");
  auto shadow = read("module top(input p);if(1) begin:g localparam p=1'b1;CELL u(.A(p));end endmodule");
  require(bit(*shadow, shadow->instances[0].ports[0].signal.bits[0]) == "1", "generate localparam must shadow outer net");
  auto width_override = read(
      "module c #(parameter [8:0] P=0)(output [8:0] y);assign y=P;endmodule "
      "module top(output [8:0] y);c u(y);defparam u.P=8'd255+8'd1;endmodule");
  require(bit(*width_override, width_override->ports[0].signal.bits[0]) == "1", "defparam expression lost destination width context");
  auto casts = read("module top(input [3:0] a);wire [7:0] b=$signed(a);CELL u(.A(b));endmodule");
  require(bit(*casts, casts->instances[0].ports[0].signal.bits[0]) == "a[3]", "signed net cast lost sign extension");
  bad("module top;genvar i;for(i=0;i<2;i=i) begin:g CELL u();end endmodule", "genvar");
  bad("module top;for(i=0;i<2;i=i+1) begin:g CELL u();end endmodule", "genvar");
  bad("module c #(parameter W=1)();endmodule module top;c u();defparam u.NOPE=2;endmodule", "defparam");
  bad("module c(input [1:0] a);endmodule module top(input [4:0] a);c u[0:2](a);endmodule", "array");
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
  const LibraryCell cell_interface{{{"A", PortShape{Direction::input, 9}}}};
  auto library = compile(*parsed.design, "top", [&](std::string_view) { return &cell_interface; });
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
  auto first = compile(*parsed.design, "top");
  auto second = compile(*parsed.design);
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
  bad("`ifdef X\nmodule top; endmodule", "conditional");
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
void hierarchyContract()
{
  auto parsed = parseSv(R"(
    module child #(parameter N=2)(input logic [N-1:0] a, output logic [N-1:0] y);
      if (N > 1) begin : chosen
        wire [N-1:0] link;
        assign link = a;
        assign y = link;
      end
    endmodule
    module top(input [3:0] a, output [3:0] y);
      child #(.N(4)) u(.a(a), .y(y));
    endmodule
  )");
  require(bool(parsed), "hierarchy fixture parses");
  auto attribute_syntax = parse("(* blackbox = UNKNOWN *) module CELL();endmodule");
  require(bool(attribute_syntax), "syntax records attribute expressions without evaluating them");
  auto attribute_bound = bindHierarchy(*attribute_syntax.design, "CELL");
  require(!attribute_bound, "semantic binding must validate blackbox attribute constants");
  const auto& written = parsed.design->modules[0].scope.declarations;
  require(written[0].data_type == DeclaredDataType::logic && written[0].object_kind == DeclaredObjectKind::implicit,
          "syntax preserves written type without deciding input net defaults");
  auto bound = bindHierarchy(*parsed.design, "top");
  require(bool(bound), bound ? "" : bound.diagnostics.front().text());
  const auto& root = *bound.design->top;
  const auto& child = *root.expansions.at(&root.syntax.statements[0])[0].scope;
  require(child.symbols.at("a").type.width() == 4 && !child.symbols.at("a").type.is_variable,
          "parameter specialization precedes port width and kind resolution");
  require(child.symbols.at("y").type.is_variable && child.parent == nullptr,
          "output logic is variable and module constants do not inherit caller scope");
  const auto& block = *child.expansions.at(&child.syntax.statements[0])[0].scope;
  require(block.parent == &child && block.prefix == "u/chosen/" && block.symbols.at("link").type.width() == 4,
          "generate creates a lexical scope after parameter resolution");
  auto flat = lower(*bound.design);
  require(bool(flat) && flat.design->ports.size() == 2 && flat.design->nets.size() == 20,
          "connectivity is produced separately from bound scopes");
}
void interfaceSpecializations()
{
  auto parsed = parse(R"(
    (* blackbox *) module A #(parameter [0:0] P=1'b1)(input p);endmodule
    (* blackbox *) module \A:u1[0:0] (input [1:0] p);endmodule
    module top(input [1:0] a);
      A u(a[0]);
      A same(a[1]);
      \A:u1[0:0] v(a);
    endmodule
  )");
  require(bool(parsed), "interface cache fixture parses");
  auto bound = bindHierarchy(*parsed.design, "top");
  require(bool(bound), bound ? "" : bound.diagnostics.front().text());
  const auto& root = *bound.design->top;
  const auto& statements = root.syntax.statements;
  const auto& first = root.expansions.at(&statements[0])[0].interface;
  const auto& same = root.expansions.at(&statements[1])[0].interface;
  const auto& second = root.expansions.at(&statements[2])[0].interface;
  require(first == same, "identical library interfaces must share their specialization");
  require(first != second && second->symbols.at("p").type.width() == 2, "escaped module name collided with a parameter specialization key");
  auto flat = lower(*bound.design);
  require(flat && flat.design->nets.size() == 2 && flat.design->instances[2].ports[0].signal.bits.size() == 2,
          "library interfaces must not allocate synthetic nets or truncate distinct interface widths");
}
int main()
{
  try {
    hierarchyContract();
    interfaceSpecializations();
    variablePortSemantics();
    systemVerilogValuesAndTypes();
    implicitConnections();
    blackboxInterfaces();
    legalDriverRelations();
    hierarchy();
    values();
    identifiersAndOwnership();
    diagnostics();
    contextValues();
    generatedStructures();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

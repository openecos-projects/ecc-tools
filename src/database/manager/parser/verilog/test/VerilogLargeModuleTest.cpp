// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "VerilogFrontend.hh"
#include "VerilogSyntax.hh"
using namespace idb::verilog;
namespace {
void require(bool value, const std::string& message)
{
  if (!value)
    throw std::runtime_error(message);
}
void flat()
{
  constexpr size_t count = 100000;
  std::ostringstream text;
  text << "module top(input a);\n";
  for (size_t i = 0; i < count; ++i)
    text << "wire n" << i << "; CELL u" << i << "(.A(n" << i << ")); assign n" << i << "=a;\n";
  text << "endmodule\n";
  auto parsed = parse(text.str());
  require(bool(parsed), parsed ? "" : parsed.diagnostics.front().text());
  require(parsed.design->modules[0].scope.declarations.size() == count + 1, "large declaration list truncated");
  require(parsed.design->modules[0].scope.statements.size() == count * 2, "large statement list truncated");
  auto result = compile(*parsed.design, "top");
  require(bool(result), result ? "" : result.diagnostics.front().text());
  require(result.design->instances.size() == count, "large instance list truncated");
  for (size_t i : {size_t(0), count / 2, count - 1}) {
    const auto& cell = result.design->instances[i];
    require(cell.name == "u" + std::to_string(i), "instance source order changed");
    require(result.design->assignments[i].target == cell.ports[0].signal.bits[0] && result.design->assignments[i].source == 0,
            "high fanout alias lost");
  }
}
void hierarchy()
{
  constexpr size_t count = 150000;
  std::ostringstream text;
  text << "module child(input a); CELL c(.A(a)); endmodule module top(input a);\n";
  for (size_t i = 0; i < count; ++i)
    text << "child u" << i << "(.a(a));\n";
  text << "endmodule\n";
  auto parsed = parse(text.str());
  require(bool(parsed), "wide hierarchy parse failed");
  auto result = compile(*parsed.design);
  require(bool(result), result ? "" : result.diagnostics.front().text());
  require(result.design->instances.size() == count, "wide hierarchy flattened incompletely");
  require(result.design->instances.back().name == "u149999/c", "hierarchy source order changed");
  require(result.design->assignments.back().target == result.design->instances.back().ports[0].signal.bits[0]
              && result.design->assignments.back().source == 0,
          "hierarchy binding lost");
  require(parsed.design->modules[0].scope.statements.size() == 1, "flatten mutated child AST");
}
}  // namespace
int main()
{
  try {
    flat();
    hierarchy();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

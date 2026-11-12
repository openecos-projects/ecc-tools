// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include <iostream>
#include <stdexcept>

#include "VerilogFrontend.hh"
#include "VerilogSyntax.hh"
using namespace idb::verilog;
int main()
{
  try {
    auto dollar = parse(R"(module top; \$cell u(); endmodule)");
    if (!dollar)
      throw std::runtime_error("escaped dollar master rejected: " + dollar.diagnostics.front().text());
    auto result = parse("module top; wire \\bus[0].n,(); , \\a\\b ; CELL \\u; (.\\p) (\\bus[0].n,(); )); endmodule");
    if (!result)
      throw std::runtime_error(result.diagnostics.front().text());
    const auto& module = result.design->modules[0];
    if (module.scope.declarations[0].name != "bus[0].n,();" || module.scope.declarations[1].name != "a\\b")
      throw std::runtime_error("escaped punctuation or internal backslash changed");
    auto flat = compile(*result.design, "top");
    if (!flat || flat.design->instances[0].name != "u;" || flat.design->instances[0].ports[0].name != "p)")
      throw std::runtime_error("escaped cell or port name changed");
    auto collision = compile(R"(module top; wire \bus[0] ; wire [0:0] bus;
CELL a(.A(\bus[0] )); CELL b(.A(bus[0])); endmodule)");
    if (!collision)
      throw std::runtime_error("escaped scalar and bus bit rejected: " + collision.diagnostics.front().text());
    const auto scalar = collision.design->instances[0].ports[0].signal.bits[0];
    const auto bus_bit = collision.design->instances[1].ports[0].signal.bits[0];
    if (scalar == bus_bit || collision.design->nets[scalar].name == collision.design->nets[bus_bit].name)
      throw std::runtime_error("distinct scalar and bus bit were merged");
    for (const auto* invalid : {"module top; wire \\ ; endmodule", "module top; wire $abc; endmodule", "module top; wire \\abc"})
      if (parse(invalid))
        throw std::runtime_error("invalid identifier accepted");
    if (!parse("module top; wire a$0; endmodule"))
      throw std::runtime_error("valid simple identifier rejected");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include <iostream>
#include <stdexcept>

#include "VerilogElaborator.hh"
#include "VerilogParser.hh"
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
    if (module.declarations[0].name != "bus[0].n,();" || module.declarations[1].name != "a\\b")
      throw std::runtime_error("escaped punctuation or internal backslash changed");
    auto flat = elaborate(*result.design, "top");
    if (!flat || flat.design->instances[0].name != "u;" || flat.design->instances[0].ports[0].name != "p)")
      throw std::runtime_error("escaped cell or port name changed");
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

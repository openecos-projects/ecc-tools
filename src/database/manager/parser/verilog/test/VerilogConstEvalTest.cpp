// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include <iostream>
#include <stdexcept>

#include "VerilogConstEval.hh"
#include "VerilogParser.hh"
using namespace idb::verilog;
namespace {
void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}
}  // namespace
int main()
{
  try {
    auto parsed = parse(
        "module top; parameter SUM=8'd255+8'd1; parameter DEPENDENT=P+8'd1; parameter [0:7] BITS=8'h96; parameter SLICE=BITS[2+:3]; "
        "endmodule");
    require(bool(parsed), "constant fixture parse failed");
    detail::ConstEvaluator evaluator(*parsed.design);
    const auto& parameters = parsed.design->modules[0].parameters;
    detail::Constants values;
    require(evaluator.requiredConstant(parameters[0].value, values).bits() == "00000000", "self-determined width changed");
    require(evaluator.requiredConstant(parameters[0].value, values, 9).bits() == "100000000", "context width failed to reach operands");
    require(!evaluator.constant(parameters[1].value, values), "unbound parameter became a value");
    values.emplace("P", detail::Constant{BitVector::parse("8'd1"), {}});
    require(evaluator.requiredConstant(parameters[1].value, values).integer() == 2, "parameter lookup failed");
    values.at("P").value = BitVector::parse("8'd7");
    require(evaluator.requiredConstant(parameters[1].value, values).integer() == 8, "value cache leaked across parameter environments");
    auto range = evaluator.bounds(parameters[2].range, values);
    values.emplace("BITS", detail::Constant{evaluator.requiredConstant(parameters[2].value, values), range});
    require(evaluator.requiredConstant(parameters[3].value, values).bits() == "010", "ascending constant selection reversed");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

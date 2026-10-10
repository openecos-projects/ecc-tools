// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include <iostream>
#include <stdexcept>

#include "VerilogExpression.hh"
#include "VerilogSyntax.hh"
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
    auto functions = parse(
        "module top; parameter A=$clog2(0), B=$clog2(1), C=$clog2(17), D=$clog2(128'h100000000000000000), "
        "E=$signed(4'hf), F=$unsigned(4'shf), G=$clog2(4'bx001), H=$clog2(8'd255+8'd1); endmodule");
    require(bool(functions), "constant system function parsing failed");
    detail::ExpressionEvaluator calls(*functions.design);
    const auto& f = functions.design->modules[0].scope.parameters;
    require(calls.requiredConstant(f[0].value, {}).integer() == 0, "clog2(0)");
    require(calls.requiredConstant(f[1].value, {}).integer() == 0, "clog2(1)");
    require(calls.requiredConstant(f[2].value, {}).integer() == 5, "clog2 rounds upward");
    require(calls.requiredConstant(f[3].value, {}).integer() == 68, "clog2 wide argument truncated");
    require(calls.requiredConstant(f[4].value, {}, 8).bits() == "11111111", "signed cast context");
    require(calls.requiredConstant(f[5].value, {}, 8).bits() == "00001111", "unsigned cast context");
    require(calls.requiredConstant(f[6].value, {}).bits() == std::string(32, 'x'), "clog2 unknown argument");
    require(calls.requiredConstant(f[7].value, {}, 64).integer() == 0, "function argument must be self-determined");
    require(!parse("module top; parameter A=$clog2(1,2); endmodule"), "wrong function arity accepted");
    auto parsed = parse(
        "module top; parameter SUM=8'd255+8'd1; parameter DEPENDENT=P+8'd1; parameter [0:7] BITS=8'h96; parameter SLICE=BITS[2+:3]; "
        "endmodule");
    require(bool(parsed), "constant fixture parse failed");
    detail::ExpressionEvaluator evaluator(*parsed.design);
    const auto& parameters = parsed.design->modules[0].scope.parameters;
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

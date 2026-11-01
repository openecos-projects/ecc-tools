// SPDX-License-Identifier: MulanPSL-2.0
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "VerilogFrontend.hh"
#include "VerilogImportPlan.hh"

using namespace idb::verilog;

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition)
    throw std::runtime_error(message);
}

ImportCellInterface bufferInterface()
{
  ImportCellInterface result;
  result.ports.emplace("A", ImportCellPort{{"A"}, Direction::input, {}});
  result.ports.emplace("Y", ImportCellPort{{"Y"}, Direction::output, {}});
  result.semantic.ports.emplace("A", PortShape{Direction::input, 1});
  result.semantic.ports.emplace("Y", PortShape{Direction::output, 1});
  return result;
}

const ImportCellInterface* lookup(std::string_view cell)
{
  static const ImportCellInterface buffer = bufferInterface();
  return cell == "BUF" ? &buffer : nullptr;
}

ImportPlanResult plan(std::string_view source)
{
  auto flat = compile(source, "plan.v", "top", {}, [](std::string_view cell) -> const LibraryCell* {
    const auto* interface = lookup(cell);
    return interface ? &interface->semantic : nullptr;
  });
  require(bool(flat), flat ? "" : flat.diagnostics.front().text());
  return makeImportPlan(*flat.design, lookup);
}

const PlannedNet& net(const VerilogImportPlan& plan, std::string_view name)
{
  const auto found = std::find_if(plan.nets.begin(), plan.nets.end(), [=](const PlannedNet& entry) { return entry.name == name; });
  require(found != plan.nets.end(), "planned net is missing: " + std::string(name));
  return *found;
}

bool sameAliasMembers(std::vector<std::string> actual, std::vector<std::string> expected)
{
  std::sort(actual.begin(), actual.end());
  std::sort(expected.begin(), expected.end());
  return actual == expected;
}

void aliasChainHasStableRepresentativeAndAliases()
{
  const std::string first = R"V(module top(input a, output z);
    wire left, middle, physical;
    assign z = left;
    assign left = middle;
    assign middle = physical;
    BUF u(.A(a), .Y(physical));
  endmodule)V";
  const std::string reordered = R"V(module top(input a, output z);
    wire physical, middle, left;
    assign middle = physical;
    assign left = middle;
    assign z = left;
    BUF u(.A(a), .Y(physical));
  endmodule)V";

  auto first_result = plan(first);
  auto reordered_result = plan(reordered);
  require(first_result && reordered_result, "alias-chain import plan failed");
  const std::vector<std::string> expected{"left", "middle", "physical", "z"};
  const auto& first_net = net(*first_result.plan, "physical");
  const auto& reordered_net = net(*reordered_result.plan, "physical");
  require(sameAliasMembers(first_net.aliases, expected) && sameAliasMembers(reordered_net.aliases, expected),
          "declaration or assignment order changed an alias group's symbols");
  require(first_result.plan->alias_count == 3 && reordered_result.plan->alias_count == 3,
          "assignment alias count changed with source ordering");
}

void inputAliasUsesTheInternalPhysicalName()
{
  auto result = plan("module top(input a, output y); wire internal; assign internal = a; BUF u(.A(internal), .Y(y)); endmodule");
  require(bool(result), "input alias import plan failed");
  const auto input = std::find_if(result.plan->ports.begin(), result.plan->ports.end(),
                                  [](const PlannedPin& port) { return port.name == "a"; });
  require(input != result.plan->ports.end(), "input port is missing");
  const auto& internal = result.plan->nets.at(input->net);
  require(internal.name == "internal", "input assignment replaced the internal physical name");
  require(sameAliasMembers(internal.aliases, {"a", "internal"}), "input alias symbols were not retained");
}

void constantAliasesRetainOriginalSymbols()
{
  auto result = plan("module top(output y); supply0 gnd; wire tied; assign tied = gnd; assign y = tied; endmodule");
  require(bool(result), "constant alias import plan failed");
  const auto& zero = net(*result.plan, verilogZeroNet);
  require(zero.type == NetType::supply0, "constant plan net lost supply type");
  require(sameAliasMembers(zero.aliases, {"gnd", "tied", "y"}), "constant plan net lost merged source symbols");
}

void rejectDriverConflict()
{
  auto result = plan("module top(input a, input b, output y); assign y = a; assign y = b; endmodule");
  require(!result && !result.diagnostics.empty() && result.diagnostics.front().message.find("multiple or supply assignment drivers") != std::string::npos,
          "multiple directed drivers were accepted");
}

void rejectConstantConflict()
{
  auto result = plan(R"V(module low(inout supply0 p); endmodule
    module high(inout supply1 p); endmodule
    module top; wire n; low l(n); high h(n); endmodule)V");
  require(!result && !result.diagnostics.empty() && result.diagnostics.front().message.find("conflicting constant drivers") != std::string::npos,
          "conflicting constants were accepted");
}

void rejectUnmappedFourStateNet()
{
  auto result = plan("module top; wire unused; assign unused = 1'bx; endmodule");
  require(!result && !result.diagnostics.empty() && result.diagnostics.front().message.find("four-state x/z") != std::string::npos,
          "isolated x/z net was accepted");
}

}  // namespace

int main()
{
  try {
    aliasChainHasStableRepresentativeAndAliases();
    inputAliasUsesTheInternalPhysicalName();
    constantAliasesRetainOriginalSymbols();
    rejectDriverConflict();
    rejectConstantConflict();
    rejectUnmappedFourStateNet();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}

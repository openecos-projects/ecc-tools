// SPDX-License-Identifier: MulanPSL-2.0

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "ACAntennaRule.hpp"
#include "ACModel.hpp"
#include "ACViolation.hpp"
#include "ACViolationType.hpp"
#include "adapters/imj/antenna/AntennaGraph.hpp"
#include "adapters/imj/antenna/AntennaKernel.hpp"

namespace {

void configureModel(imj::ACModel& model)
{
  model.get_micron_dbu() = 1000;

  imj::ACAntennaRule rule;
  rule.layer_name = "M1";
  rule.layer_order = 0;
  rule.is_routing = true;
  rule.area_ratio = 2.0;
  model.get_rule_list().push_back(std::move(rule));
  imj::AntennaKernel::initializeRuleIndex(model);
}

imj::AntennaGraphInput makeGraph(int64_t width_dbu)
{
  imj::AntennaGraphInput input;
  input.net_name = "antenna_kernel_test";

  imj::AntennaGraphNode gate;
  gate.id = 0;
  gate.gate_area = 1.0;
  input.nodes.push_back(gate);

  imj::AntennaGraphNode conductor;
  conductor.id = 1;
  conductor.is_conductor = true;
  conductor.is_routing = true;
  conductor.shapes.push_back({.layer_order = 0, .rectangle = {.ll_x = 0, .ll_y = 0, .ur_x = width_dbu, .ur_y = 1000}});
  input.nodes.push_back(std::move(conductor));
  input.edges.push_back({.first = 0, .second = 1, .time = 0});
  return input;
}

bool expectPass()
{
  imj::ACModel model;
  configureModel(model);
  std::vector<imj::ACViolation> violations;
  imj::AntennaKernel::checkNet(model, makeGraph(1000), violations);
  if (!violations.empty()) {
    std::cerr << "expected one-square-micron conductor to pass a 2.0 PAR rule\n";
    return false;
  }
  return true;
}

bool expectViolation()
{
  imj::ACModel model;
  configureModel(model);
  std::vector<imj::ACViolation> violations;
  imj::AntennaKernel::checkNet(model, makeGraph(3000), violations);
  if (violations.size() != 1u || violations.front().type != imj::ACViolationType::kAntennaPar
      || violations.front().layer_name != "M1") {
    std::cerr << "expected three-square-micron conductor to emit one M1 PAR violation\n";
    return false;
  }

  constexpr double kTolerance = 1.0e-12;
  const auto& violation = violations.front();
  if (std::abs(violation.ratio - 3.0) > kTolerance || std::abs(violation.threshold - 2.0) > kTolerance
      || std::abs(violation.lx) > kTolerance || std::abs(violation.ly) > kTolerance
      || std::abs(violation.hx - 3.0) > kTolerance || std::abs(violation.hy - 1.0) > kTolerance) {
    std::cerr << "unexpected violation geometry or ratio: ratio=" << violation.ratio << " threshold=" << violation.threshold
              << " bbox=(" << violation.lx << ',' << violation.ly << ',' << violation.hx << ',' << violation.hy << ")\n";
    return false;
  }
  return true;
}

}  // namespace

int main()
{
  return expectPass() && expectViolation() ? 0 : 1;
}

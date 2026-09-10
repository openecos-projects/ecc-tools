// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#include "ACViolation.hpp"
#include "AntennaEngine.hpp"
#include "AntennaFixer.hpp"
#include "AntennaResult.hpp"
#include "AntennaRuleEvaluator.hpp"
#include "IdbCellMaster.h"
#include "IdbEnum.h"
#include "RoutingContext.hpp"
#include "Utility.hpp"

int main()
{
  const std::vector<std::pair<double, double>> pwl{{0.0, 0.0}, {2.0, 10.0}};
  izh::ACThresholdPick pick = izh::AntennaRuleEvaluator::pickThreshold(5.0, 8.0, pwl, 1.0, true);
  if (!pick.available || !pick.is_diff) {
    return 1;
  }
  if (!izh::Utility::equalDoubleByError(pick.threshold, 5.0, ZH_ERROR)) {
    return 1;
  }

  izh::ACThresholdPick plain = izh::AntennaRuleEvaluator::pickThreshold(4.0, -1.0, {}, 0.0, false);
  if (!plain.available || plain.is_diff || !izh::Utility::equalDoubleByError(plain.threshold, 4.0, ZH_ERROR)) {
    return 1;
  }

  izh::ACViolation v;
  v.net_name = "n1";
  v.pin_name = "A";
  v.inst_name = "u1";
  v.layer_order = 2;
  v.type = izh::ACViolationType::kAntennaCutPar;
  if (v.pin_name != "A" || !izh::acViolationIsCut(v.type)) {
    return 1;
  }

  izh::RoutingContext ctx;
  izh::AntennaFixer fixer;
  if (fixer.classify(v, ctx) != izh::AFFixKind::kDiode) {
    return 1;
  }
  v.type = izh::ACViolationType::kAntennaPar;
  v.layer_order = 100000;
  if (fixer.classify(v, ctx) != izh::AFFixKind::kDiode) {
    return 1;
  }

  idb::IdbCellMaster master;
  master.set_type(idb::CellMasterType::kCoreAntenaCell);
  if (!master.is_antenna_cell()) {
    return 1;
  }
  master.set_type(idb::CellMasterType::kCore);
  if (master.is_antenna_cell()) {
    return 1;
  }

  izh::AntennaEngine::initInst();
  izh::AntennaEngine* inst = &ZHAE;
  izh::AntennaEngine::initInst();
  if (inst != &ZHAE) {
    return 1;
  }
  izh::AntennaEngine::destroyInst();
  return 0;
}

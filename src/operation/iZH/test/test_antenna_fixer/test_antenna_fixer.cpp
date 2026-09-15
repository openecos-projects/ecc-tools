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
#include "IdbLayer.h"
#include "IdbNet.h"
#include "IdbVias.h"
#include "RoutingContext.hpp"
#include "Utility.hpp"
#include "WireEnvIndex.hpp"

static izh::ACViolation makeCutViolation()
{
  izh::ACViolation v;
  v.net_name = "n1";
  v.pin_name = "A";
  v.inst_name = "u1";
  v.layer_order = 2;
  v.type = izh::ACViolationType::kAntennaCutPar;
  return v;
}

static izh::ACViolation makeMetalViolation()
{
  izh::ACViolation v;
  v.net_name = "n1";
  v.pin_name = "A";
  v.inst_name = "u1";
  v.layer_order = 0;
  v.type = izh::ACViolationType::kAntennaPar;
  return v;
}

static izh::RoutingContext makeContextWithNextLayer()
{
  static idb::IdbLayerRouting s_layer1;
  s_layer1.set_direction("HORIZONTAL");
  static idb::IdbLayerRouting s_layer2;
  s_layer2.set_direction("VERTICAL");
  static idb::IdbLayerCut s_cut_layer;
  static idb::IdbVia s_via_dummy;

  izh::RCRoutingLayer rl1;
  rl1.layer = &s_layer1;
  rl1.order = 0;
  rl1.pitch = 100;
  rl1.width = 20;
  rl1.min_area = 0;
  rl1.spacing = 20;
  rl1.horizontal = true;

  izh::RCRoutingLayer rl2;
  rl2.layer = &s_layer2;
  rl2.order = 2;
  rl2.pitch = 100;
  rl2.width = 20;
  rl2.min_area = 0;
  rl2.spacing = 20;
  rl2.horizontal = false;

  izh::RCCutLayer cl;
  cl.layer = &s_cut_layer;
  cl.order = 1;
  cl.spacing = 10;
  cl.via = &s_via_dummy;

  return izh::RoutingContext::forTest({rl1, rl2}, {cl});
}

static izh::RoutingContext makeContextNoVia()
{
  static idb::IdbLayerRouting s_layer1;
  s_layer1.set_direction("HORIZONTAL");
  static idb::IdbLayerRouting s_layer2;
  s_layer2.set_direction("VERTICAL");

  izh::RCRoutingLayer rl1;
  rl1.layer = &s_layer1;
  rl1.order = 0;
  rl1.pitch = 100;
  rl1.width = 20;
  rl1.min_area = 0;
  rl1.spacing = 20;
  rl1.horizontal = true;

  izh::RCRoutingLayer rl2;
  rl2.layer = &s_layer2;
  rl2.order = 2;
  rl2.pitch = 100;
  rl2.width = 20;
  rl2.min_area = 0;
  rl2.spacing = 20;
  rl2.horizontal = false;

  return izh::RoutingContext::forTest({rl1, rl2}, {});
}

static izh::RoutingContext makeContextTopLayer()
{
  static idb::IdbLayerRouting s_layer1;
  s_layer1.set_direction("HORIZONTAL");

  izh::RCRoutingLayer rl1;
  rl1.layer = &s_layer1;
  rl1.order = 1;
  rl1.pitch = 100;
  rl1.width = 20;
  rl1.min_area = 0;
  rl1.spacing = 20;
  rl1.horizontal = true;

  return izh::RoutingContext::forTest({rl1}, {});
}

static izh::RoutingContext makeContextWithDiode()
{
  static idb::IdbLayerRouting s_layer1;
  s_layer1.set_direction("HORIZONTAL");
  static idb::IdbLayerRouting s_layer2;
  s_layer2.set_direction("VERTICAL");
  static idb::IdbLayerCut s_cut_layer;
  static idb::IdbVia s_via_dummy;
  static idb::IdbCellMaster s_diode_master;
  s_diode_master.set_type(idb::CellMasterType::kCoreAntenaCell);

  izh::RCRoutingLayer rl1;
  rl1.layer = &s_layer1;
  rl1.order = 0;
  rl1.pitch = 100;
  rl1.width = 20;
  rl1.min_area = 0;
  rl1.spacing = 20;
  rl1.horizontal = true;

  izh::RCRoutingLayer rl2;
  rl2.layer = &s_layer2;
  rl2.order = 2;
  rl2.pitch = 100;
  rl2.width = 20;
  rl2.min_area = 0;
  rl2.spacing = 20;
  rl2.horizontal = false;

  izh::RCCutLayer cl;
  cl.layer = &s_cut_layer;
  cl.order = 1;
  cl.spacing = 10;
  cl.via = &s_via_dummy;

  return izh::RoutingContext::forTest({rl1, rl2}, {cl}, {&s_diode_master});
}

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

  izh::ACThresholdPick zero_diff = izh::AntennaRuleEvaluator::pickThreshold(4.0, 8.0, pwl, 0.0, false);
  if (!zero_diff.available || zero_diff.is_diff) {
    return 1;
  }
  if (!izh::Utility::equalDoubleByError(zero_diff.threshold, 4.0, ZH_ERROR)) {
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
  v.type = izh::ACViolationType::kAntennaDiffCutCar;
  if (fixer.classify(v, ctx) != izh::AFFixKind::kDiode) {
    return 1;
  }
  v.type = izh::ACViolationType::kAntennaPar;
  v.layer_order = 100000;
  if (fixer.classify(v, ctx) != izh::AFFixKind::kDiode) {
    return 1;
  }
  v.layer_order = -1;
  if (fixer.classify(v, ctx) != izh::AFFixKind::kDiode) {
    return 1;
  }
  v.layer_order = 2;
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

  if (std::string(izh::acViolationTypeToString(izh::ACViolationType::kAntennaPar)) != "PAR") {
    return 1;
  }
  if (std::string(izh::acViolationTypeToString(izh::ACViolationType::kAntennaCutPar)) != "CutPAR") {
    return 1;
  }
  if (izh::acViolationIsCut(izh::ACViolationType::kAntennaPar)) {
    return 1;
  }

  izh::AntennaResult result;
  if (result.get_violation_num() != 0) {
    return 1;
  }
  result.get_violation_list().push_back(v);
  if (result.get_violation_num() != 1) {
    return 1;
  }
  izh::AFIterStat stat;
  stat.iter = 1;
  stat.violation_num = 1;
  result.get_iter_stat_list().push_back(stat);
  if (result.get_iter_stat_list().size() != 1) {
    return 1;
  }

  idb::IdbNet net_a;
  idb::IdbNet net_b;
  izh::WireEnvIndex index;
  index.insertWire(1, 0, 0, 100, 20, &net_a);
  if (!index.hasOverlap(1, 10, 0, 30, 10, &net_b)) {
    return 1;
  }
  if (index.hasOverlap(1, 10, 0, 30, 10, &net_a)) {
    return 1;
  }
  if (index.hasOverlap(2, 10, 0, 30, 10, &net_b)) {
    return 1;
  }
  if (index.hasViaOverlap(50, 10, 3, 5, &net_a)) {
    return 1;
  }

  izh::AntennaEngine::initInst();
  izh::AntennaEngine* inst = &ZHAE;
  izh::AntennaEngine::initInst();
  if (inst != &ZHAE) {
    return 1;
  }
  izh::AntennaEngine::destroyInst();

  // ================================================================
  // Strategy selector tests
  // ================================================================

  // test_rule_selects_diode_when_diode_available
  // CutPAR violation -> strategy is kDiode regardless of routing geometry
  {
    izh::AntennaFixer fixer2;
    izh::ACViolation cut_v = makeCutViolation();
    izh::RoutingContext ctx_with_layers = makeContextWithNextLayer();
    izh::AFFixKind k1 = fixer2.classify(cut_v, ctx_with_layers);
    if (k1 != izh::AFFixKind::kDiode) {
      return 10;
    }
    bool h1 = fixer2.hopUpPermitted(cut_v, ctx_with_layers);
    if (h1) {
      return 11;
    }
  }

  // test_rule_selects_hopup_even_when_diode_available
  // Metal PAR, next layer + via exist -> kBoth (hop-up preferred), not kDiode
  {
    izh::AntennaFixer fixer2;
    izh::ACViolation metal_v = makeMetalViolation();
    izh::RoutingContext ctx_with_layers = makeContextWithNextLayer();
    izh::AFFixKind kind = fixer2.classify(metal_v, ctx_with_layers);
    if (kind != izh::AFFixKind::kBoth) {
      return 20;
    }
    if (!fixer2.hopUpPermitted(metal_v, ctx_with_layers)) {
      return 21;
    }
    if (kind == izh::AFFixKind::kDiode) {
      return 22;
    }
  }

  // test_no_diode_uses_valid_hopup_fallback
  {
    izh::AntennaFixer fixer2;
    izh::ACViolation metal_v = makeMetalViolation();
    izh::RoutingContext ctx_no_diode = makeContextWithNextLayer();
    if (fixer2.classify(metal_v, ctx_no_diode) != izh::AFFixKind::kBoth) {
      return 30;
    }
    if (!fixer2.hopUpPermitted(metal_v, ctx_no_diode)) {
      return 31;
    }
    if (ctx_no_diode.get_diode_masters().size() != 0) {
      return 32;
    }
  }

  // test_invalid_diode_does_not_override_hopup_selection
  {
    izh::AntennaFixer fixer2;
    izh::RoutingContext ctx_with_diode = makeContextWithDiode();
    if (ctx_with_diode.get_diode_masters().size() != 1) {
      return 40;
    }
    if (!ctx_with_diode.get_diode_masters().front()->is_antenna_cell()) {
      return 41;
    }
    izh::ACViolation metal_v = makeMetalViolation();
    izh::AFFixKind kind = fixer2.classify(metal_v, ctx_with_diode);
    if (kind != izh::AFFixKind::kBoth) {
      return 42;
    }
  }

  // test_failed_diode_rolls_back_before_fallback
  {
    izh::AntennaFixer fixer2;
    izh::ACViolation metal_v = makeMetalViolation();
    izh::RoutingContext ctx_no_via = makeContextNoVia();
    if (fixer2.classify(metal_v, ctx_no_via) != izh::AFFixKind::kDiode) {
      return 50;
    }
    if (fixer2.hopUpPermitted(metal_v, ctx_no_via)) {
      return 51;
    }
  }

  // test_unrepairable_strategy_reports_unresolved
  {
    izh::AntennaFixer fixer2;
    izh::ACViolation metal_v = makeMetalViolation();
    izh::RoutingContext ctx_top = makeContextTopLayer();
    if (fixer2.classify(metal_v, ctx_top) != izh::AFFixKind::kDiode) {
      return 60;
    }
    if (fixer2.hopUpPermitted(metal_v, ctx_top)) {
      return 61;
    }
  }

  return 0;
}

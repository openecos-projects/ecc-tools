// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************

#include "AntennaFixer.hpp"

#include "AntennaResult.hpp"
#include "Logger.hpp"
#include "Utility.hpp"

#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbLayer.h"
#include "IdbRegularWire.h"
#include "IdbTerm.h"
#include "IdbVias.h"

namespace izh {

AFFixKind AntennaFixer::classify(const ACViolation& violation, const RoutingContext& ctx) const
{
  if (acViolationIsCut(violation.type)) {
    return AFFixKind::kDiode;
  }
  if (violation.layer_order < 0 || violation.layer_order >= ctx.get_top_routing_order()) {
    return AFFixKind::kDiode;
  }
  const RCRoutingLayer* next = ctx.findNextRouting(violation.layer_order);
  if (next == nullptr || ctx.findViaBetween(violation.layer_order, next->order) == nullptr) {
    return AFFixKind::kDiode;
  }
  return AFFixKind::kBoth;
}

bool AntennaFixer::hopUpPermitted(const ACViolation& violation, const RoutingContext& ctx) const
{
  if (acViolationIsCut(violation.type)) {
    return false;
  }
  if (violation.layer_order < 0 || violation.layer_order >= ctx.get_top_routing_order()) {
    return false;
  }
  const RCRoutingLayer* next = ctx.findNextRouting(violation.layer_order);
  return next != nullptr && ctx.findViaBetween(violation.layer_order, next->order) != nullptr;
}

bool AntennaFixer::applySelected(idb::IdbDesign* design, idb::IdbNet* net, const ACViolation& violation, const RoutingContext& ctx,
                                 WireEnvIndex& index, AFIterStat& stat, bool& logged_no_cell)
{
  AFFixKind kind = classify(violation, ctx);
  bool hop_ok = hopUpPermitted(violation, ctx);
  bool applied = false;

  if (kind == AFFixKind::kHopUp || kind == AFFixKind::kBoth) {
    applied = applyHopUp(design, net, violation, ctx, index, true, stat);
    if (applied) {
      return true;
    }
  }

  if (kind == AFFixKind::kDiode || kind == AFFixKind::kBoth) {
    applied = applyDiode(design, net, violation, ctx, index, stat, logged_no_cell);
    if (applied) {
      return true;
    }
  }

  if (kind == AFFixKind::kHopUp && !applied && hop_ok) {
    applied = applyDiode(design, net, violation, ctx, index, stat, logged_no_cell);
    if (applied) {
      return true;
    }
  }

  if (kind == AFFixKind::kDiode && !applied && hop_ok && !ctx.get_diode_masters().empty()) {
    applied = applyHopUp(design, net, violation, ctx, index, true, stat);
    if (applied) {
      return true;
    }
  }

  if (!applied) {
    ZHLOG.warn(Loc::current(), "antenna violation unresolved: net=", violation.net_name, " pin=", violation.pin_name,
               " layer_order=", violation.layer_order);
  }
  return applied;
}

idb::IdbPin* AntennaFixer::findVictimPin(idb::IdbNet* net, const ACViolation& violation) const
{
  if (net == nullptr) {
    return nullptr;
  }
  auto match = [&](idb::IdbPin* pin, bool instance_pin) -> bool {
    if (pin == nullptr) {
      return false;
    }
    if (!violation.pin_name.empty() && pin->get_pin_name() != violation.pin_name) {
      return false;
    }
    if (instance_pin && !violation.inst_name.empty()) {
      if (pin->get_instance() == nullptr || pin->get_instance()->get_name() != violation.inst_name) {
        return false;
      }
    }
    return true;
  };

  if (net->get_instance_pin_list() != nullptr) {
    for (idb::IdbPin* pin : net->get_instance_pin_list()->get_pin_list()) {
      if (match(pin, true)) {
        return pin;
      }
    }
  }
  if (net->get_io_pins() != nullptr) {
    for (idb::IdbPin* pin : net->get_io_pins()->get_pin_list()) {
      if (match(pin, false)) {
        return pin;
      }
    }
  }
  if (net->get_instance_pin_list() != nullptr && !net->get_instance_pin_list()->get_pin_list().empty()) {
    return net->get_instance_pin_list()->get_pin_list().front();
  }
  if (net->get_io_pins() != nullptr && !net->get_io_pins()->get_pin_list().empty()) {
    return net->get_io_pins()->get_pin_list().front();
  }
  return nullptr;
}

bool AntennaFixer::pinCoord(idb::IdbPin* pin, int32_t& x, int32_t& y) const
{
  x = 0;
  y = 0;
  if (pin == nullptr) {
    return false;
  }
  if (pin->get_location() != nullptr && pin->get_location()->is_init()) {
    x = pin->get_location()->get_x();
    y = pin->get_location()->get_y();
    return true;
  }
  if (pin->get_average_coordinate() != nullptr) {
    x = pin->get_average_coordinate()->get_x();
    y = pin->get_average_coordinate()->get_y();
    return true;
  }
  return false;
}

const RCRoutingLayer* AntennaFixer::resolveStubLayer(idb::IdbPin* pin, const RoutingContext& ctx) const
{
  if (pin != nullptr) {
    for (idb::IdbLayerShape* shape : pin->get_port_box_list()) {
      if (shape == nullptr || shape->get_layer() == nullptr) {
        continue;
      }
      idb::IdbLayer* layer = shape->get_layer();
      for (const auto& rl : ctx.get_routing_layers()) {
        if (rl.layer == layer) {
          return &rl;
        }
      }
    }
  }
  return ctx.get_routing_layers().empty() ? nullptr : &ctx.get_routing_layers().front();
}

bool AntennaFixer::addViaAndStub(idb::IdbNet* net, int32_t x, int32_t y, const RCRoutingLayer& lower, const RCRoutingLayer& upper,
                                 idb::IdbVia* via, WireEnvIndex& index)
{
  if (net == nullptr || net->get_wire_list() == nullptr || via == nullptr || lower.layer == nullptr || upper.layer == nullptr) {
    return false;
  }
  idb::IdbRegularWire* wire = net->get_wire_list()->add_wire();
  wire->set_wire_state(idb::IdbWiringStatement::kRouted);

  idb::IdbRegularWireSegment* via_seg = new idb::IdbRegularWireSegment();
  via_seg->set_layer(upper.layer);
  via_seg->set_is_via(true);
  via_seg->add_point(x, y);
  via_seg->copy_via(via);
  if (!via_seg->get_via_list().empty() && via_seg->get_via_list().front() != nullptr) {
    via_seg->get_via_list().front()->set_coordinate(x, y);
    index.insertVia(via_seg->get_via_list().front(), net);
  }
  via_seg->set_layer_as_new();
  wire->add_segment(via_seg);

  int32_t stub = std::max(upper.width, 1);
  if (upper.min_area > 0 && stub > 0) {
    stub = std::max(stub, (upper.min_area + stub - 1) / stub);
  }
  int32_t x2 = x;
  int32_t y2 = y;
  if (upper.horizontal) {
    x2 = x + stub;
  } else {
    y2 = y + stub;
  }

  idb::IdbRegularWireSegment* stub_seg = new idb::IdbRegularWireSegment();
  stub_seg->set_layer(upper.layer);
  stub_seg->add_point(x, y);
  stub_seg->add_point(x2, y2);
  wire->add_segment(stub_seg);
  int32_t hw = std::max(upper.width, 1) / 2;
  index.insertWire(upper.order, std::min(x, x2) - hw, std::min(y, y2) - hw, std::max(x, x2) + hw, std::max(y, y2) + hw, net);
  return true;
}

bool AntennaFixer::tryPlaceVia(idb::IdbDesign* design, idb::IdbNet* net, int32_t x, int32_t y, const RCRoutingLayer& lower,
                               const RCRoutingLayer& upper, idb::IdbVia* via, const RoutingContext& ctx, WireEnvIndex& index)
{
  (void) design;
  if (!index.inDie(ctx, x, y)) {
    return false;
  }
  int32_t hw = std::max(lower.width, 1) / 2;
  int32_t pad = std::max(lower.spacing, hw);
  if (index.hasOverlap(lower.order, x - pad, y - pad, x + pad, y + pad, net)) {
    return false;
  }
  int32_t uhw = std::max(upper.width, 1) / 2;
  int32_t upad = std::max(upper.spacing, uhw);
  if (index.hasOverlap(upper.order, x - upad, y - upad, x + upad, y + upad, net)) {
    return false;
  }
  const RCCutLayer* cut = ctx.findCutBetween(lower.order, upper.order);
  int cut_order = cut != nullptr ? cut->order : (lower.order + 1);
  int32_t cut_spacing = cut != nullptr ? cut->spacing : pad;
  if (index.hasViaOverlap(x, y, cut_order, cut_spacing, net)) {
    return false;
  }
  return addViaAndStub(net, x, y, lower, upper, via, index);
}

bool AntennaFixer::applyHopUp(idb::IdbDesign* design, idb::IdbNet* net, const ACViolation& violation, const RoutingContext& ctx,
                              WireEnvIndex& index, bool allow_jog, AFIterStat& stat)
{
  const RCRoutingLayer* lower = ctx.findRoutingByOrder(violation.layer_order);
  const RCRoutingLayer* upper = ctx.findNextRouting(violation.layer_order);
  if (lower == nullptr || upper == nullptr) {
    ++stat.hop_up_rejected;
    return false;
  }
  idb::IdbVia* via = ctx.findViaBetween(lower->order, upper->order);
  if (via == nullptr) {
    ++stat.hop_up_rejected;
    return false;
  }
  idb::IdbPin* pin = findVictimPin(net, violation);
  int32_t x = 0;
  int32_t y = 0;
  if (!pinCoord(pin, x, y)) {
    x = static_cast<int32_t>((violation.lx + violation.hx) * 0.5 * ctx.get_micron_dbu());
    y = static_cast<int32_t>((violation.ly + violation.hy) * 0.5 * ctx.get_micron_dbu());
  }

  if (tryPlaceVia(design, net, x, y, *lower, *upper, via, ctx, index)) {
    ++stat.hop_up_applied;
    return true;
  }
  if (!allow_jog) {
    ++stat.hop_up_rejected;
    return false;
  }

  int32_t pitch = std::max(lower->pitch, 1);
  int32_t max_jog = ctx.get_max_jog();
  bool prefer_h = lower->horizontal;
  for (int32_t step = pitch; step <= max_jog; step += pitch) {
    std::vector<std::pair<int32_t, int32_t>> cands;
    if (prefer_h) {
      cands.emplace_back(x + step, y);
      cands.emplace_back(x - step, y);
    } else {
      cands.emplace_back(x, y + step);
      cands.emplace_back(x, y - step);
    }
    for (auto [cx, cy] : cands) {
      if (tryPlaceVia(design, net, cx, cy, *lower, *upper, via, ctx, index)) {
        int32_t hw = std::max(lower->width, 1) / 2;
        int32_t pad = std::max(lower->spacing, hw);
        if (!index.hasOverlap(lower->order, std::min(x, cx) - pad, std::min(y, cy) - pad, std::max(x, cx) + pad, std::max(y, cy) + pad,
                              net)) {
          idb::IdbRegularWire* wire = net->get_wire_list()->add_wire();
          wire->set_wire_state(idb::IdbWiringStatement::kRouted);
          idb::IdbRegularWireSegment* jog = new idb::IdbRegularWireSegment();
          jog->set_layer(lower->layer);
          jog->add_point(x, y);
          jog->add_point(cx, cy);
          wire->add_segment(jog);
          index.insertWire(lower->order, std::min(x, cx) - hw, std::min(y, cy) - hw, std::max(x, cx) + hw, std::max(y, cy) + hw, net);
          ++stat.jog_applied;
          ++stat.hop_up_applied;
          return true;
        }
      }
    }
  }
  ++stat.jog_rejected;
  ++stat.hop_up_rejected;
  return false;
}

idb::IdbTerm* AntennaFixer::pickDiodeTerm(idb::IdbCellMaster* master) const
{
  if (master == nullptr) {
    return nullptr;
  }
  idb::IdbTerm* fallback = nullptr;
  for (idb::IdbTerm* term : master->get_term_list()) {
    if (term == nullptr || term->is_pdn()) {
      continue;
    }
    if (term->has_antenna_diff_area()) {
      return term;
    }
    if (fallback == nullptr) {
      fallback = term;
    }
  }
  return fallback;
}

bool AntennaFixer::applyDiode(idb::IdbDesign* design, idb::IdbNet* net, const ACViolation& violation, const RoutingContext& ctx,
                              WireEnvIndex& index, AFIterStat& stat, bool& logged_no_cell)
{
  if (ctx.get_diode_masters().empty()) {
    if (!logged_no_cell) {
      ZHLOG.warn(Loc::current(), "No CORE ANTENNACELL found; leftover antenna violations will remain");
      logged_no_cell = true;
    }
    ++stat.diode_rejected;
    return false;
  }
  idb::IdbPin* pin = findVictimPin(net, violation);
  int32_t pin_x = 0;
  int32_t pin_y = 0;
  if (!pinCoord(pin, pin_x, pin_y)) {
    pin_x = static_cast<int32_t>((violation.lx + violation.hx) * 0.5 * ctx.get_micron_dbu());
    pin_y = static_cast<int32_t>((violation.ly + violation.hy) * 0.5 * ctx.get_micron_dbu());
  }

  for (idb::IdbCellMaster* master : ctx.get_diode_masters()) {
    idb::IdbTerm* term = pickDiodeTerm(master);
    if (term == nullptr) {
      continue;
    }
    int32_t x = 0;
    int32_t y = 0;
    idb::IdbOrient orient = idb::IdbOrient::kN_R0;
    if (!index.findFreeDiodeSite(ctx, pin_x, pin_y, static_cast<int32_t>(master->get_width()), static_cast<int32_t>(master->get_height()), x,
                                 y, orient)) {
      continue;
    }
    std::string inst_name = design->makeUniqueInstanceName("ANTENNA_DIODE");
    idb::IdbInstance* inst
        = design->createInstance(inst_name, master->get_name(), idb::IdbInstanceType::kDist, idb::IdbPlacementStatus::kPlaced, orient, x, y,
                                 idb::IdbCreatePolicy::kErrorIfExists);
    if (inst == nullptr) {
      continue;
    }
    if (!design->connectInstancePinToNet(inst_name, term->get_name(), net->get_net_name())) {
      design->removeInstanceSafe(inst_name);
      continue;
    }
    index.insertInstance(inst);

    idb::IdbPin* diode_pin = inst->get_pin_by_term(term->get_name());
    int32_t dx = x;
    int32_t dy = y;
    pinCoord(diode_pin, dx, dy);

    const RCRoutingLayer* stub_layer = resolveStubLayer(pin, ctx);
    if (stub_layer != nullptr && stub_layer->layer != nullptr && net->get_wire_list() != nullptr) {
      idb::IdbRegularWire* wire = net->get_wire_list()->add_wire();
      wire->set_wire_state(idb::IdbWiringStatement::kRouted);

      int32_t stub_x1 = dx;
      int32_t stub_y1 = dy;
      int32_t stub_x2 = pin_x;
      int32_t stub_y2 = pin_y;

      if (std::abs(dx - pin_x) > std::abs(dy - pin_y)) {
        stub_y2 = stub_y1;
      } else {
        stub_x2 = stub_x1;
      }
      if (stub_x1 != stub_x2 || stub_y1 != stub_y2) {
        idb::IdbRegularWireSegment* seg1 = new idb::IdbRegularWireSegment();
        seg1->set_layer(stub_layer->layer);
        seg1->add_point(dx, dy);
        seg1->add_point(stub_x2, stub_y2);
        seg1->set_layer_as_new();
        wire->add_segment(seg1);
        int32_t hw = std::max(stub_layer->width, 1) / 2;
        index.insertWire(stub_layer->order, std::min(dx, stub_x2) - hw, std::min(dy, stub_y2) - hw, std::max(dx, stub_x2) + hw,
                         std::max(dy, stub_y2) + hw, net);

        if (stub_x2 != pin_x || stub_y2 != pin_y) {
          idb::IdbRegularWireSegment* seg2 = new idb::IdbRegularWireSegment();
          seg2->set_layer(stub_layer->layer);
          seg2->add_point(stub_x2, stub_y2);
          seg2->add_point(pin_x, pin_y);
          seg2->set_layer_as_new();
          wire->add_segment(seg2);
          index.insertWire(stub_layer->order, std::min(stub_x2, pin_x) - hw, std::min(stub_y2, pin_y) - hw,
                           std::max(stub_x2, pin_x) + hw, std::max(stub_y2, pin_y) + hw, net);
        }
      } else {
        idb::IdbRegularWireSegment* stub = new idb::IdbRegularWireSegment();
        stub->set_layer(stub_layer->layer);
        stub->add_point(dx, dy);
        stub->add_point(pin_x, pin_y);
        stub->set_layer_as_new();
        wire->add_segment(stub);
        int32_t hw = std::max(stub_layer->width, 1) / 2;
        index.insertWire(stub_layer->order, std::min(dx, pin_x) - hw, std::min(dy, pin_y) - hw, std::max(dx, pin_x) + hw,
                         std::max(dy, pin_y) + hw, net);
      }
    }
    ++stat.diode_applied;
    return true;
  }
  ++stat.diode_rejected;
  return false;
}

}  // namespace izh

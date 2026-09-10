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
  return AFFixKind::kHopUp;
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

void AntennaFixer::pinCoord(idb::IdbPin* pin, int32_t& x, int32_t& y) const
{
  x = 0;
  y = 0;
  if (pin == nullptr) {
    return;
  }
  if (pin->get_location() != nullptr && pin->get_location()->is_init()) {
    x = pin->get_location()->get_x();
    y = pin->get_location()->get_y();
    return;
  }
  if (pin->get_average_coordinate() != nullptr) {
    x = pin->get_average_coordinate()->get_x();
    y = pin->get_average_coordinate()->get_y();
  }
}

bool AntennaFixer::addViaAndStub(idb::IdbNet* net, int32_t x, int32_t y, const RCRoutingLayer& lower, const RCRoutingLayer& upper,
                                 idb::IdbVia* via)
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
  if (index.hasViaOverlap(x, y, cut_order, cut_spacing)) {
    return false;
  }
  return addViaAndStub(net, x, y, lower, upper, via);
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
  pinCoord(pin, x, y);
  if (x == 0 && y == 0) {
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
        idb::IdbRegularWire* wire = net->get_wire_list()->add_wire();
        wire->set_wire_state(idb::IdbWiringStatement::kRouted);
        idb::IdbRegularWireSegment* jog = new idb::IdbRegularWireSegment();
        jog->set_layer(lower->layer);
        jog->add_point(x, y);
        jog->add_point(cx, cy);
        wire->add_segment(jog);
        ++stat.jog_applied;
        ++stat.hop_up_applied;
        return true;
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
  pinCoord(pin, pin_x, pin_y);
  if (pin_x == 0 && pin_y == 0) {
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
      ++stat.diode_rejected;
      return false;
    }

    idb::IdbPin* diode_pin = inst->get_pin_by_term(term->get_name());
    int32_t dx = x;
    int32_t dy = y;
    pinCoord(diode_pin, dx, dy);
    const RCRoutingLayer* m1 = ctx.get_routing_layers().empty() ? nullptr : &ctx.get_routing_layers().front();
    if (m1 != nullptr && m1->layer != nullptr && net->get_wire_list() != nullptr) {
      idb::IdbRegularWire* wire = net->get_wire_list()->add_wire();
      wire->set_wire_state(idb::IdbWiringStatement::kRouted);
      idb::IdbRegularWireSegment* stub = new idb::IdbRegularWireSegment();
      stub->set_layer(m1->layer);
      stub->add_point(dx, dy);
      stub->add_point(pin_x, pin_y);
      stub->set_layer_as_new();
      wire->add_segment(stub);
    }
    ++stat.diode_applied;
    return true;
  }
  ++stat.diode_rejected;
  return false;
}

}  // namespace izh

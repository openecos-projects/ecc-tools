// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "LibertyArcSelector.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ista {

bool LibertyArcSelector::isClockArcTriggerTrans(TimingCellArc& timing_cell_arc, TransType input_trans_type)
{
  if (!timing_cell_arc.get_is_clock_arc()) {
    return true;
  }
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    return true;
  }
  TimingArc& timing_arc = timing_cell_arc.get_timing_arc_list().front();
  if (timing_arc.get_trigger_trans_type() == TransType::kFall) {
    return input_trans_type == TransType::kFall;
  }
  if (timing_arc.get_trigger_trans_type() == TransType::kRise) {
    return input_trans_type == TransType::kRise;
  }
  return input_trans_type == TransType::kRise;
}

TimingCellArc* LibertyArcSelector::cellArc(Arc& arc)
{
  Database& database = _database;
  if (arc.libertyArcId()) {
    return database.liberty().findCellArc(*arc.libertyArcId());
  }
  if (database.network().instances().count(arc.get_owner_name()) == 0) {
    return nullptr;
  }
  Instance& instance = database.network().instances()[arc.get_owner_name()];
  std::map<std::string, LibertyCell>& timing_cell_map = database.liberty().get_cell_map();
  if (timing_cell_map.count(instance.get_cell_name()) == 0) {
    return nullptr;
  }
  LibertyCell& timing_cell = timing_cell_map[instance.get_cell_name()];
  for (TimingCellArc& timing_cell_arc : timing_cell.get_cell_arc_list()) {
    if (timing_cell_arc.get_source_port() == arc.get_library_source_port() && timing_cell_arc.get_sink_port() == arc.get_library_sink_port()) {
      return &timing_cell_arc;
    }
  }
  return nullptr;
}

TransType LibertyArcSelector::getOutputTransType(TimingCellArc& timing_cell_arc, TransType input_trans_type)
{
  if (isNegativeArc(timing_cell_arc)) {
    return input_trans_type == TransType::kRise ? TransType::kFall : TransType::kRise;
  }
  return input_trans_type;
}

std::vector<TransType> LibertyArcSelector::getOutputTransTypeList(TimingCellArc& timing_cell_arc, TransType input_trans_type)
{
  std::vector<TransType> output_trans_type_list;
  if (timing_cell_arc.isThreeStateArc()) {
    for (auto output : {TransType::kRise, TransType::kFall})
      if (!cellCandidates(timing_cell_arc, input_trans_type, output).empty())
        output_trans_type_list.push_back(output);
    return output_trans_type_list;
  }
  if (!isUnateArc(timing_cell_arc) || isTwoTypeSenseArcSet(timing_cell_arc) || timing_cell_arc.get_is_clock_arc()) {
    for (TransType output_trans_type : {TransType::kRise, TransType::kFall}) {
      if (isMatchTimingType(timing_cell_arc, output_trans_type)) {
        output_trans_type_list.push_back(output_trans_type);
      }
    }
    return output_trans_type_list;
  }

  TransType output_trans_type = getOutputTransType(timing_cell_arc, input_trans_type);
  if (isMatchTimingType(timing_cell_arc, output_trans_type)) {
    output_trans_type_list.push_back(output_trans_type);
  }
  return output_trans_type_list;
}

std::vector<TimingArc*> LibertyArcSelector::cellCandidates(TimingCellArc& timing_cell_arc, TransType input_trans_type, TransType output_trans_type,
                                                           const std::string& output_pin)
{
  bool is_flip = input_trans_type != output_trans_type;
  std::vector<TimingArc*> candidate_arc_list;
  bool is_virtual_driver = false;
  if (!output_pin.empty()) {
    Database& database = _database;
    auto pin = database.network().pins().find(output_pin);
    is_virtual_driver = pin != database.network().pins().end() && pin->second.get_is_port();
  }
  for (TimingArc& timing_arc : timing_cell_arc.get_timing_arc_list()) {
    if (timing_arc.isThreeStateArc()) {
      const auto trigger = timing_arc.get_trigger_trans_type();
      if (trigger != TransType::kNone && trigger != input_trans_type)
        continue;
    } else {
      if (is_flip && isPositiveArc(timing_arc))
        continue;
      if (!is_flip && isNegativeArc(timing_arc))
        continue;
    }
    if (!isMatchTimingType(timing_arc, output_trans_type)) {
      continue;
    }
    if (!timing_arc.get_when_expression().get_is_empty() && !output_pin.empty() && !is_virtual_driver) {
      Database& database = _database;
      auto pin = database.network().pins().find(output_pin);
      if (pin != database.network().pins().end() && !pin->second.get_is_port()) {
        std::map<std::string, bool> values;
        for (LogicExpressionTerm& term : timing_arc.get_when_expression().get_term_list()) {
          if (term.get_operation_type() != LogicOperationType::kPort) {
            continue;
          }
          const auto value = database.graph().simValues().value(pin->second.get_instance_name() + ":" + term.get_port_name());
          if (value && (*value == TimingCaseValue::kZero || *value == TimingCaseValue::kOne)) {
            values[term.get_port_name()] = *value == TimingCaseValue::kOne;
          }
        }
        if (timing_arc.get_when_expression().evaluate_constant(values) == std::optional<bool>(false)) {
          continue;
        }
      }
    }
    candidate_arc_list.push_back(&timing_arc);
  }
  if (is_virtual_driver && !candidate_arc_list.empty()) {
    // A set_driving_cell cell is a virtual instance, so it has no instance
    // pins from which to resolve Liberty `when` conditions.  OpenSTA uses the
    // default (first declared) conditional arc for this case.  Keep all
    // unconditional alternatives for the existing max/min reduction, but do
    // not let an unresolved conditional arc win by pessimism.
    std::vector<TimingArc*> unconditional_arc_list;
    for (TimingArc* timing_arc : candidate_arc_list) {
      if (timing_arc->get_when_expression().get_is_empty()) {
        unconditional_arc_list.push_back(timing_arc);
      }
    }
    if (!unconditional_arc_list.empty()) {
      return unconditional_arc_list;
    }
    return {candidate_arc_list.front()};
  }
  return candidate_arc_list;
}

std::vector<TimingArc*> LibertyArcSelector::checkCandidates(TimingCheckArc& timing_check_arc, TransType clock_trans_type, TransType data_trans_type)
{
  std::vector<TimingArc*> candidate_arc_list;
  for (TimingArc& timing_arc : timing_check_arc.get_timing_arc_list()) {
    if (!isMatchTimingType(timing_arc, data_trans_type)) {
      continue;
    }
    candidate_arc_list.push_back(&timing_arc);
  }
  return candidate_arc_list;
}

bool LibertyArcSelector::isMatchTimingType(TimingArc& timing_arc, TransType trans_type)
{
  return timing_arc.get_delay_table_map().count(trans_type) > 0 || timing_arc.get_slew_table_map().count(trans_type) > 0
         || timing_arc.get_check_table_map().count(trans_type) > 0;
}

bool LibertyArcSelector::isPositiveArc(TimingArc& timing_arc)
{
  return timing_arc.get_sense() == TimingArcSense::kPositive || timing_arc.get_sense() == TimingArcSense::kNone;
}

bool LibertyArcSelector::isNegativeArc(TimingArc& timing_arc)
{
  return timing_arc.get_sense() == TimingArcSense::kNegative;
}

bool LibertyArcSelector::isUnateArc(TimingCellArc& timing_cell_arc)
{
  for (TimingArc& timing_arc : timing_cell_arc.get_timing_arc_list()) {
    if (timing_arc.get_sense() == TimingArcSense::kNonUnate) {
      return false;
    }
  }
  return true;
}

bool LibertyArcSelector::isNegativeArc(TimingCellArc& timing_cell_arc)
{
  for (TimingArc& timing_arc : timing_cell_arc.get_timing_arc_list()) {
    if (!isNegativeArc(timing_arc)) {
      return false;
    }
  }
  return true;
}

bool LibertyArcSelector::isTwoTypeSenseArcSet(TimingCellArc& timing_cell_arc)
{
  bool has_positive = false;
  bool has_negative = false;
  for (TimingArc& timing_arc : timing_cell_arc.get_timing_arc_list()) {
    if (isPositiveArc(timing_arc)) {
      has_positive = true;
    } else if (isNegativeArc(timing_arc)) {
      has_negative = true;
    }
  }
  return has_positive && has_negative;
}

bool LibertyArcSelector::isMatchTimingType(TimingCellArc& timing_cell_arc, TransType trans_type)
{
  for (TimingArc& timing_arc : timing_cell_arc.get_timing_arc_list()) {
    if (isMatchTimingType(timing_arc, trans_type)) {
      return true;
    }
  }
  return false;
}

}  // namespace ista

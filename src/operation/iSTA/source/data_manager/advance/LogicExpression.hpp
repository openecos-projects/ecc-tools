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
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "LogicExpressionTerm.hpp"
#include "PowerActivity.hpp"
#include "STAHeader.hpp"

namespace ista {

class LogicExpression
{
 public:
  LogicExpression() = default;
  ~LogicExpression() = default;
  // getter
  std::vector<LogicExpressionTerm>& get_term_list() { return _term_list; }
  bool get_is_empty() const { return _term_list.empty(); }
  // setter
  void set_term_list(const std::vector<LogicExpressionTerm>& term_list) { _term_list = term_list; }
  // function
  bool get_has_port(std::string& port_name)
  {
    for (LogicExpressionTerm& term : _term_list) {
      if (term.get_operation_type() == LogicOperationType::kPort && term.get_port_name() == port_name) {
        return true;
      }
    }
    return false;
  }

  PowerActivity evaluate_activity(std::map<std::string, PowerActivity>& port_activity_map)
  {
    BddModel bdd_model;
    int32_t root_node_idx = 0;
    if (!build_bdd(bdd_model, root_node_idx)) {
      return PowerActivity();
    }

    bool is_valid = true;
    std::map<int32_t, double> static_probability_map;
    double static_probability = get_bdd_static_probability(bdd_model, root_node_idx, port_activity_map, static_probability_map, is_valid);
    if (!is_valid) {
      return PowerActivity();
    }
    double transition_density = get_bdd_transition_density(bdd_model, root_node_idx, port_activity_map, static_probability_map, is_valid);
    if (!is_valid) {
      return PowerActivity();
    }

    PowerActivity stack_activity = evaluate_stack_activity(port_activity_map);
    PowerActivity activity;
    if (stack_activity.get_is_valid() && stack_activity.get_transition_density() > STA_ERROR && transition_density > STA_ERROR) {
      double density_scale = transition_density / stack_activity.get_transition_density();
      activity.set_rise_transition_density(stack_activity.get_rise_transition_density() * density_scale);
      activity.set_fall_transition_density(stack_activity.get_fall_transition_density() * density_scale);
    } else {
      activity.set_transition_density(transition_density);
    }
    activity.set_static_probability(static_probability);
    activity.set_origin(transition_density <= STA_ERROR && (static_probability <= STA_ERROR || static_probability >= 1.0 - STA_ERROR)
                            ? PowerActivityOrigin::kConstant
                            : PowerActivityOrigin::kPropagated);
    activity.set_is_valid(true);
    return activity;
  }

  double get_sensitivity_probability(std::string& port_name, std::map<std::string, PowerActivity>& port_activity_map)
  {
    BddModel bdd_model;
    int32_t root_node_idx = 0;
    if (!build_bdd(bdd_model, root_node_idx)) {
      return 0.0;
    }
    if (bdd_model.get_port_variable_map().count(port_name) == 0) {
      return 0.0;
    }

    int32_t port_variable_idx = bdd_model.get_port_variable_map()[port_name];
    int32_t difference_node_idx = bdd_model.get_boolean_difference(root_node_idx, port_variable_idx);
    bool is_valid = true;
    std::map<int32_t, double> static_probability_map;
    double sensitivity_probability = get_bdd_static_probability(bdd_model, difference_node_idx, port_activity_map, static_probability_map, is_valid);
    return is_valid ? sensitivity_probability : 0.0;
  }

 private:
  class BddNode
  {
   public:
    BddNode() = default;
    BddNode(const int32_t variable_idx, const int32_t low_node_idx, const int32_t high_node_idx)
        : _variable_idx(variable_idx), _low_node_idx(low_node_idx), _high_node_idx(high_node_idx)
    {
    }
    ~BddNode() = default;
    // getter
    int32_t get_variable_idx() const { return _variable_idx; }
    int32_t get_low_node_idx() const { return _low_node_idx; }
    int32_t get_high_node_idx() const { return _high_node_idx; }
    // setter
    // function

   private:
    int32_t _variable_idx = -1;
    int32_t _low_node_idx = 0;
    int32_t _high_node_idx = 0;
  };

  class BddModel
  {
   public:
    BddModel()
    {
      _node_list.emplace_back(-1, 0, 0);
      _node_list.emplace_back(-1, 1, 1);
    }
    ~BddModel() = default;
    // getter
    std::map<std::string, int32_t>& get_port_variable_map() { return _port_variable_map; }
    std::string& get_port_name(const int32_t variable_idx) { return _port_name_list[variable_idx]; }
    // setter
    // function
    int32_t get_port_node(std::string& port_name)
    {
      if (_port_variable_map.count(port_name) > 0) {
        return get_unique_node(_port_variable_map[port_name], 0, 1);
      }
      int32_t variable_idx = static_cast<int32_t>(_port_name_list.size());
      _port_variable_map[port_name] = variable_idx;
      _port_name_list.push_back(port_name);
      return get_unique_node(variable_idx, 0, 1);
    }

    int32_t get_not_node(const int32_t node_idx)
    {
      if (node_idx == 0) {
        return 1;
      }
      if (node_idx == 1) {
        return 0;
      }
      if (_not_node_map.count(node_idx) > 0) {
        return _not_node_map[node_idx];
      }
      BddNode& node = _node_list[node_idx];
      int32_t variable_idx = node.get_variable_idx();
      int32_t low_node_idx = node.get_low_node_idx();
      int32_t high_node_idx = node.get_high_node_idx();
      int32_t not_node_idx = get_unique_node(variable_idx, get_not_node(low_node_idx), get_not_node(high_node_idx));
      _not_node_map[node_idx] = not_node_idx;
      return not_node_idx;
    }

    int32_t get_or_node(const int32_t left_node_idx, const int32_t right_node_idx)
    {
      return get_binary_node(LogicOperationType::kOr, left_node_idx, right_node_idx);
    }

    int32_t get_and_node(const int32_t left_node_idx, const int32_t right_node_idx)
    {
      return get_binary_node(LogicOperationType::kAnd, left_node_idx, right_node_idx);
    }

    int32_t get_xor_node(const int32_t left_node_idx, const int32_t right_node_idx)
    {
      return get_binary_node(LogicOperationType::kXor, left_node_idx, right_node_idx);
    }

    int32_t get_boolean_difference(const int32_t node_idx, const int32_t variable_idx)
    {
      if (node_idx == 0 || node_idx == 1) {
        return 0;
      }
      if (_boolean_difference_map.count(std::make_pair(node_idx, variable_idx)) > 0) {
        return _boolean_difference_map[std::make_pair(node_idx, variable_idx)];
      }

      BddNode& node = _node_list[node_idx];
      int32_t node_variable_idx = node.get_variable_idx();
      if (node_variable_idx > variable_idx) {
        return 0;
      }
      int32_t low_node_idx = node.get_low_node_idx();
      int32_t high_node_idx = node.get_high_node_idx();
      int32_t difference_node_idx = 0;
      if (node_variable_idx == variable_idx) {
        difference_node_idx = get_xor_node(low_node_idx, high_node_idx);
      } else {
        difference_node_idx
            = get_unique_node(node_variable_idx, get_boolean_difference(low_node_idx, variable_idx), get_boolean_difference(high_node_idx, variable_idx));
      }
      _boolean_difference_map[std::make_pair(node_idx, variable_idx)] = difference_node_idx;
      return difference_node_idx;
    }

    BddNode& get_node(const int32_t node_idx) { return _node_list[node_idx]; }

   private:
    int32_t get_binary_node(const LogicOperationType operation_type, int32_t left_node_idx, int32_t right_node_idx)
    {
      if (left_node_idx > right_node_idx) {
        std::swap(left_node_idx, right_node_idx);
      }
      if (left_node_idx <= 1 && right_node_idx <= 1) {
        return get_constant_binary_node(operation_type, left_node_idx, right_node_idx);
      }
      std::tuple<int32_t, int32_t, int32_t> key = std::make_tuple(static_cast<int32_t>(operation_type), left_node_idx, right_node_idx);
      if (_binary_node_map.count(key) > 0) {
        return _binary_node_map[key];
      }

      int32_t variable_idx = std::min(get_node_variable_idx(left_node_idx), get_node_variable_idx(right_node_idx));
      int32_t left_low_node_idx = get_node_variable_idx(left_node_idx) == variable_idx ? _node_list[left_node_idx].get_low_node_idx() : left_node_idx;
      int32_t left_high_node_idx = get_node_variable_idx(left_node_idx) == variable_idx ? _node_list[left_node_idx].get_high_node_idx() : left_node_idx;
      int32_t right_low_node_idx = get_node_variable_idx(right_node_idx) == variable_idx ? _node_list[right_node_idx].get_low_node_idx() : right_node_idx;
      int32_t right_high_node_idx = get_node_variable_idx(right_node_idx) == variable_idx ? _node_list[right_node_idx].get_high_node_idx() : right_node_idx;
      int32_t binary_node_idx = get_unique_node(variable_idx, get_binary_node(operation_type, left_low_node_idx, right_low_node_idx),
                                                get_binary_node(operation_type, left_high_node_idx, right_high_node_idx));
      _binary_node_map[key] = binary_node_idx;
      return binary_node_idx;
    }

    int32_t get_constant_binary_node(const LogicOperationType operation_type, const int32_t left_node_idx, const int32_t right_node_idx)
    {
      bool left_value = left_node_idx == 1;
      bool right_value = right_node_idx == 1;
      bool result_value = false;
      switch (operation_type) {
        case LogicOperationType::kOr:
          result_value = left_value || right_value;
          break;
        case LogicOperationType::kAnd:
          result_value = left_value && right_value;
          break;
        case LogicOperationType::kXor:
          result_value = left_value != right_value;
          break;
        default:
          return 0;
      }
      return result_value ? 1 : 0;
    }

    int32_t get_node_variable_idx(const int32_t node_idx)
    {
      return node_idx <= 1 ? std::numeric_limits<int32_t>::max() : _node_list[node_idx].get_variable_idx();
    }

    int32_t get_unique_node(const int32_t variable_idx, const int32_t low_node_idx, const int32_t high_node_idx)
    {
      if (low_node_idx == high_node_idx) {
        return low_node_idx;
      }
      std::tuple<int32_t, int32_t, int32_t> key = std::make_tuple(variable_idx, low_node_idx, high_node_idx);
      if (_unique_node_map.count(key) > 0) {
        return _unique_node_map[key];
      }
      int32_t node_idx = static_cast<int32_t>(_node_list.size());
      _node_list.emplace_back(variable_idx, low_node_idx, high_node_idx);
      _unique_node_map[key] = node_idx;
      return node_idx;
    }

    std::vector<BddNode> _node_list;
    std::vector<std::string> _port_name_list;
    std::map<std::string, int32_t> _port_variable_map;
    std::map<std::tuple<int32_t, int32_t, int32_t>, int32_t> _unique_node_map;
    std::map<std::tuple<int32_t, int32_t, int32_t>, int32_t> _binary_node_map;
    std::map<int32_t, int32_t> _not_node_map;
    std::map<std::pair<int32_t, int32_t>, int32_t> _boolean_difference_map;
  };

  bool build_bdd(BddModel& bdd_model, int32_t& root_node_idx)
  {
    std::vector<int32_t> node_idx_stack;
    for (LogicExpressionTerm& term : _term_list) {
      LogicOperationType operation_type = term.get_operation_type();
      if (operation_type == LogicOperationType::kPort) {
        node_idx_stack.push_back(bdd_model.get_port_node(term.get_port_name()));
        continue;
      }
      if (operation_type == LogicOperationType::kOne) {
        node_idx_stack.push_back(1);
        continue;
      }
      if (operation_type == LogicOperationType::kZero) {
        node_idx_stack.push_back(0);
        continue;
      }
      if (operation_type == LogicOperationType::kNot) {
        if (node_idx_stack.empty()) {
          return false;
        }
        int32_t input_node_idx = node_idx_stack.back();
        node_idx_stack.pop_back();
        node_idx_stack.push_back(bdd_model.get_not_node(input_node_idx));
        continue;
      }
      if (node_idx_stack.size() < 2) {
        return false;
      }
      int32_t right_node_idx = node_idx_stack.back();
      node_idx_stack.pop_back();
      int32_t left_node_idx = node_idx_stack.back();
      node_idx_stack.pop_back();
      if (operation_type == LogicOperationType::kOr) {
        node_idx_stack.push_back(bdd_model.get_or_node(left_node_idx, right_node_idx));
      } else if (operation_type == LogicOperationType::kAnd) {
        node_idx_stack.push_back(bdd_model.get_and_node(left_node_idx, right_node_idx));
      } else if (operation_type == LogicOperationType::kXor) {
        node_idx_stack.push_back(bdd_model.get_xor_node(left_node_idx, right_node_idx));
      } else {
        return false;
      }
    }
    if (node_idx_stack.size() != 1) {
      return false;
    }
    root_node_idx = node_idx_stack.back();
    return true;
  }

  double get_bdd_static_probability(BddModel& bdd_model, const int32_t node_idx, std::map<std::string, PowerActivity>& port_activity_map,
                                    std::map<int32_t, double>& static_probability_map, bool& is_valid)
  {
    if (node_idx == 0) {
      return 0.0;
    }
    if (node_idx == 1) {
      return 1.0;
    }
    if (static_probability_map.count(node_idx) > 0) {
      return static_probability_map[node_idx];
    }

    BddNode& node = bdd_model.get_node(node_idx);
    std::string& port_name = bdd_model.get_port_name(node.get_variable_idx());
    PowerActivity port_activity = get_port_activity(port_name, port_activity_map);
    if (!port_activity.get_is_valid()) {
      is_valid = false;
      return 0.0;
    }

    double static_probability = port_activity.get_static_probability();
    if (static_probability <= STA_ERROR) {
      double probability = get_bdd_static_probability(bdd_model, node.get_low_node_idx(), port_activity_map, static_probability_map, is_valid);
      static_probability_map[node_idx] = probability;
      return probability;
    }
    if (static_probability >= 1.0 - STA_ERROR) {
      double high_probability = get_bdd_static_probability(bdd_model, node.get_high_node_idx(), port_activity_map, static_probability_map, is_valid);
      static_probability_map[node_idx] = high_probability;
      return high_probability;
    }
    double probability = get_bdd_static_probability(bdd_model, node.get_low_node_idx(), port_activity_map, static_probability_map, is_valid);
    if (!is_valid) {
      return 0.0;
    }
    double high_probability = get_bdd_static_probability(bdd_model, node.get_high_node_idx(), port_activity_map, static_probability_map, is_valid);
    if (!is_valid) {
      return 0.0;
    }
    probability = probability * (1.0 - static_probability) + high_probability * static_probability;
    static_probability_map[node_idx] = probability;
    return probability;
  }

  double get_bdd_transition_density(BddModel& bdd_model, const int32_t root_node_idx, std::map<std::string, PowerActivity>& port_activity_map,
                                    std::map<int32_t, double>& static_probability_map, bool& is_valid)
  {
    double transition_density = 0.0;
    for (std::pair<const std::string, int32_t>& port_pair : bdd_model.get_port_variable_map()) {
      std::string port_name = port_pair.first;
      PowerActivity port_activity = get_port_activity(port_name, port_activity_map);
      if (port_activity.get_is_valid() && port_activity.get_transition_density() <= STA_ERROR) {
        continue;
      }
      int32_t difference_node_idx = bdd_model.get_boolean_difference(root_node_idx, port_pair.second);
      bool difference_is_valid = true;
      double sensitivity_probability
          = get_bdd_static_probability(bdd_model, difference_node_idx, port_activity_map, static_probability_map, difference_is_valid);
      if (!difference_is_valid) {
        is_valid = false;
        return 0.0;
      }
      if (sensitivity_probability <= STA_ERROR) {
        continue;
      }
      if (!port_activity.get_is_valid()) {
        is_valid = false;
        return 0.0;
      }
      transition_density += port_activity.get_transition_density() * sensitivity_probability;
    }
    return transition_density;
  }

  PowerActivity evaluate_stack_activity(std::map<std::string, PowerActivity>& port_activity_map)
  {
    std::vector<PowerActivity> activity_stack;
    for (LogicExpressionTerm& term : _term_list) {
      LogicOperationType operation_type = term.get_operation_type();
      if (operation_type == LogicOperationType::kPort) {
        activity_stack.push_back(get_port_activity(term.get_port_name(), port_activity_map));
        continue;
      }
      if (operation_type == LogicOperationType::kOne) {
        activity_stack.push_back(get_constant_activity(1.0));
        continue;
      }
      if (operation_type == LogicOperationType::kZero) {
        activity_stack.push_back(get_constant_activity(0.0));
        continue;
      }
      if (operation_type == LogicOperationType::kNot) {
        if (activity_stack.empty()) {
          return PowerActivity();
        }
        PowerActivity input_activity = activity_stack.back();
        activity_stack.pop_back();
        activity_stack.push_back(get_not_activity(input_activity));
        continue;
      }
      if (activity_stack.size() < 2) {
        return PowerActivity();
      }
      PowerActivity right_activity = activity_stack.back();
      activity_stack.pop_back();
      PowerActivity left_activity = activity_stack.back();
      activity_stack.pop_back();
      if (operation_type == LogicOperationType::kOr) {
        activity_stack.push_back(get_or_activity(left_activity, right_activity));
      } else if (operation_type == LogicOperationType::kAnd) {
        activity_stack.push_back(get_and_activity(left_activity, right_activity));
      } else if (operation_type == LogicOperationType::kXor) {
        activity_stack.push_back(get_xor_activity(left_activity, right_activity));
      } else {
        return PowerActivity();
      }
    }
    if (activity_stack.size() != 1) {
      return PowerActivity();
    }
    return activity_stack.back();
  }

  PowerActivity get_port_activity(std::string& port_name, std::map<std::string, PowerActivity>& port_activity_map)
  {
    if (port_activity_map.count(port_name) == 0) {
      return PowerActivity();
    }
    return port_activity_map[port_name];
  }

  PowerActivity get_constant_activity(const double static_probability)
  {
    PowerActivity activity;
    activity.set_static_probability(static_probability);
    activity.set_origin(PowerActivityOrigin::kConstant);
    activity.set_is_valid(true);
    return activity;
  }

  PowerActivity get_not_activity(PowerActivity& input_activity)
  {
    PowerActivity activity;
    activity.set_rise_transition_density(input_activity.get_fall_transition_density());
    activity.set_fall_transition_density(input_activity.get_rise_transition_density());
    activity.set_static_probability(1.0 - input_activity.get_static_probability());
    activity.set_origin(PowerActivityOrigin::kPropagated);
    activity.set_is_valid(input_activity.get_is_valid());
    return activity;
  }

  PowerActivity get_or_activity(PowerActivity& left_activity, PowerActivity& right_activity)
  {
    double left_probability = left_activity.get_static_probability();
    double right_probability = right_activity.get_static_probability();
    double transition_density
        = left_activity.get_transition_density() * (1.0 - right_probability) + right_activity.get_transition_density() * (1.0 - left_probability);
    return get_binary_activity(transition_density, left_probability + right_probability - left_probability * right_probability, left_activity, right_activity);
  }

  PowerActivity get_and_activity(PowerActivity& left_activity, PowerActivity& right_activity)
  {
    double left_probability = left_activity.get_static_probability();
    double right_probability = right_activity.get_static_probability();
    double transition_density = left_activity.get_transition_density() * right_probability + right_activity.get_transition_density() * left_probability;
    return get_binary_activity(transition_density, left_probability * right_probability, left_activity, right_activity);
  }

  PowerActivity get_xor_activity(PowerActivity& left_activity, PowerActivity& right_activity)
  {
    double left_probability = left_activity.get_static_probability();
    double right_probability = right_activity.get_static_probability();
    double transition_density = left_activity.get_transition_density() + right_activity.get_transition_density();
    double static_probability = left_probability * (1.0 - right_probability) + (1.0 - left_probability) * right_probability;
    return get_binary_activity(transition_density, static_probability, left_activity, right_activity);
  }

  PowerActivity get_binary_activity(const double transition_density, const double static_probability, PowerActivity& left_activity,
                                    PowerActivity& right_activity)
  {
    PowerActivity activity;
    activity.set_transition_density(transition_density);
    activity.set_static_probability(static_probability);
    activity.set_origin(PowerActivityOrigin::kPropagated);
    activity.set_is_valid(left_activity.get_is_valid() || right_activity.get_is_valid());
    return activity;
  }

  std::vector<LogicExpressionTerm> _term_list;
};

}  // namespace ista

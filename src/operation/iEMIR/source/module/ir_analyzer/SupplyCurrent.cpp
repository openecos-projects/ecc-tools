// SPDX-License-Identifier: MulanPSL-2.0
#include "SupplyCurrent.hpp"

#include <stdexcept>

#include "Logger.hpp"

namespace iemir {

double graphSourceVoltage(PowerGraph& graph, std::map<uint64_t, InstancePower>& powers)
{
  if (graph.get_net_type() == PowerNetType::kGround) {
    return 0.0;
  }
  double voltage = 0.0;
  auto add_voltage = [&](double value, uint64_t id, const std::string& pin) {
    if (value <= EMIR_ERROR) {
      return;
    }
    if (voltage > EMIR_ERROR && std::abs(value - voltage) > EMIR_ERROR) {
      throw std::runtime_error("Inconsistent source voltages on net " + graph.get_net_name() + ": " + std::to_string(voltage) + " V versus "
                               + std::to_string(value) + " V at instance " + std::to_string(id) + "/" + pin);
    }
    voltage = value;
  };
  for (const auto& [id, nodes] : graph.get_instance_node_id_list_map()) {
    auto found = powers.find(id);
    if (found == powers.end()) {
      continue;
    }
    auto& power = found->second;
    if (power.get_supply_currents().empty()) {
      add_voltage(power.get_voltage(), id, "<single-supply>");
    } else {
      for (const auto& [pin, supply] : power.get_supply_currents()) {
        auto contact = graph.get_instance_pin_node_id_list_map().find({id, pin});
        if (contact != graph.get_instance_pin_node_id_list_map().end() && !contact->second.empty()) {
          add_voltage(supply.voltage, id, pin);
        }
      }
    }
  }
  if (voltage <= EMIR_ERROR) {
    throw std::runtime_error("Power graph source voltage is unavailable on net " + graph.get_net_name());
  }
  return voltage;
}

void distributeNodeCurrent(IAModel& model, const std::vector<std::size_t>& nodes, const std::map<std::size_t, double>& weights, double current)
{
  const std::set<std::size_t> unique_nodes(nodes.begin(), nodes.end());
  double total_weight = 0.0;
  for (auto node : unique_nodes) {
    if (auto weight = weights.find(node); weight != weights.end()) {
      total_weight += weight->second;
    }
  }
  for (auto node : unique_nodes) {
    auto weight = weights.find(node);
    double fraction = total_weight > 0.0 ? (weight == weights.end() ? 0.0 : weight->second / total_weight) : 1.0 / unique_nodes.size();
    model.get_node_current_map()[node] += current * fraction;
  }
}

void buildSupplyCurrent(PowerGraph& graph, uint64_t id, InstancePower& power, IAModel& model)
{
  const bool ground = graph.get_net_type() == PowerNetType::kGround;
  for (const auto& [pin, supply] : power.get_supply_currents()) {
    auto key = std::make_pair(id, ground ? supply.ground_pin : pin);
    auto contact = graph.get_instance_pin_node_id_list_map().find(key);
    if (contact == graph.get_instance_pin_node_id_list_map().end() || contact->second.empty()) {
      continue;
    }
    // Return the same current on the associated ground; never distribute the
    // other rails' remainder onto this graph or count it once per power net.
    distributeNodeCurrent(model, contact->second, graph.get_instance_pin_node_weight_map()[key], (ground ? 1.0 : -1.0) * supply.current);
    model.add_pin_mapped_current(supply.current);
  }
}

void reportSupplyCoverage(Database& database, const std::string& path)
{
  using PinKey = std::pair<uint64_t, std::string>;
  std::map<PinKey, std::pair<std::string, PowerNetType>> connections;
  for (auto& [net_name, net] : database.get_power_net_map()) {
    for (const auto& [key, has_geometry] : net.get_pin_has_geometry()) {
      auto [entry, inserted] = connections.emplace(key, std::make_pair(net_name, net.get_type()));
      if (!inserted && entry->second.first != net_name) {
        throw std::runtime_error("PG pin connected to more than one net: instance " + std::to_string(key.first) + "/" + key.second);
      }
    }
  }
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("Cannot create supply coverage report: " + path);
  }
  output << "instance_id\tpower_pin\tground_pin\tvoltage_v\tcurrent_a\tpower_net\tground_net\tpower_analyzed\tground_analyzed\n" << std::setprecision(17);
  double total = 0.0;
  double analyzed = 0.0;
  std::size_t skipped = 0;
  for (auto& [id, power] : database.get_instance_power_map()) {
    for (const auto& [pin, supply] : power.get_supply_currents()) {
      auto contact = [&](const std::string& name, PowerNetType type) {
        auto found = connections.find({id, name});
        if (found == connections.end() || found->second.second != type) {
          if (supply.current != 0.0) {
            throw std::runtime_error("No matching DEF PG connection for instance " + std::to_string(id) + "/" + name);
          }
          return std::make_pair(std::string(), false);
        }
        auto graph = database.get_power_graph_map().find(found->second.first);
        bool active = graph != database.get_power_graph_map().end() && graph->second.get_instance_pin_node_id_list_map().contains({id, name})
                      && !graph->second.get_instance_pin_node_id_list_map().at({id, name}).empty();
        if (!active && graph != database.get_power_graph_map().end() && supply.current != 0.0) {
          throw std::runtime_error("PG pin has no graph contact: instance " + std::to_string(id) + "/" + name);
        }
        return std::make_pair(found->second.first, active);
      };
      auto [power_net, power_active] = contact(pin, PowerNetType::kPower);
      auto [ground_net, ground_active] = contact(supply.ground_pin, PowerNetType::kGround);
      total += supply.voltage * supply.current;
      analyzed += power_active ? supply.voltage * supply.current : 0.0;
      skipped += !power_active || !ground_active;
      output << id << '\t' << pin << '\t' << supply.ground_pin << '\t' << supply.voltage << '\t' << supply.current << '\t' << power_net << '\t' << ground_net
             << '\t' << power_active << '\t' << ground_active << '\n';
    }
  }
  EMIRLOG.info(Loc::current(), "Explicit supply power coverage: analyzed=", analyzed, " W, total=", total,
               " W, rails without both analyzed power/ground contacts=", skipped, "; report: ", path);
  if (skipped != 0) {
    EMIRLOG.warn(Loc::current(), "EMIR covers only the power graphs selected by its sources; unselected supply rails are NOT signed off.");
  }
}

}  // namespace iemir

// SPDX-License-Identifier: MulanPSL-2.0
#include "SupplyCurrent.hpp"

#include <filesystem>
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

void reportSupplyCoverage(Database& database, const std::string& path, bool allow_unmapped_pg_pins)
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
  double incomplete_power = 0.0;
  double unmapped_power_current = 0.0;
  double unmapped_ground_current = 0.0;
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
          if (!allow_unmapped_pg_pins) {
            throw std::runtime_error("PG pin has no graph contact: instance " + std::to_string(id) + "/" + name);
          }
          EMIRLOG.warn(Loc::current(), "PG pin has no graph contact: instance ", id, "/", name, "; omitting ", supply.current,
                       " A on net ", found->second.first, " (exploratory analysis only)");
        }
        return std::make_pair(found->second.first, active);
      };
      auto [power_net, power_active] = contact(pin, PowerNetType::kPower);
      auto [ground_net, ground_active] = contact(supply.ground_pin, PowerNetType::kGround);
      total += supply.voltage * supply.current;
      analyzed += power_active ? supply.voltage * supply.current : 0.0;
      skipped += !power_active || !ground_active;
      incomplete_power += (!power_active || !ground_active) ? supply.voltage * supply.current : 0.0;
      unmapped_power_current += power_active ? 0.0 : supply.current;
      unmapped_ground_current += ground_active ? 0.0 : supply.current;
      output << id << '\t' << pin << '\t' << supply.ground_pin << '\t' << supply.voltage << '\t' << supply.current << '\t' << power_net << '\t' << ground_net
             << '\t' << power_active << '\t' << ground_active << '\n';
    }
  }
  const auto summary_path = std::filesystem::path(path).replace_extension(".rpt");
  std::ofstream summary(summary_path);
  if (!summary) {
    throw std::runtime_error("Cannot create supply coverage summary: " + summary_path.string());
  }
  summary << "# Explicit per-supply records only; legacy single-supply records are excluded.\n"
          << "# Missing-contact currents are omitted on that net, not reassigned.\n"
          << "# These results do not certify full-chip EMIR signoff.\n" << std::setprecision(17)
          << "allow_unmapped_pg_pins " << allow_unmapped_pg_pins << '\n'
          << "total_explicit_supply_power_w " << total << '\n'
          << "power_side_analyzed_w " << analyzed << '\n'
          << "incomplete_supply_power_w " << incomplete_power << '\n'
          << "unmapped_power_current_a " << unmapped_power_current << '\n'
          << "unmapped_ground_current_a " << unmapped_ground_current << '\n'
          << "supplies_without_both_contacts " << skipped << '\n';
  EMIRLOG.info(Loc::current(), "Explicit supply power coverage: analyzed=", analyzed, " W, total=", total,
               " W, rails without both analyzed power/ground contacts=", skipped, "; report: ", path);
  if (skipped != 0) {
    EMIRLOG.warn(Loc::current(), "Incomplete supply coverage: ", incomplete_power, " W; omitted power/ground current=", unmapped_power_current,
                 "/", unmapped_ground_current, " A. Unselected rails and unmapped PG pins are NOT signed off.");
  }
}

}  // namespace iemir

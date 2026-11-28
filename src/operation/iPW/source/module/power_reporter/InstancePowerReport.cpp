// SPDX-License-Identifier: MulanPSL-2.0
#include "PowerReporter.hpp"

#include <stdexcept>

#include "DataManager.hpp"
#include "Utility.hpp"

namespace ipw {
namespace {

void writePower(std::ostream& output, const std::string& name, double voltage, const PowerValue& power)
{
  // Preserve the existing nonnegative load contract for Liberty internal-energy
  // cancellation, now independently per rail. P = V * I uses the exported sum.
  const double internal = std::max(0.0, power.get_internal_power());
  const double total = internal + power.get_switching_power() + power.get_leakage_power();
  output << name << '\t' << voltage << '\t' << internal << '\t' << power.get_switching_power() << '\t'
         << power.get_leakage_power() << '\t' << total << '\t' << (voltage > 0.0 ? total / voltage : 0.0);
}

}  // namespace

void PowerReporter::outputInstancePower()
{
  Database& database = PWDM.getDatabase();
  const bool peak = database.get_peak_power_summary().get_is_enabled();
  if (peak) {
    for (auto& instance : database.get_instance_power_list()) {
      if (instance.get_supply_power().size() > 1) {
        throw std::runtime_error(
            "Per-supply peak-window power export is unavailable; use averaged power for multi-supply EMIR"
        );
      }
    }
  }
  std::ofstream* output
      = PWUTIL.getOutputFileStream(PWUTIL.getString(PWDM.getConfig().pr_temp_directory_path, "instance_power.tsv"));
  (*output) << (peak ? "# iEMIR_INSTANCE_POWER_V1\n" : "# iEMIR_INSTANCE_POWER_V2\n")
            << "instance_name\tvoltage_v\tinternal_power_w\tswitching_power_w\tleakage_power_w\ttotal_power_w\taverage_"
               "current_a";
  if (!peak) {
    (*output) << "\tpower_pin\tground_pin";
  }
  (*output) << '\n' << std::setprecision(17);
  if (peak) {
    outputPeakInstancePower(output);
  } else {
    for (auto& [name, instance] : database.get_instance_map()) {
      auto& power = database.get_instance_power_list()[instance.get_instance_id()];
      if (power.get_supply_power().empty()) {
        writePower(*output, name, power.get_voltage(), power.get_power_value());
        (*output) << "\t\t\n";
      } else {
        for (const auto& [pin, rail] : power.get_supply_power()) {
          writePower(*output, name, rail.supply.voltage, rail.power);
          (*output) << '\t' << pin << '\t' << rail.supply.ground_pin << '\n';
        }
      }
    }
  }
  PWUTIL.closeFileStream(output);
}

void PowerReporter::outputPeakInstancePower(std::ofstream* output)
{
  Database& database = PWDM.getDatabase();
  auto& powers = database.get_instance_power_list();
  for (auto& [name, instance] : database.get_instance_map()) {
    const uint32_t id = static_cast<uint32_t>(instance.get_instance_id());
    auto* peak = database.get_peak_power_summary().findInstancePeakValue(id);
    if (peak == nullptr) {
      continue;
    }
    double voltage = id < powers.size() ? powers[id].get_voltage() : 0.0;
    if (id < powers.size() && !powers[id].get_supply_power().empty()) {
      voltage = powers[id].get_supply_power().begin()->second.supply.voltage;
    }
    writePower(*output, name, voltage, peak->get_power_value());
    (*output) << '\n';
  }
}

}  // namespace ipw

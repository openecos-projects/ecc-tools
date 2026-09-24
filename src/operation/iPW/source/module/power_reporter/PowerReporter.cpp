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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "PowerReporter.hpp"

#include "DataManager.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"

namespace ipw {

// public

void PowerReporter::initInst()
{
  if (_pr_instance == nullptr) {
    _pr_instance = new PowerReporter();
  }
}

PowerReporter& PowerReporter::getInst()
{
  if (_pr_instance == nullptr) {
    PWLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_pr_instance;
}

void PowerReporter::destroyInst()
{
  if (_pr_instance != nullptr) {
    delete _pr_instance;
    _pr_instance = nullptr;
  }
}

// function

void PowerReporter::report()
{
  Monitor monitor;
  PWLOG.info(Loc::current(), "Starting...");

  outputPowerReport();
  outputInstancePower();

  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

PowerReporter* PowerReporter::_pr_instance = nullptr;

void PowerReporter::outputPowerReport()
{
  std::ofstream* power_report_file = PWUTIL.getOutputFileStream(PWUTIL.getString(PWDM.getConfig().pr_temp_directory_path, "power.rpt"));
  outputPowerDesignInfo(power_report_file);
  outputPowerUnitInfo(power_report_file);
  outputPowerSummary(power_report_file);
  outputPowerGroupList(power_report_file);
  outputPowerAttribute(power_report_file);
  PWUTIL.closeFileStream(power_report_file);
}

void PowerReporter::outputPowerDesignInfo(std::ofstream* power_report_file)
{
  Database& database = PWDM.getDatabase();
  TimingLibrary& timing_library = database.get_timing_library();
  (*power_report_file) << "Design : " << database.get_design_name() << "\n";
  (*power_report_file) << "Global Operating Voltage = " << std::setprecision(4) << timing_library.get_nom_voltage() << "\n\n";
}

void PowerReporter::outputPowerUnitInfo(std::ofstream* power_report_file)
{
  TimingLibrary& timing_library = PWDM.getDatabase().get_timing_library();
  std::string leakage_power_unit = timing_library.get_leakage_power_unit() ? *timing_library.get_leakage_power_unit() : "1mW";
  (*power_report_file) << "Dynamic Power Units = 1mW\n";
  (*power_report_file) << "Leakage Power Units = " << leakage_power_unit << "\n\n";
}

void PowerReporter::outputPowerSummary(std::ofstream* power_report_file)
{
  PowerValue& total_power_value = PWDM.getDatabase().get_power_summary().get_total_power_value();
  double dynamic_power = getDynamicPower();
  (*power_report_file) << "Cell Internal Power  = " << std::setw(12) << getPowerString(total_power_value.get_internal_power()) << "\n";
  (*power_report_file) << "Net Switching Power  = " << std::setw(12) << getPowerString(total_power_value.get_switching_power()) << "\n";
  (*power_report_file) << "Total Dynamic Power  = " << std::setw(12) << getPowerString(dynamic_power) << "\n";
  (*power_report_file) << "Cell Leakage Power   = " << std::setw(12) << getPowerString(total_power_value.get_leakage_power()) << "\n\n";
}

void PowerReporter::outputPowerGroupList(std::ofstream* power_report_file)
{
  (*power_report_file) << "                 Internal         Switching           Leakage            Total\n";
  (*power_report_file) << "Power Group      Power            Power               Power              Power   (   %    )  Attrs\n";
  (*power_report_file) << "--------------------------------------------------------------------------------------------------\n";
  for (PowerGroupType power_group_type : GetPowerGroupTypeList()()) {
    outputPowerGroup(power_report_file, power_group_type);
  }
  (*power_report_file) << "--------------------------------------------------------------------------------------------------\n";
  PowerValue& total_power_value = PWDM.getDatabase().get_power_summary().get_total_power_value();
  (*power_report_file) << std::left << std::setw(15) << "Total" << std::right << std::setw(13)
                       << getPowerTotalString(total_power_value.get_internal_power(), false) << std::setw(18)
                       << getPowerTotalString(total_power_value.get_switching_power(), false) << std::setw(18)
                       << getPowerTotalString(total_power_value.get_leakage_power(), true) << std::setw(18)
                       << getPowerTotalString(total_power_value.get_total_power(), false) << "\n";
}

void PowerReporter::outputPowerGroup(std::ofstream* power_report_file, PowerGroupType power_group_type)
{
  PowerValue power_value = getPowerGroupPowerValue(power_group_type);
  double total_power = PWDM.getDatabase().get_power_summary().get_total_power_value().get_total_power();
  (*power_report_file) << std::left << std::setw(15) << GetPowerGroupTypeName()(power_group_type) << std::right << std::setw(10)
                       << getPowerTableString(power_value.get_internal_power(), false) << std::setw(18)
                       << getPowerTableString(power_value.get_switching_power(), false) << std::setw(18)
                       << getPowerTableString(power_value.get_leakage_power(), true) << std::setw(18)
                       << getPowerTableString(power_value.get_total_power(), false) << "  (" << std::setw(7) << std::fixed << std::setprecision(2)
                       << getPercentage(power_value.get_total_power(), total_power) << "%)" << getPowerGroupAttribute(power_group_type) << "\n";
}

void PowerReporter::outputPowerAttribute(std::ofstream* power_report_file)
{
  (*power_report_file) << "\ni - Including register clock pin internal power\n";
}

void PowerReporter::outputInstancePower()
{
  Database& database = PWDM.getDatabase();
  std::ofstream* instance_power_file = PWUTIL.getOutputFileStream(PWUTIL.getString(PWDM.getConfig().pr_temp_directory_path, "instance_power.bin"));
  outputInstancePowerHeader(instance_power_file);
  for (std::pair<const std::string, InstancePower>& instance_power_pair : database.get_instance_power_map()) {
    outputInstancePowerRecord(instance_power_file, instance_power_pair.first, instance_power_pair.second);
  }
  PWUTIL.closeFileStream(instance_power_file);
}

void PowerReporter::outputInstancePowerHeader(std::ofstream* instance_power_file)
{
  char magic[8] = {'I', 'S', 'T', 'A', 'P', 'W', 'R', '\0'};
  uint32_t version = 1;
  uint32_t record_size = sizeof(uint64_t) + sizeof(uint32_t) + 4 * sizeof(double);
  uint64_t instance_power_num = PWDM.getDatabase().get_instance_power_map().size();
  instance_power_file->write(magic, static_cast<std::streamsize>(sizeof(magic)));
  instance_power_file->write(reinterpret_cast<const char*>(&version), static_cast<std::streamsize>(sizeof(version)));
  instance_power_file->write(reinterpret_cast<const char*>(&record_size), static_cast<std::streamsize>(sizeof(record_size)));
  instance_power_file->write(reinterpret_cast<const char*>(&instance_power_num), static_cast<std::streamsize>(sizeof(instance_power_num)));
}

void PowerReporter::outputInstancePowerRecord(std::ofstream* instance_power_file, const std::string& instance_name, InstancePower& instance_power)
{
  // iEMIR resolves the stable ID against the same IDB design database.
  uint64_t instance_id = PWDM.getDatabase().get_instance_map()[instance_name].get_instance_id();
  uint32_t power_group_type = static_cast<uint32_t>(instance_power.get_power_group_type());
  double voltage = instance_power.get_voltage();
  double internal_power = instance_power.get_power_value().get_internal_power();
  double switching_power = instance_power.get_power_value().get_switching_power();
  double leakage_power = instance_power.get_power_value().get_leakage_power();
  instance_power_file->write(reinterpret_cast<const char*>(&instance_id), static_cast<std::streamsize>(sizeof(instance_id)));
  instance_power_file->write(reinterpret_cast<const char*>(&power_group_type), static_cast<std::streamsize>(sizeof(power_group_type)));
  instance_power_file->write(reinterpret_cast<const char*>(&voltage), static_cast<std::streamsize>(sizeof(voltage)));
  instance_power_file->write(reinterpret_cast<const char*>(&internal_power), static_cast<std::streamsize>(sizeof(internal_power)));
  instance_power_file->write(reinterpret_cast<const char*>(&switching_power), static_cast<std::streamsize>(sizeof(switching_power)));
  instance_power_file->write(reinterpret_cast<const char*>(&leakage_power), static_cast<std::streamsize>(sizeof(leakage_power)));
}

PowerValue PowerReporter::getPowerGroupPowerValue(PowerGroupType power_group_type)
{
  PowerSummary& power_summary = PWDM.getDatabase().get_power_summary();
  if (power_summary.get_group_power_map().count(power_group_type) == 0) {
    return PowerValue();
  }
  return power_summary.get_group_power_map()[power_group_type];
}

double PowerReporter::getDynamicPower()
{
  PowerValue& total_power_value = PWDM.getDatabase().get_power_summary().get_total_power_value();
  return total_power_value.get_internal_power() + total_power_value.get_switching_power();
}

double PowerReporter::getPercentage(double numerator, double denominator)
{
  if (std::fabs(denominator) <= PW_ERROR) {
    return 0.0;
  }
  return numerator * 100.0 / denominator;
}

std::string PowerReporter::getPowerString(double power)
{
  double abs_power = std::fabs(power);
  double unit_scale = 1.0;
  std::string unit_name = "W";
  if (abs_power == 0.0) {
    unit_scale = 1E3;
    unit_name = "mW";
  } else if (abs_power < 1E-9) {
    unit_scale = 1E12;
    unit_name = "pW";
  } else if (abs_power < 1E-6) {
    unit_scale = 1E9;
    unit_name = "nW";
  } else if (abs_power < 1E-3) {
    unit_scale = 1E6;
    unit_name = "uW";
  } else {
    unit_scale = 1E3;
    unit_name = "mW";
  }
  std::stringstream oss;
  oss << std::fixed << std::setprecision(4) << power * unit_scale << " " << unit_name;
  return oss.str();
}

std::string PowerReporter::getPowerTableString(double power, bool is_leakage_power)
{
  double display_power = power * (is_leakage_power ? 1E9 : 1E3);
  std::stringstream oss;
  if (std::fabs(display_power) > PW_ERROR && std::fabs(display_power) < 0.1) {
    oss << std::scientific << std::setprecision(4) << display_power;
  } else {
    oss << std::fixed << std::setprecision(4) << display_power;
  }
  return oss.str();
}

std::string PowerReporter::getPowerTotalString(double power, bool is_leakage_power)
{
  return getPowerTableString(power, is_leakage_power) + (is_leakage_power ? " nW" : " mW");
}

std::string PowerReporter::getPowerGroupAttribute(PowerGroupType power_group_type)
{
  return power_group_type == PowerGroupType::kClockNetwork ? "  i" : "";
}

}  // namespace ipw

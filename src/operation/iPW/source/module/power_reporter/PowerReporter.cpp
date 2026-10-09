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

#include <algorithm>

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

void PowerReporter::reportCellPower(const std::string& directory_path, const int32_t top_num, const bool is_all)
{
  Monitor monitor;
  PWLOG.info(Loc::current(), "Starting...");

  Database& database = PWDM.getDatabase();
  if (database.get_instance_power_map().empty()) {
    PWLOG.error(Loc::current(), "No instance power data found, run_pw first!");
  }
  if (!is_all && top_num < 1) {
    PWLOG.error(Loc::current(), "The -top option requires a positive integer!");
  }

  std::vector<std::pair<std::string, InstancePower*>> cell_power_list;
  std::size_t skipped_num = 0;
  for (std::pair<const std::string, InstancePower>& instance_power_pair : database.get_instance_power_map()) {
    const std::string& instance_name = instance_power_pair.first;
    auto instance_iter = database.get_instance_map().find(instance_name);
    if (instance_iter == database.get_instance_map().end()
        || database.get_timing_library().get_cell_map().count(instance_iter->second.get_cell_name()) == 0) {
      skipped_num++;
      continue;
    }
    cell_power_list.emplace_back(instance_name, &instance_power_pair.second);
  }
  std::sort(cell_power_list.begin(), cell_power_list.end(),
            [](const std::pair<std::string, InstancePower*>& left_pair, const std::pair<std::string, InstancePower*>& right_pair) {
              double left_power = left_pair.second->get_power_value().get_internal_power();
              double right_power = right_pair.second->get_power_value().get_internal_power();
              if (left_power != right_power) {
                return left_power > right_power;
              }
              return left_pair.first < right_pair.first;
            });
  if (skipped_num > 0) {
    PWLOG.info(Loc::current(), "Skipped ", skipped_num, " instance(s) without power table from cell power report.");
  }
  outputCellPowerReport(directory_path, cell_power_list, top_num, is_all);

  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PowerReporter::reportNetPower(const std::string& directory_path, const int32_t top_num, const bool is_all)
{
  Monitor monitor;
  PWLOG.info(Loc::current(), "Starting...");

  Database& database = PWDM.getDatabase();
  if (database.get_instance_power_map().empty()) {
    PWLOG.error(Loc::current(), "No instance power data found, run_pw first!");
  }
  if (!is_all && top_num < 1) {
    PWLOG.error(Loc::current(), "The -top option requires a positive integer!");
  }

  std::vector<std::pair<std::string, NetPower*>> net_power_list;
  for (std::pair<const std::string, NetPower>& net_power_pair : database.get_net_power_map()) {
    net_power_list.emplace_back(net_power_pair.first, &net_power_pair.second);
  }
  std::sort(net_power_list.begin(), net_power_list.end(),
            [](const std::pair<std::string, NetPower*>& left_pair, const std::pair<std::string, NetPower*>& right_pair) {
              double left_power = left_pair.second->get_switching_power();
              double right_power = right_pair.second->get_switching_power();
              if (left_power != right_power) {
                return left_power > right_power;
              }
              return left_pair.first < right_pair.first;
            });
  outputNetPowerReport(directory_path, net_power_list, top_num, is_all);

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
  std::ofstream* instance_power_file
      = PWUTIL.getOutputFileStream(PWUTIL.getString(PWDM.getConfig().pr_temp_directory_path, "instance_power.tsv"));
  (*instance_power_file) << "# iEMIR_PTPX_INSTANCE_POWER_V1\n";
  (*instance_power_file) << "instance_name\tvoltage_v\tinternal_power_w\tswitching_power_w\tleakage_power_w\t"
                            "total_power_w\taverage_current_a\n";
  (*instance_power_file) << std::setprecision(17);
  for (std::pair<const std::string, InstancePower>& instance_power_pair : PWDM.getDatabase().get_instance_power_map()) {
    InstancePower& instance_power = instance_power_pair.second;
    PowerValue& power_value = instance_power.get_power_value();
    double voltage = instance_power.get_voltage();
    double total_power = power_value.get_total_power();
    double average_current = voltage > 0.0 ? total_power / voltage : 0.0;
    (*instance_power_file) << instance_power_pair.first << '\t' << voltage << '\t' << power_value.get_internal_power() << '\t';
    (*instance_power_file) << power_value.get_switching_power() << '\t' << power_value.get_leakage_power() << '\t';
    (*instance_power_file) << total_power << '\t' << average_current << '\n';
  }
  PWUTIL.closeFileStream(instance_power_file);
}

void PowerReporter::outputCellPowerReport(const std::string& directory_path,
                                          std::vector<std::pair<std::string, InstancePower*>>& cell_power_list,
                                          const int32_t top_num, const bool is_all)
{
  Database& database = PWDM.getDatabase();
  std::string report_directory_path = directory_path.empty() ? PWDM.getConfig().pr_temp_directory_path : directory_path;
  if (!report_directory_path.empty() && report_directory_path.back() != '/') {
    report_directory_path += "/";
  }
  PWUTIL.createDir(report_directory_path);

  std::size_t total_num = cell_power_list.size();
  std::size_t display_num = is_all ? total_num : std::min(static_cast<std::size_t>(top_num), total_num);
  double design_total_power = database.get_power_summary().get_total_power_value().get_total_power();

  std::ofstream* cell_power_report_file = PWUTIL.getOutputFileStream(PWUTIL.getString(report_directory_path, "cell_power.rpt"));
  (*cell_power_report_file) << "****************************************\n";
  (*cell_power_report_file) << "Report : Averaged Power\n";
  (*cell_power_report_file) << "        -cell_power\n";
  (*cell_power_report_file) << "        -sort_by cell_internal_power\n";
  (*cell_power_report_file) << "        " << (is_all ? "-all" : PWUTIL.getString("-top ", top_num)) << "\n";
  (*cell_power_report_file) << "Design : " << database.get_design_name() << "\n";
  (*cell_power_report_file) << "Global Operating Voltage = " << std::setprecision(4) << database.get_timing_library().get_nom_voltage()
                            << "\n";
  (*cell_power_report_file) << "Power Units = 1W\n";
  (*cell_power_report_file) << "Cells : " << total_num << " total, " << display_num << " displayed\n";
  (*cell_power_report_file) << "****************************************\n\n\n\n";
  (*cell_power_report_file) << "  Attributes\n";
  (*cell_power_report_file) << "  ----------\n";
  (*cell_power_report_file) << "      a  -  Annotated internal | leakage | switching power \n";
  (*cell_power_report_file) << "      b  -  Black-box (unresolved) cell\n";
  (*cell_power_report_file) << "      c  -  Clock pin internal power only\n";
  (*cell_power_report_file) << "      d  -  Does not include clock pin internal power\n";
  (*cell_power_report_file) << "      h  -  Hierarchical cell\n\n";
  (*cell_power_report_file) << "                        Internal  Switching Leakage   Total\n";
  (*cell_power_report_file) << "Cell                    Power     Power     Power     Power    (     %)   Attrs\n";
  (*cell_power_report_file) << std::string(80, '-') << "\n";

  PowerValue display_power_value;
  for (std::size_t cell_idx = 0; cell_idx < display_num; cell_idx++) {
    const std::string& cell_name = cell_power_list[cell_idx].first;
    PowerValue& cell_power_value = cell_power_list[cell_idx].second->get_power_value();
    display_power_value.add_power_value(cell_power_value);
    double cell_total_power = cell_power_value.get_total_power();
    if (cell_name.size() < 24) {
      (*cell_power_report_file) << std::left << std::setw(24) << cell_name << std::right;
    } else {
      (*cell_power_report_file) << cell_name << "\n" << std::string(24, ' ');
    }
    (*cell_power_report_file) << std::setw(9) << getPowerCellString(cell_power_value.get_internal_power()) << " " << std::setw(9)
                              << getPowerCellString(cell_power_value.get_switching_power()) << " " << std::setw(9)
                              << getPowerCellString(cell_power_value.get_leakage_power()) << " " << std::setw(9)
                              << getPowerCellString(cell_total_power) << " (" << std::setw(5)
                              << getPowerCellPercentageString(getPercentage(cell_total_power, design_total_power)) << "%)  \n";
  }
  (*cell_power_report_file) << std::string(80, '-') << "\n";

  std::string total_label = is_all ? PWUTIL.getString("Totals (", total_num, " cells)")
                                   : PWUTIL.getString("Total (", display_num, " of ", total_num, " cells)");
  if (total_label.size() < 24) {
    (*cell_power_report_file) << std::left << std::setw(24) << total_label << std::right;
  } else {
    (*cell_power_report_file) << total_label << "\n" << std::string(24, ' ');
  }
  (*cell_power_report_file) << std::setw(9) << getPowerCellString(display_power_value.get_internal_power()) << " " << std::setw(9)
                            << getPowerCellString(display_power_value.get_switching_power()) << " " << std::setw(9)
                            << getPowerCellString(display_power_value.get_leakage_power()) << " " << std::setw(9)
                            << getPowerCellString(display_power_value.get_total_power());
  if (is_all) {
    (*cell_power_report_file) << " (" << std::setw(5) << getPowerCellPercentageString(100.0) << "%)";
  }
  (*cell_power_report_file) << "\n";
  PWUTIL.closeFileStream(cell_power_report_file);

  PWLOG.info(Loc::current(), "Reported ", display_num, " of ", total_num, " cell(s) to '",
             PWUTIL.getString(report_directory_path, "cell_power.rpt"), "'.");
}

void PowerReporter::outputNetPowerReport(const std::string& directory_path, std::vector<std::pair<std::string, NetPower*>>& net_power_list,
                                         const int32_t top_num, const bool is_all)
{
  Database& database = PWDM.getDatabase();
  std::string report_directory_path = directory_path.empty() ? PWDM.getConfig().pr_temp_directory_path : directory_path;
  if (!report_directory_path.empty() && report_directory_path.back() != '/') {
    report_directory_path += "/";
  }
  PWUTIL.createDir(report_directory_path);

  std::size_t total_num = net_power_list.size();
  std::size_t display_num = is_all ? total_num : std::min(static_cast<std::size_t>(top_num), total_num);

  std::ofstream* net_power_report_file = PWUTIL.getOutputFileStream(PWUTIL.getString(report_directory_path, "net_power.rpt"));
  (*net_power_report_file) << "****************************************\n";
  (*net_power_report_file) << "Report : Averaged Power\n";
  (*net_power_report_file) << "        -net_power\n";
  (*net_power_report_file) << "        -sort_by net_switching_power\n";
  (*net_power_report_file) << "        " << (is_all ? "-all" : PWUTIL.getString("-top ", top_num)) << "\n";
  (*net_power_report_file) << "Design : " << database.get_design_name() << "\n";
  (*net_power_report_file) << "Global Operating Voltage = " << std::setprecision(4) << database.get_timing_library().get_nom_voltage()
                           << "\n";
  (*net_power_report_file) << "Power Units = 1W\n";
  (*net_power_report_file) << "Nets : " << total_num << " total, " << display_num << " displayed\n";
  (*net_power_report_file) << "****************************************\n\n\n\n";
  (*net_power_report_file) << "  Attributes\n";
  (*net_power_report_file) << "  ----------\n";
  (*net_power_report_file) << "      a  -  Switching activity information / Switching power information annotated on net\n";
  (*net_power_report_file) << "      p  -  Propagated switching activity information on net\n";
  (*net_power_report_file) << "      d  -  Default switching activity used on net\n";
  (*net_power_report_file) << "      u  -  Net switching activity uninitialized\n";
  (*net_power_report_file) << "      m  -  Net is driven by multiple pins\n";
  (*net_power_report_file) << "      b  -  Net is a primary input (boundary net)\n\n";
  (*net_power_report_file) << "\t\tToggle rates reported with respect to time unit 1ns\n";
  (*net_power_report_file) << std::string(80, '-') << "\n";
  (*net_power_report_file) << "                                  Total      Static   Toggle   Switching\n";
  (*net_power_report_file) << "Net                         Vdd   Net Load   Prob.    Rate     Power      Attrs\n";
  (*net_power_report_file) << std::string(80, '-') << "\n";

  double display_switching_power = 0.0;
  for (std::size_t net_idx = 0; net_idx < display_num; net_idx++) {
    const std::string& net_name = net_power_list[net_idx].first;
    NetPower& net_power = *net_power_list[net_idx].second;
    display_switching_power += net_power.get_switching_power();

    if (net_name.size() < 28) {
      (*net_power_report_file) << std::left << std::setw(28) << net_name << std::right;
    } else {
      (*net_power_report_file) << net_name << "\n" << std::string(28, ' ');
    }
    (*net_power_report_file) << getNetPowerValueString(net_power) << "  " << getNetPowerAttributeString(net_name, net_power) << "\n";
  }
  (*net_power_report_file) << std::string(80, '-') << "\n";

  std::string total_label = is_all ? PWUTIL.getString("Total (", total_num, " nets)")
                                   : PWUTIL.getString("Total (", display_num, " of ", total_num, " nets)");
  (*net_power_report_file) << std::left << std::setw(63) << total_label << std::right << std::setw(9)
                           << getPowerCellString(display_switching_power) << " Watt\n";
  PWUTIL.closeFileStream(net_power_report_file);

  PWLOG.info(Loc::current(), "Reported ", display_num, " of ", total_num, " net(s) to '",
             PWUTIL.getString(report_directory_path, "net_power.rpt"), "'.");
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

std::string PowerReporter::getPowerCellString(double power)
{
  if (std::fabs(power) < 1E-15) {
    return "0.0000";
  }
  std::stringstream oss;
  oss << std::scientific << std::setprecision(3) << power;
  return oss.str();
}

std::string PowerReporter::getPowerCellPercentageString(double percentage)
{
  if (percentage >= 99.995) {
    return "100.0";
  }
  std::stringstream oss;
  oss << std::fixed << std::setprecision(2) << percentage;
  return oss.str();
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

std::string PowerReporter::getNetPowerValueString(NetPower& net_power)
{
  double static_probability = net_power.get_is_activity_valid() ? net_power.get_static_probability() : 0.0;
  double transition_density = net_power.get_is_activity_valid() ? net_power.get_transition_density() : 0.0;
  std::stringstream oss;
  oss << std::fixed << std::setprecision(2) << std::setw(4) << net_power.get_voltage() << "   " << std::setprecision(3) << std::setw(6)
      << net_power.get_total_net_load() << "   " << std::setw(6) << static_probability << "   " << std::setprecision(4) << std::setw(7)
      << transition_density << "   " << std::setw(9) << getPowerCellString(net_power.get_switching_power());
  return oss.str();
}

std::string PowerReporter::getNetPowerAttributeString(const std::string& net_name, NetPower& net_power)
{
  std::string attribute_string;
  if (!net_power.get_is_activity_valid()) {
    attribute_string += "u";
  } else {
    switch (net_power.get_activity_origin()) {
      case PowerActivityOrigin::kVcd:
        attribute_string += "a";
        break;
      case PowerActivityOrigin::kInput:
        attribute_string += "d";
        break;
      case PowerActivityOrigin::kClock:
      case PowerActivityOrigin::kPropagated:
      case PowerActivityOrigin::kSequential:
      case PowerActivityOrigin::kConstant:
        attribute_string += "p";
        break;
      case PowerActivityOrigin::kNone:
      default:
        attribute_string += "u";
        break;
    }
  }
  Database& database = PWDM.getDatabase();
  auto net_iter = database.get_net_map().find(net_name);
  if (net_iter != database.get_net_map().end()) {
    if (net_iter->second.get_driver_pin_list().empty()) {
      attribute_string += "b";
    } else if (net_iter->second.get_driver_pin_list().size() > 1) {
      attribute_string += "m";
    }
  }
  return attribute_string;
}

}  // namespace ipw

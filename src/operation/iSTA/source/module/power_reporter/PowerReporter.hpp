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
#pragma once

#include "Database.hpp"
#include "PRModel.hpp"

namespace ista {

#define STAPR (ista::PowerReporter::getInst())

class PowerReporter
{
 public:
  static void initInst();
  static PowerReporter& getInst();
  static void destroyInst();
  // function
  void report();

 private:
  // self
  static PowerReporter* _pr_instance;

  PowerReporter() = default;
  PowerReporter(const PowerReporter& other) = delete;
  PowerReporter(PowerReporter&& other) = delete;
  ~PowerReporter() = default;
  PowerReporter& operator=(const PowerReporter& other) = delete;
  PowerReporter& operator=(PowerReporter&& other) = delete;
  // function
  PRModel initPRModel();
  void buildPowerReportFilePath(PRModel& pr_model);
  void buildInstancePowerFilePath(PRModel& pr_model);
  void outputPowerReport(PRModel& pr_model);
  void outputPowerDesignInfo(std::ofstream* power_report_file);
  void outputPowerUnitInfo(std::ofstream* power_report_file);
  void outputPowerSummary(std::ofstream* power_report_file);
  void outputPowerGroupList(std::ofstream* power_report_file);
  void outputPowerGroup(std::ofstream* power_report_file, PowerGroupType power_group_type);
  void outputPowerAttribute(std::ofstream* power_report_file);
  void outputInstancePower(PRModel& pr_model);
  void outputInstancePowerHeader(std::ofstream* instance_power_file);
  void outputInstancePowerRecord(std::ofstream* instance_power_file, InstancePower& instance_power);
  PowerValue getPowerGroupPowerValue(PowerGroupType power_group_type);
  double getDynamicPower();
  double getPercentage(double numerator, double denominator);
  std::string getPowerString(double power);
  std::string getPowerTableString(double power, bool is_leakage_power);
  std::string getPowerTotalString(double power, bool is_leakage_power);
  std::string getPowerGroupAttribute(PowerGroupType power_group_type);
};

}  // namespace ista

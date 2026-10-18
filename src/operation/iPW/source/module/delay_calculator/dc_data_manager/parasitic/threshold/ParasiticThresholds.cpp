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
#include "ParasiticThresholds.hpp"

#include "DataManager.hpp"
#include "Database.hpp"
#include "Instance.hpp"
#include "Pin.hpp"
#include "PWHeader.hpp"
#include "TimingArc.hpp"
#include "TimingCell.hpp"
#include "TimingLibrary.hpp"

namespace ipw {

double getNormalizedThreshold(double threshold)
{
  if (threshold > 1.0) {
    return threshold * 0.01;
  }
  return threshold;
}

void adjustParasiticLoadThreshold(
    TimingArc& timing_arc,
    const std::string& load_pin,
    TransType trans_type,
    double& wire_delay,
    double& load_slew
)
{
  TimingCell* load_timing_cell = getThresholdTimingCell(load_pin);
  if (load_timing_cell == nullptr || load_timing_cell->get_library_name() == timing_arc.get_library_name()) {
    return;
  }

  double driver_output_threshold = trans_type == TransType::kFall ? timing_arc.get_output_threshold_pct_fall()
                                                                  : timing_arc.get_output_threshold_pct_rise();
  double driver_slew_lower_threshold = trans_type == TransType::kFall ? timing_arc.get_slew_lower_threshold_pct_fall()
                                                                      : timing_arc.get_slew_lower_threshold_pct_rise();
  double driver_slew_upper_threshold = trans_type == TransType::kFall ? timing_arc.get_slew_upper_threshold_pct_fall()
                                                                      : timing_arc.get_slew_upper_threshold_pct_rise();
  double driver_slew_derate = timing_arc.get_slew_derate();
  double load_input_threshold = getTimingCellInputThreshold(*load_timing_cell, trans_type);
  double load_slew_lower_threshold = getTimingCellSlewLowerThreshold(*load_timing_cell, trans_type);
  double load_slew_upper_threshold = getTimingCellSlewUpperThreshold(*load_timing_cell, trans_type);
  double load_slew_derate = load_timing_cell->get_slew_derate_from_library();

  driver_output_threshold = getNormalizedThreshold(driver_output_threshold);
  driver_slew_lower_threshold = getNormalizedThreshold(driver_slew_lower_threshold);
  driver_slew_upper_threshold = getNormalizedThreshold(driver_slew_upper_threshold);
  load_input_threshold = getNormalizedThreshold(load_input_threshold);
  load_slew_lower_threshold = getNormalizedThreshold(load_slew_lower_threshold);
  load_slew_upper_threshold = getNormalizedThreshold(load_slew_upper_threshold);

  double driver_slew_delta = driver_slew_upper_threshold - driver_slew_lower_threshold;
  double load_slew_delta = load_slew_upper_threshold - load_slew_lower_threshold;
  if (!(driver_slew_delta > PW_ERROR && load_slew_delta > PW_ERROR && driver_slew_derate > PW_ERROR
        && load_slew_derate > PW_ERROR)) {
    return;
  }

  double wire_delay_delta = load_slew * ((load_input_threshold - driver_output_threshold) / driver_slew_delta);
  if (trans_type == TransType::kFall) {
    wire_delay -= wire_delay_delta;
  } else {
    wire_delay += wire_delay_delta;
  }
  load_slew = load_slew * ((load_slew_delta / load_slew_derate) / (driver_slew_delta / driver_slew_derate));
}

bool isParasiticLoadSlewCompatible(TimingArc& timing_arc, const std::string& load_pin, TransType trans_type)
{
  // The optional resistance-ratio approximation is limited to matching slew
  // conventions. Preserve the receiver waveform when a library conversion is
  // needed; converting the driver slew alone can discard significant RC slew.
  TimingCell* load_timing_cell = getThresholdTimingCell(load_pin);
  if (load_timing_cell == nullptr) {
    return false;
  }
  double driver_lower_threshold = trans_type == TransType::kFall ? timing_arc.get_slew_lower_threshold_pct_fall()
                                                                 : timing_arc.get_slew_lower_threshold_pct_rise();
  double driver_upper_threshold = trans_type == TransType::kFall ? timing_arc.get_slew_upper_threshold_pct_fall()
                                                                 : timing_arc.get_slew_upper_threshold_pct_rise();
  double load_lower_threshold = getTimingCellSlewLowerThreshold(*load_timing_cell, trans_type);
  double load_upper_threshold = getTimingCellSlewUpperThreshold(*load_timing_cell, trans_type);
  return std::abs(getNormalizedThreshold(driver_lower_threshold) - getNormalizedThreshold(load_lower_threshold))
             < PW_ERROR
         && std::abs(getNormalizedThreshold(driver_upper_threshold) - getNormalizedThreshold(load_upper_threshold))
                < PW_ERROR
         && std::abs(timing_arc.get_slew_derate() - load_timing_cell->get_slew_derate_from_library()) < PW_ERROR;
}

TimingCell* getThresholdTimingCell(uint32_t pin_index)
{
  // PinStaticContext 在建图时已经做过同样的解析（port/悬空实例/黑盒 -> nullptr，
  // 否则指向 library 里那个 TimingCell），所以按 pin 索引取是 O(1)，
  // 省掉原来 6 次按名字的大 map 探测。
  const std::vector<Database::PinStaticContext>& pin_static_context_list
      = PWDM.getDatabase().get_pin_static_context_list();
  if (pin_index >= pin_static_context_list.size()) {
    return nullptr;
  }
  return pin_static_context_list[pin_index].timing_cell;
}

TimingCell* getThresholdTimingCell(const std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  auto pin_iter = database.get_pin_map().find(pin_name);
  if (pin_iter == database.get_pin_map().end()) {
    return nullptr;
  }
  return getThresholdTimingCell(static_cast<uint32_t>(pin_iter->second.get_index()));
}

double getTimingCellSlewLowerThreshold(TimingCell& timing_cell, TransType trans_type)
{
  if (trans_type == TransType::kFall) {
    return timing_cell.get_slew_lower_threshold_pct_fall();
  }
  return timing_cell.get_slew_lower_threshold_pct_rise();
}

double getTimingCellSlewUpperThreshold(TimingCell& timing_cell, TransType trans_type)
{
  if (trans_type == TransType::kFall) {
    return timing_cell.get_slew_upper_threshold_pct_fall();
  }
  return timing_cell.get_slew_upper_threshold_pct_rise();
}

double getTimingCellInputThreshold(TimingCell& timing_cell, TransType trans_type)
{
  if (trans_type == TransType::kFall) {
    return timing_cell.get_input_threshold_pct_fall();
  }
  return timing_cell.get_input_threshold_pct_rise();
}

}  // namespace ipw

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

#include "ConnectType.hpp"
#include "Logger.hpp"
#include "Net.hpp"
#include "RTHeader.hpp"
#include "Utility.hpp"

namespace irt {

inline constexpr int32_t kDetourTopNum = 10;
inline constexpr double kDetourMinSteinerLengthUm = 5.0;

struct NetDetourStat
{
  std::string net_name;
  double routed_length = 0;
  double steiner_length = 0;
  double detour_ratio = 0;
};

struct NetDetourSummary
{
  std::vector<NetDetourStat> top_detour_net_list;
  int32_t valid_net_num = 0;
  double avg_detour_ratio = 0;
};

class DetourStat
{
 public:
  static void report(const std::map<int32_t, double>& routed_length_map, const std::map<int32_t, double>& steiner_length_map,
                     std::vector<Net>& net_list, const std::string& stage)
  {
    std::vector<NetDetourStat> clock_stat_list;
    std::vector<NetDetourStat> signal_stat_list;
    for (auto& [net_idx, routed_length] : routed_length_map) {
      auto iter = steiner_length_map.find(net_idx);
      if (iter == steiner_length_map.end() || iter->second <= 0) {
        continue;
      }
      NetDetourStat stat;
      bool is_clock = false;
      if (0 <= net_idx && net_idx < static_cast<int32_t>(net_list.size())) {
        Net& net = net_list[net_idx];
        stat.net_name = net.get_net_name();
        is_clock = net.get_connect_type() == ConnectType::kClock;
      }
      stat.routed_length = routed_length;
      stat.steiner_length = iter->second;
      stat.detour_ratio = routed_length / iter->second;
      if (is_clock) {
        clock_stat_list.push_back(std::move(stat));
      } else {
        signal_stat_list.push_back(std::move(stat));
      }
    }

    std::vector<NetDetourStat> filtered_signal_stat_list;
    filtered_signal_stat_list.reserve(signal_stat_list.size());
    for (NetDetourStat& stat : signal_stat_list) {
      if (stat.steiner_length >= kDetourMinSteinerLengthUm) {
        filtered_signal_stat_list.push_back(std::move(stat));
      }
    }
    signal_stat_list = std::move(filtered_signal_stat_list);

    NetDetourSummary clock_summary = buildSummary(clock_stat_list);
    NetDetourSummary signal_summary = buildSummary(signal_stat_list);
    printSummary(clock_summary, RTUTIL.getString(stage, " clock"));
    printSummary(signal_summary, RTUTIL.getString(stage, " signal"));
  }

 private:
  static NetDetourSummary buildSummary(std::vector<NetDetourStat>& stat_list)
  {
    NetDetourSummary summary;
    summary.valid_net_num = static_cast<int32_t>(stat_list.size());
    double total_ratio = 0;
    for (const NetDetourStat& stat : stat_list) {
      total_ratio += stat.detour_ratio;
    }
    if (summary.valid_net_num > 0) {
      summary.avg_detour_ratio = total_ratio / summary.valid_net_num;
    }
    std::ranges::sort(stat_list, [](const NetDetourStat& first, const NetDetourStat& second) {
      if (first.detour_ratio != second.detour_ratio) {
        return first.detour_ratio > second.detour_ratio;
      }
      return first.routed_length > second.routed_length;
    });
    if (static_cast<int32_t>(stat_list.size()) > kDetourTopNum) {
      stat_list.resize(kDetourTopNum);
    }
    summary.top_detour_net_list = std::move(stat_list);
    return summary;
  }

  static void printSummary(const NetDetourSummary& summary, const std::string& stage)
  {
    RTLOG.info(Loc::current(), stage, " detour: valid_net=", summary.valid_net_num, ", avg_ratio=",
               RTUTIL.formatByTwoDecimalPlaces(summary.avg_detour_ratio));
    fort::char_table detour_table;
    detour_table.set_cell_text_align(fort::text_align::right);
    detour_table << fort::header << "net"
                 << "routed(um)"
                 << "steiner(um)"
                 << "ratio" << fort::endr;
    for (const NetDetourStat& stat : summary.top_detour_net_list) {
      detour_table << stat.net_name << RTUTIL.formatByTwoDecimalPlaces(stat.routed_length)
                   << RTUTIL.formatByTwoDecimalPlaces(stat.steiner_length) << RTUTIL.formatByTwoDecimalPlaces(stat.detour_ratio) << fort::endr;
    }
    RTUTIL.printTableList({detour_table});
  }
};

}  // namespace irt

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

#include <cstdint>
#include <cstring>
#include <cstdint>
#include <functional>
#include <string>
#include <tuple>

#include "AnalysisType.hpp"
#include "TransType.hpp"

namespace ipw {

class TimingArc;

inline std::uint64_t getDoubleBits(double value)
{
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

// 降阶模型（Arnoldi / DMP）的 key：用 net 名在 ParasiticNet 内的稳定地址 + 节点 id 代替
// 两个 std::string。旧写法每次探测都要拷两次字符串并在红黑树里做字符串比较，
// 实测占掉 SignalPropagator 近一半时间（salsa20: 4.91s 里 3.44s 是探测本身）。
struct ParasiticModelKey
{
  const std::string* net_name = nullptr;  // 指向 ParasiticNet 内的 net 名（生命期内稳定）
  uint32_t source_node_id = 0;            // ParasiticGraph 的节点 id
  AnalysisType analysis_type = AnalysisType::kNone;
  TransType trans_type = TransType::kNone;

  bool operator==(const ParasiticModelKey& other) const
  {
    return net_name == other.net_name && source_node_id == other.source_node_id
           && analysis_type == other.analysis_type && trans_type == other.trans_type;
  }
};

struct ParasiticModelKeyHash
{
  std::size_t operator()(const ParasiticModelKey& key) const
  {
    std::size_t seed = std::hash<const std::string*>()(key.net_name);
    seed ^= static_cast<std::size_t>(key.source_node_id) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    seed ^= static_cast<std::size_t>(key.analysis_type) * 0x100000001b3ULL + (seed << 6) + (seed >> 2);
    seed ^= static_cast<std::size_t>(key.trans_type) * 0x100000001b3ULL + (seed << 6) + (seed >> 2);
    return seed;
  }
};

struct ArcTimingKey
{
  std::string output_pin;
  const TimingArc* timing_arc = nullptr;
  AnalysisType analysis_type = AnalysisType::kNone;
  TransType trans_type = TransType::kNone;
  double input_slew = 0.0;

  bool operator<(const ArcTimingKey& other) const
  {
    if (output_pin != other.output_pin) {
      return output_pin < other.output_pin;
    }
    if (timing_arc != other.timing_arc) {
      return std::less<const TimingArc*>()(timing_arc, other.timing_arc);
    }
    if (analysis_type != other.analysis_type) {
      return analysis_type < other.analysis_type;
    }
    if (trans_type != other.trans_type) {
      return trans_type < other.trans_type;
    }
    return getDoubleBits(input_slew) < getDoubleBits(other.input_slew);
  }
};

struct ArcDriverKey
{
  std::string name;
  AnalysisType analysis_type = AnalysisType::kNone;
  TransType trans_type = TransType::kNone;
  double driver_slew = 0.0;

  bool operator<(const ArcDriverKey& other) const
  {
    if (name != other.name) {
      return name < other.name;
    }
    if (analysis_type != other.analysis_type) {
      return analysis_type < other.analysis_type;
    }
    if (trans_type != other.trans_type) {
      return trans_type < other.trans_type;
    }
    return getDoubleBits(driver_slew) < getDoubleBits(other.driver_slew);
  }
};

}  // namespace ipw

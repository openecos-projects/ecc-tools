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

#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "DelayCacheKey.hpp"
#include "ParasiticArnoldiModel.hpp"
#include "ParasiticCornerState.hpp"
#include "ParasiticDelayResult.hpp"
#include "ParasiticDmpModel.hpp"
#include "ParasiticGraph.hpp"

namespace ipw {

#define PWDCACHE (ipw::DelayCache::threadCache())

// 延时计算的缓存全部收口在这里：
// - 每个线程一份（峰值流程按窗口并行，各线程不能共享可写缓存）
// - 结果类缓存用 getOrBuild：命中直接返回，未命中才用 builder 构造
// - RC 图（ParasiticGraph）与每 corner 的节点数值按 net / corner 存成稠密数组
class DelayCache
{
 public:
  DelayCache() = default;
  ~DelayCache() = default;

  static DelayCache& threadCache();

  void clear();

  // ===== 结果类缓存（DMP / Arnoldi）=====

  template <typename Builder>
  ParasiticDmpModel& dmpModel(const ParasiticModelKey& key, Builder&& builder)
  {
    return getOrBuild(_parasitic_dmp_model_map, key, std::forward<Builder>(builder));
  }

  template <typename Builder>
  ParasiticDelayResult& dmpTimingResult(const ArcTimingKey& key, Builder&& builder)
  {
    return getOrBuild(_parasitic_dmp_timing_result_map, key, std::forward<Builder>(builder));
  }

  template <typename Builder>
  ParasiticArnoldiModel& arnoldiModel(const ParasiticModelKey& key, Builder&& builder)
  {
    return getOrBuild(_parasitic_arnoldi_model_map, key, std::forward<Builder>(builder));
  }

  template <typename Builder>
  ParasiticDelayResult& arnoldiTimingResult(const ArcTimingKey& key, Builder&& builder)
  {
    return getOrBuild(_parasitic_arnoldi_timing_result_map, key, std::forward<Builder>(builder));
  }

  // 驱动侧结果缓存（cacheArnoldiDriverResult / cacheDmpDriverResult）已移到
  // SharedDriverResultCache：它影响数值（命中与否会切换斜率模型），必须全局共享才
  // 能让并行执行与串行执行逐位一致。

  template <typename Builder>
  ParasiticDelayResult& inputPortResult(const ArcDriverKey& key, Builder&& builder)
  {
    return getOrBuild(_parasitic_input_port_result_map, key, std::forward<Builder>(builder));
  }

  // ===== 索引化的 RC 图与逐 corner 数值（按 net / 节点 id 访问）=====

  // 逐节点数值容器，见 ParasiticCornerState.hpp；保留嵌套名以免改动既有调用点。
  using ParasiticCornerState = ::ipw::ParasiticCornerState;

  // 一次 try_emplace 同时给出"取或建"：命中不拷贝字符串，未命中才插入空条目，
  // 调用方按 inserted 决定是否构建。旧写法（count + operator[]）要探测两次。
  std::pair<ParasiticGraph&, bool> parasiticGraph(const std::string& net_name)
  {
    auto [iter, inserted] = _parasitic_graph_map.try_emplace(net_name);
    return {iter->second, inserted};
  }

  ParasiticCornerState& parasiticCornerState(
      const std::string& net_name,
      AnalysisType analysis_type,
      TransType trans_type
  )
  {
    return _parasitic_corner_state_map[net_name][analysis_type][trans_type];
  }

 private:
  template <typename Map, typename Key, typename Builder>
  static typename Map::mapped_type& getOrBuild(Map& cache, const Key& key, Builder&& builder)
  {
    auto [iter, inserted] = cache.try_emplace(key);
    if (inserted) {
      iter->second = builder();
    }
    return iter->second;
  }

  std::map<std::string, ParasiticGraph> _parasitic_graph_map;
  std::map<std::string, std::map<AnalysisType, std::map<TransType, ParasiticCornerState>>> _parasitic_corner_state_map;
  std::unordered_map<ParasiticModelKey, ParasiticDmpModel, ParasiticModelKeyHash> _parasitic_dmp_model_map;
  std::map<ArcTimingKey, ParasiticDelayResult> _parasitic_dmp_timing_result_map;
  std::unordered_map<ParasiticModelKey, ParasiticArnoldiModel, ParasiticModelKeyHash> _parasitic_arnoldi_model_map;
  std::map<ArcTimingKey, ParasiticDelayResult> _parasitic_arnoldi_timing_result_map;
  std::map<ArcDriverKey, ParasiticDelayResult> _parasitic_input_port_result_map;
};

}  // namespace ipw

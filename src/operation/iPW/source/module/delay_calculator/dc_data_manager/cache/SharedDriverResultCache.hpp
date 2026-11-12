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

#include <array>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>

#include "DelayCacheKey.hpp"
#include "ParasiticDelayResult.hpp"

namespace ipw {

// 驱动侧延时结果缓存：原本在 thread_local DelayCache 里，改为进程内共享。
//
// 为什么必须共享：net 弧的斜率有两条模型路径 —— 命中 driver 结果缓存时走 Arnoldi
// 逐 term 模型，未命中时落到 buildParasiticDelayMap 的 impulse 兜底。缓存若按线程
// 隔离，"命中/未命中"就会随线程划分变化，同一 net 弧会算出不同数值（实测总功耗
// 1e-3 % 级、末位差）——不是舍入，而是换了一套近似。共享之后命中集合与串行执行
// 完全一致，结果逐位不变。
//
// 并发写法：按 key 分片加锁（kShardNum 个分片），插入即发布。
// 注意：不能改成"桶边界批量发布"—— 同一桶内存在"刚写入就被读到"的依赖
//（实测 1 线程下延迟发布会改变结果 8.6041 -> 8.6046 mW），必须保持立即插入语义。
// 同 key 的结果由同一组输入唯一确定，所以用 try_emplace 插入后不再改写，
// 取出的指针也不会失效。
class SharedDriverResultCache
{
 public:
  enum class Kind
  {
    kArnoldi,
    kDmp
  };

  static SharedDriverResultCache& getInst()
  {
    static SharedDriverResultCache instance;
    return instance;
  }

  ParasiticDelayResult* find(Kind kind, const ArcDriverKey& key)
  {
    Shard& shard = shardOf(kind, key);
    std::lock_guard<std::mutex> guard(shard.mutex);
    auto iter = shard.map.find(key);
    return iter == shard.map.end() ? nullptr : &iter->second;
  }

  void insert(Kind kind, const ArcDriverKey& key, const ParasiticDelayResult& value)
  {
    Shard& shard = shardOf(kind, key);
    std::lock_guard<std::mutex> guard(shard.mutex);
    shard.map.try_emplace(key, value);
  }

  void clear()
  {
    for (Shard& shard : _shard_list) {
      std::lock_guard<std::mutex> guard(shard.mutex);
      shard.map.clear();
    }
  }

 private:
  SharedDriverResultCache() = default;
  ~SharedDriverResultCache() = default;
  SharedDriverResultCache(const SharedDriverResultCache& other) = delete;
  SharedDriverResultCache(SharedDriverResultCache&& other) = delete;
  SharedDriverResultCache& operator=(const SharedDriverResultCache& other) = delete;
  SharedDriverResultCache& operator=(SharedDriverResultCache&& other) = delete;

  static constexpr std::size_t kShardNum = 256;

  struct Shard
  {
    std::mutex mutex;
    std::map<ArcDriverKey, ParasiticDelayResult> map;
  };

  static std::size_t shardIndex(Kind kind, const ArcDriverKey& key)
  {
    std::size_t seed = std::hash<std::string>()(key.name);
    seed ^= static_cast<std::size_t>(kind) * 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    seed ^= static_cast<std::size_t>(key.analysis_type) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    seed ^= static_cast<std::size_t>(key.trans_type) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    seed ^= static_cast<std::size_t>(getDoubleBits(key.driver_slew)) + 0x9e3779b97f4a7c15ULL + (seed << 6)
            + (seed >> 2);
    return seed % kShardNum;
  }

  Shard& shardOf(Kind kind, const ArcDriverKey& key) { return _shard_list[shardIndex(kind, key)]; }

  std::array<Shard, kShardNum> _shard_list;
};

}  // namespace ipw

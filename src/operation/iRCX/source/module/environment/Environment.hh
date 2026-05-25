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

#include <map>
#include <vector>

#include "Types.hh"
#include "EnvPool.hh"
#include "Pixel.hh"
#include "Track.hh"
namespace ircx {

class TopoPool;
class LayoutData;

class Environment final
{
 public:
  Environment() = default;
  ~Environment() = default;

  // Disallow copy/move
  Environment(const Environment&) = delete;
  Environment& operator=(const Environment&) = delete;
  Environment(Environment&&) = delete;
  Environment& operator=(Environment&&) = delete;

  void set_layout_data(const LayoutData* v) { layout_data_ = v; }
  void set_topo_pool(const TopoPool* v) { topo_pool_ = v; }

  void reset();
  [[nodiscard]] bool buildNetEnvPools();

  // getter
  const std::vector<EnvPool>& net_env_pools() const { return net_env_pools_; }
  const EnvPool& net_env_pool(Size net_id) const {
    LOG_FATAL_IF(net_id >= net_env_pools_.size()) << "net_id out of range.";
    return net_env_pools_[net_id];
  }
 private:
  [[nodiscard]] bool buildTracks();
  [[nodiscard]] bool buildPixels();
  void buildSearchTrackNumMap();

  const LayoutData* layout_data_{nullptr};
  const TopoPool* topo_pool_{nullptr};

  F32 bucket_size_um_{kDefaultBucketUm};
  F32 window_size_um_{kDefaultWindowUm};

  Size cross_layer_{3};

  std::map<Size, Pixel> layer_to_pixel_prefer_dir_;
  std::map<Size, Pixel> layer_to_pixel_nonprefer_dir_;
  std::map<Size, Track> layer_to_track_;  // preferred routing direction only
  std::map<Size, Dbu> layer_to_search_track_num_;

  std::vector<EnvPool> net_env_pools_;
};

} // namespace ircx

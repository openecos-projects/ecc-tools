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
#include <algorithm>
#include <omp.h>
#include <vector>

#include "Environment.hh"
#include "IntervalUtils.hh"
#include "LayoutData.hh"
#include "TopoPool.hh"
#include "IntervalEngine.hh"
#include "log/Log.hh"
namespace ircx {

void Environment::reset()
{
  layout_data_ = nullptr;
  topo_pool_ = nullptr;
  layer_to_pixel_prefer_dir_.clear();
  layer_to_pixel_nonprefer_dir_.clear();
  layer_to_track_.clear();
  layer_to_search_track_num_.clear();
}

bool Environment::buildTracks()
{
  if (!layout_data_) {
    LOG_ERROR << "build environment tracks failed: LayoutData not initialized.";
    return false;
  }
  if (!topo_pool_) {
    LOG_ERROR << "build environment tracks failed: TopoPool not initialized.";
    return false;
  }

  const std::map<Size, RoutingLayer>& routing_layers = layout_data_->routing_layers;

  const GtlRectI& rect = layout_data_->die_shape;
  Dbu die_x0 = geom::MinX(rect);
  Dbu die_y0 = geom::MinY(rect);
  Dbu die_x1 = geom::MaxX(rect);
  Dbu die_y1 = geom::MaxY(rect);
  Dbu die_dx = geom::DeltaX(rect);
  Dbu die_dy = geom::DeltaY(rect);

  Dbu bucket_dlt = static_cast<Dbu>(bucket_size_um_ * layout_data_->micron_to_dbu);

  layer_to_track_.clear();

  // init
  for (const auto& [lid, layer] : routing_layers) {
    bool is_horz = layer.is_prefer_horz();
    const RoutingLayer::TrackInfo& ti = layer.track_info();
    Track track;

    Dbu track_ori = is_horz ? ti.y0 : ti.x0;
    Dbu track_num = is_horz ? ti.ny : ti.nx;
    Dbu track_dlt = is_horz ? ti.dy : ti.dx;

    if (is_horz) {
      while(track_ori > die_y0) {
        track_ori -= track_dlt;
        track_num += 1;
      }
      while(track_ori + track_dlt * track_num <= die_y1) track_num += 1;
    } else {
      while(track_ori > die_x0) {
        track_ori -= track_dlt;
        track_num += 1;
      }
      while(track_ori + track_dlt * track_num <= die_x1) track_num += 1;
    }

    track.set_track_ori(track_ori);
    track.set_track_num(track_num);
    track.set_track_dlt(track_dlt);

    Dbu bucket_len = is_horz ? die_dx : die_dy;
    Dbu bucket_num = (bucket_len + bucket_dlt - 1) / bucket_dlt;
    track.set_bucket_ori(is_horz ? die_x0 : die_y0);
    track.set_bucket_num(bucket_num);
    track.set_bucket_dlt(bucket_dlt);

    if (!track.initTrack()) {
      LOG_ERROR << "build environment tracks failed on layer " << lid;
      return false;
    }
    layer_to_track_[lid] = std::move(track);
  }

  // build: regular edges
  const std::vector<TopoEdge>& edge_pool = topo_pool_->edge_pool();
  for (const TopoEdge& edge : edge_pool) {
    if (edge.is_via()) continue;

    Size lid = edge.layer_id();
    bool layer_is_horz = routing_layers.at(lid).is_prefer_horz();

    if (edge.is_horz() == layer_is_horz) {
      layer_to_track_.at(lid).addEdge(edge);
    }
  }

  // build: special-net edges (power/ground context)
  for (const TopoEdge& edge : topo_pool_->special_edge_pool()) {
    if (edge.is_via()) continue;

    Size lid = edge.layer_id();
    bool layer_is_horz = routing_layers.at(lid).is_prefer_horz();

    if (edge.is_horz() == layer_is_horz) {
      layer_to_track_.at(lid).addEdge(edge);
    }
  }

  return true;
}

bool Environment::buildPixels()
{
  if (!layout_data_) {
    LOG_ERROR << "build environment pixels failed: LayoutData not initialized.";
    return false;
  }
  if (!topo_pool_) {
    LOG_ERROR << "build environment pixels failed: TopoPool not initialized.";
    return false;
  }

  const std::map<Size, RoutingLayer>& routing_layers = layout_data_->routing_layers;

  const GtlRectI& rect = layout_data_->die_shape;
  Dbu die_x0 = geom::MinX(rect);
  Dbu die_y0 = geom::MinY(rect);
  Dbu die_x1 = geom::MaxX(rect);
  Dbu die_y1 = geom::MaxY(rect);

  layer_to_pixel_prefer_dir_.clear();
  layer_to_pixel_nonprefer_dir_.clear();

  // init
  for (const auto& [lid, layer] : routing_layers) {
    const RoutingLayer::TrackInfo& ti = layer.track_info();
    Pixel pixel;

    Dbu x0 = ti.x0;
    Dbu y0 = ti.y0;
    Dbu nx = ti.nx;
    Dbu ny = ti.ny;
    Dbu dx = ti.dx;
    Dbu dy = ti.dy;
    if (layer.is_prefer_horz()) {
      dx = layer.layer_width();
    } else {
      dy = layer.layer_width();
    }

    while(x0 > die_x0) {
      x0 -= dx;
      nx += 1;
    }
    while(x0 + dx * nx <= die_x1) nx += 1;

    while(y0 > die_y0) {
      y0 -= dy;
      ny += 1;
    }
    while(y0 + dy * ny <= die_y1) ny += 1;

    pixel.set_x0(x0);
    pixel.set_nx(nx);
    pixel.set_dx(dx);
    
    pixel.set_y0(y0);
    pixel.set_ny(ny);
    pixel.set_dy(dy);

    if (!pixel.initPixel()) {
      LOG_ERROR << "build environment pixels failed on layer " << lid;
      return false;
    }
    layer_to_pixel_prefer_dir_[lid] = pixel;
    layer_to_pixel_nonprefer_dir_[lid] = std::move(pixel);
  }

  auto add_pixel_edge = [&](const TopoEdge& edge) {
    if (edge.is_via()) {
      return;
    }

    Size lid = edge.layer_id();
    bool layer_is_horz = routing_layers.at(lid).is_prefer_horz();

    if (edge.is_horz() == layer_is_horz) {
      layer_to_pixel_prefer_dir_.at(lid).addEdge(edge);
    } else {
      layer_to_pixel_nonprefer_dir_.at(lid).addEdge(edge);
    }
  };

  // build: regular edges
  for (const TopoEdge& edge : topo_pool_->edge_pool()) {
    add_pixel_edge(edge);
  }

  // build: special-net edges (power/ground context)
  for (const TopoEdge& edge : topo_pool_->special_edge_pool()) {
    add_pixel_edge(edge);
  }

  return true;
}

void Environment::buildSearchTrackNumMap()
{
  const std::map<Size, RoutingLayer>& routing_layers = layout_data_->routing_layers;

  layer_to_search_track_num_.clear();

  for (const auto& [lid, layer] : routing_layers) {
    // Dbu window_size = static_cast<Dbu>(window_size_um_ * layout_data_->micron_to_dbu);
    // layer_to_search_track_num_[lid] = window_size / layer_to_track_[lid].track_dlt();
    layer_to_search_track_num_[lid] = 10;
  }
}

bool Environment::buildNetEnvPools(std::vector<EnvPool>& net_env_pools)
{
  if (!layout_data_) {
    LOG_ERROR << "build environment failed: LayoutData not initialized.";
    return false;
  }
  if (!topo_pool_) {
    LOG_ERROR << "build environment failed: TopoPool not initialized.";
    return false;
  }

  if (!buildTracks() || !buildPixels()) {
    return false;
  }
  buildSearchTrackNumMap();

  Size net_num = layout_data_->regular_net_count();
  net_env_pools.clear();
  net_env_pools.resize(net_num);

  const std::map<Size, RoutingLayer>& routing_layers = layout_data_->routing_layers;
  const Size min_lid = routing_layers.empty() ? 0 : routing_layers.begin()->first;
  const Size max_lid = routing_layers.empty() ? 0 : routing_layers.rbegin()->first;

  auto widen_me = [](const LineSegmentI& seg, Dbu ext) {
    LineSegmentI out = seg;
    out.a0 -= ext;
    out.a1 += ext;
    return out;
  };

  auto clip_cross_segs = [](const std::vector<CrossOverlapSub>& full, Dbu a0, Dbu a1) {
    return clipIntervals(
        full,
        a0,
        a1,
        [](const CrossOverlapSub& lhs, const CrossOverlapSub& rhs) {
          return lhs.blw_layer == rhs.blw_layer && lhs.abv_layer == rhs.abv_layer;
        });
  };

  TrackOverlapMerge track_merger;
  PixelOverlapMerge pixel_merger;

  auto collect_cross_side = [&](const LineSegmentI& full_seg, Size base_lid, bool search_up) {
    std::vector<PixelOverlapMerge::LayerPixelOverlaps> bufs;

    for (Size delta = 1; delta <= cross_layer_; ++delta) {
      Size cand_lid = 0;

      if (search_up) {
        if (base_lid > max_lid || max_lid - base_lid < delta) {
          break;
        }
        cand_lid = base_lid + delta;
      } else {
        if (base_lid < min_lid || base_lid - min_lid < delta) {
          break;
        }
        cand_lid = base_lid - delta;
      }

      auto it_layer = routing_layers.find(cand_lid);
      if (it_layer == routing_layers.end()) {
        continue;
      }

      // Cross-over only queries the conductor set orthogonal to full_seg.
      const auto& pixel_map =
          (it_layer->second.is_prefer_horz() != full_seg.is_horz)
              ? layer_to_pixel_prefer_dir_
              : layer_to_pixel_nonprefer_dir_;

      auto it_pixel = pixel_map.find(cand_lid);
      if (it_pixel == pixel_map.end()) {
        continue;
      }

      std::vector<PixelOverlap> segs = it_pixel->second.get_overlap(full_seg);
      if (segs.empty()) {
        continue;
      }

      // Smaller layer deltas have higher priority in PixelOverlapMerge.
      PixelOverlapMerge::LayerPixelOverlaps in;
      in.layer = cand_lid;
      in.segs = std::move(segs);
      bufs.push_back(std::move(in));
    }

    return bufs;
  };

  #pragma omp parallel for schedule(dynamic)
  for (Size nid = 0; nid < net_num; nid++) {
    EnvPool& net_env_pool = net_env_pools[nid];
    net_env_pool.clear();

    for (const TopoEdge& edge : topo_pool_->net_edges(nid)) {
      if (edge.is_via()) {
        net_env_pool.append_edge_env_interval_pool({});  // placeholder to keep index aligned with TopoPool
        continue;
      }

      const Size lid = edge.layer_id();
      const LineSegmentI query_seg = widen_me(edge.line_segment(), 0);

      std::vector<TrackOverlap> track_ov_up =
          layer_to_track_[lid].get_overlap(query_seg,  layer_to_search_track_num_[lid], nullptr);
      std::vector<TrackOverlap> track_ov_dn =
          layer_to_track_[lid].get_overlap(query_seg, -layer_to_search_track_num_[lid], nullptr);

      std::vector<EnvInterval> out;
      track_merger.compute(query_seg.a0, query_seg.a1, track_ov_dn, track_ov_up, out);

      const auto dn_inputs = collect_cross_side(query_seg, lid, /*search_up=*/false);
      const auto up_inputs = collect_cross_side(query_seg, lid, /*search_up=*/true);

      std::vector<CrossOverlapSub> cross_full;
      pixel_merger.compute(query_seg.a0, query_seg.a1, dn_inputs, up_inputs, cross_full);

      for (EnvInterval& interval : out) {
        interval.cross_segs = clip_cross_segs(cross_full, interval.a0, interval.a1);
      }

      net_env_pool.append_edge_env_interval_pool(std::move(out));
    }
  }

  return true;
}

} // namespace ircx

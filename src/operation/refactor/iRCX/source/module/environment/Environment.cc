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
/**
 * @file Environment.cc
 * @brief iRCX module implementation detail.
 */
#include "Environment.hh"

#include <algorithm>
#include <vector>

#include "IntervalEngine.hh"
#include "IntervalUtils.hh"
#include "LayoutData.hh"
#include "NetEnvironment.hh"
#include "ParallelUtils.hh"
#include "TopoPool.hh"
#include "log/Log.hh"

namespace ircx {

namespace {

struct Axis
{
  Dbu origin{0};
  Dbu count{0};
  Dbu step{0};
};

auto ceilDivPositive(Dbu value,
                     Dbu divisor) -> Dbu
{
  if (value <= 0 || divisor <= 0) {
    return 0;
  }
  return static_cast<Dbu>((static_cast<I64>(value) + divisor - 1) / divisor);
}

auto coverAxis(Dbu origin,
               Dbu count,
               Dbu step,
               Dbu lo,
               Dbu hi) -> Axis
{
  if (step <= 0) {
    return {origin, count, step};
  }

  I64 axis_origin = origin;
  I64 axis_count = count;
  const I64 axis_step = step;

  if (axis_origin > lo) {
    const I64 shift = (axis_origin - lo + axis_step - 1) / axis_step;
    axis_origin -= shift * axis_step;
    axis_count += shift;
  }

  const I64 covered_hi = axis_origin + axis_step * axis_count;
  if (covered_hi <= hi) {
    axis_count += (static_cast<I64>(hi) - covered_hi) / axis_step + 1;
  }

  return {static_cast<Dbu>(axis_origin), static_cast<Dbu>(axis_count), step};
}

auto initTrackForDirection(Track& track,
                           const RoutingLayer::TrackInfo& ti,
                           const GtlRectI& rect,
                           Dbu bucket_dlt,
                           bool is_horz) -> bool
{
  const Dbu die_x0 = geom::minX(rect);
  const Dbu die_y0 = geom::minY(rect);
  const Dbu die_x1 = geom::maxX(rect);
  const Dbu die_y1 = geom::maxY(rect);
  const Dbu die_dx = geom::deltaX(rect);
  const Dbu die_dy = geom::deltaY(rect);

  const Dbu track_ori = is_horz ? ti.y0 : ti.x0;
  const Dbu track_num = is_horz ? ti.ny : ti.nx;
  const Dbu track_dlt = is_horz ? ti.dy : ti.dx;
  const Dbu axis_lo = is_horz ? die_y0 : die_x0;
  const Dbu axis_hi = is_horz ? die_y1 : die_x1;
  const Axis track_axis = coverAxis(track_ori, track_num, track_dlt, axis_lo, axis_hi);

  track.set_track_origin(track_axis.origin);
  track.set_track_count(track_axis.count);
  track.set_track_step(track_axis.step);
  track.set_bucket_origin(is_horz ? die_x0 : die_y0);
  track.set_bucket_count(ceilDivPositive(is_horz ? die_dx : die_dy, bucket_dlt));
  track.set_bucket_step(bucket_dlt);
  return track.initTrack();
}

}  // namespace

void Environment::reset()
{
  layout_data_ = nullptr;
  topo_pool_ = nullptr;
  layer_to_pixel_prefer_dir_.clear();
  layer_to_pixel_nonprefer_dir_.clear();
  layer_to_track_prefer_dir_.clear();
  layer_to_track_nonprefer_dir_.clear();
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
  Dbu bucket_dlt = static_cast<Dbu>(bucket_size_um_ * layout_data_->dbu_per_micron);

  layer_to_track_prefer_dir_.clear();
  layer_to_track_nonprefer_dir_.clear();

  // init
  for (const auto& [lid, layer] : routing_layers) {
    const RoutingLayer::TrackInfo& ti = layer.get_track_info();

    Track prefer_track;
    if (!initTrackForDirection(prefer_track, ti, rect, bucket_dlt, layer.is_prefer_horz())) {
      LOG_ERROR << "build environment tracks failed on layer " << lid;
      return false;
    }
    layer_to_track_prefer_dir_[lid] = std::move(prefer_track);

    Track nonprefer_track;
    if (!initTrackForDirection(nonprefer_track, ti, rect, bucket_dlt, !layer.is_prefer_horz())) {
      LOG_ERROR << "build environment non-preferred tracks failed on layer " << lid;
      return false;
    }
    layer_to_track_nonprefer_dir_[lid] = std::move(nonprefer_track);
  }

  auto add_track_edge = [&](const TopoEdge& edge) {
    if (edge.is_via()) {
      return;
    }

    const Size lid = edge.get_layer_id();
    const bool layer_is_horz = routing_layers.at(lid).is_prefer_horz();
    auto& track_map = edge.is_horz() == layer_is_horz
                          ? layer_to_track_prefer_dir_
                          : layer_to_track_nonprefer_dir_;
    track_map.at(lid).addEdge(edge);
  };

  // build: regular edges
  const std::vector<TopoEdge>& edge_pool = topo_pool_->get_edge_pool();
  for (const TopoEdge& edge : edge_pool) {
    add_track_edge(edge);
  }

  // build: special-net edges (power/ground context)
  for (const TopoEdge& edge : topo_pool_->get_special_edge_pool()) {
    add_track_edge(edge);
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
  Dbu die_x0 = geom::minX(rect);
  Dbu die_y0 = geom::minY(rect);
  Dbu die_x1 = geom::maxX(rect);
  Dbu die_y1 = geom::maxY(rect);

  layer_to_pixel_prefer_dir_.clear();
  layer_to_pixel_nonprefer_dir_.clear();

  // init
  for (const auto& [lid, layer] : routing_layers) {
    const RoutingLayer::TrackInfo& ti = layer.get_track_info();
    Pixel pixel;

    Dbu x0 = ti.x0;
    Dbu y0 = ti.y0;
    Dbu nx = ti.nx;
    Dbu ny = ti.ny;
    Dbu dx = ti.dx;
    Dbu dy = ti.dy;
    // if (layer.is_prefer_horz()) {
    //   dx = layer.get_layer_width();
    // } else {
    //   dy = layer.get_layer_width();
    // }

    const auto x_axis = coverAxis(x0, nx, dx, die_x0, die_x1);
    const auto y_axis = coverAxis(y0, ny, dy, die_y0, die_y1);

    pixel.set_x0(x_axis.origin);
    pixel.set_nx(x_axis.count);
    pixel.set_dx(x_axis.step);
    
    pixel.set_y0(y_axis.origin);
    pixel.set_ny(y_axis.count);
    pixel.set_dy(y_axis.step);

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

    Size lid = edge.get_layer_id();
    bool layer_is_horz = routing_layers.at(lid).is_prefer_horz();

    if (edge.is_horz() == layer_is_horz) {
      layer_to_pixel_prefer_dir_.at(lid).addEdge(edge);
    } else {
      layer_to_pixel_nonprefer_dir_.at(lid).addEdge(edge);
    }
  };

  // build: regular edges
  for (const TopoEdge& edge : topo_pool_->get_edge_pool()) {
    add_pixel_edge(edge);
  }

  // build: special-net edges (power/ground context)
  for (const TopoEdge& edge : topo_pool_->get_special_edge_pool()) {
    add_pixel_edge(edge);
  }

  return true;
}

void Environment::buildSearchTrackNumMap()
{
  const std::map<Size, RoutingLayer>& routing_layers = layout_data_->routing_layers;

  layer_to_search_track_num_.clear();

  for (const auto& [lid, layer] : routing_layers) {
    // Dbu window_size = static_cast<Dbu>(window_size_um_ * layout_data_->dbu_per_micron);
    // layer_to_search_track_num_[lid] = window_size / layer_to_track_[lid].get_track_step();
    layer_to_search_track_num_[lid] = 10;
  }
}

bool Environment::buildNetEnvironments(std::vector<NetEnvironment>& net_environments)
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

  Size net_num = layout_data_->get_regular_net_count();
  net_environments.clear();
  net_environments.resize(net_num);

  const std::map<Size, RoutingLayer>& routing_layers = layout_data_->routing_layers;
  const Size min_lid = routing_layers.empty() ? 0 : routing_layers.begin()->first;
  const Size max_lid = routing_layers.empty() ? 0 : routing_layers.rbegin()->first;

  auto widen_segment = [](const LineSegmentI& seg, Dbu ext) {
    LineSegmentI out = seg;
    out.lo -= ext;
    out.hi += ext;
    return out;
  };

  auto clip_cross_segments = [](const std::vector<CrossOverlapSub>& full, Dbu a0, Dbu a1) {
    return ircx::interval::clip(
        full,
        a0,
        a1,
        [](const CrossOverlapSub& lhs, const CrossOverlapSub& rhs) {
          return lhs.blw_layer == rhs.blw_layer && lhs.abv_layer == rhs.abv_layer;
        });
  };

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

      std::vector<PixelOverlap> segs = it_pixel->second.overlap(full_seg);
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

  const int net_threads = parallel::threadCount(net_num);
#pragma omp parallel for schedule(dynamic) num_threads(net_threads)
  for (Size nid = 0; nid < net_num; nid++) {
    TrackOverlapMerge track_merger;
    PixelOverlapMerge pixel_merger;
    NetEnvironment& environment = net_environments[nid];
    environment.clear();

    for (const TopoEdge& edge : topo_pool_->get_net_edges(nid)) {
      if (edge.is_via()) {
        environment.appendEdgeIntervals({});  // placeholder to keep index aligned with TopoPool
        continue;
      }

      const Size lid = edge.get_layer_id();
      const LineSegmentI query_seg = widen_segment(edge.get_line_segment(), 0);

      std::vector<TrackOverlap> track_ov_up;
      std::vector<TrackOverlap> track_ov_dn;
      const bool layer_is_horz = routing_layers.at(lid).is_prefer_horz();
      const auto& track_map = edge.is_horz() == layer_is_horz
                                  ? layer_to_track_prefer_dir_
                                  : layer_to_track_nonprefer_dir_;
      if (const auto track_it = track_map.find(lid); track_it != track_map.end()) {
        track_ov_up =
            track_it->second.overlap(query_seg, layer_to_search_track_num_[lid], nullptr);
        track_ov_dn =
            track_it->second.overlap(query_seg, -layer_to_search_track_num_[lid], nullptr);
      }

      std::vector<EdgeEnvironmentInterval> out;
      track_merger.compute(query_seg.lo, query_seg.hi, track_ov_dn, track_ov_up, out);

      const auto dn_inputs = collect_cross_side(query_seg, lid, /*search_up=*/false);
      const auto up_inputs = collect_cross_side(query_seg, lid, /*search_up=*/true);

      std::vector<CrossOverlapSub> cross_full;
      pixel_merger.compute(query_seg.lo, query_seg.hi, dn_inputs, up_inputs, cross_full);

      for (EdgeEnvironmentInterval& interval : out) {
        interval.cross_segs = clip_cross_segments(cross_full, interval.a0, interval.a1);
      }

      environment.appendEdgeIntervals(std::move(out));
    }
  }

  return true;
}

} // namespace ircx

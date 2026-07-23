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
#include "RuleValidator.hpp"

namespace idrc {

void RuleValidator::verifyParallelRunLengthSpacing(RVCluster& rv_cluster)
{
  std::vector<RoutingLayer>& routing_layer_list = DRCDM.getDatabase().get_routing_layer_list();
  const auto& layer_data = rv_cluster.get_layer_data();

  for (auto& [routing_layer_idx, rv_layer_data] : layer_data) {
    RoutingLayer& routing_layer = routing_layer_list[routing_layer_idx];
    ParallelRunLengthSpacingRule& parallel_run_length_spacing_rule = routing_layer.get_parallel_run_length_spacing_rule();
    std::map<std::set<int32_t>, std::map<int32_t, std::vector<PlanarRect>>> net_required_violation_rect_map;
    std::map<std::set<int32_t>, std::map<int32_t, std::vector<PlanarRect>>> env_net_required_violation_rect_map;
    auto has_opposing_polygon_edges = [&](const PlanarRect& marker_rect, int32_t net_idx) {
      std::vector<std::pair<GTLRectInt, int32_t>> boundary_id_list;
      rv_layer_data.queryBoundaries(DRCUTIL.convertToGTLRectInt(marker_rect), std::back_inserter(boundary_id_list));

      bool has_left = false;
      bool has_right = false;
      bool has_bottom = false;
      bool has_top = false;
      for (const auto& [edge, boundary_id] : boundary_id_list) {
        if (rv_layer_data.getNetIdxByBoundaryId(boundary_id) != net_idx) {
          continue;
        }
        PlanarRect edge_rect = DRCUTIL.convertToPlanarRect(edge);
        int32_t x_overlap
            = std::min(edge_rect.get_ur_x(), marker_rect.get_ur_x()) - std::max(edge_rect.get_ll_x(), marker_rect.get_ll_x());
        int32_t y_overlap
            = std::min(edge_rect.get_ur_y(), marker_rect.get_ur_y()) - std::max(edge_rect.get_ll_y(), marker_rect.get_ll_y());
        if (edge_rect.getXSpan() == 0 && edge_rect.get_ll_x() == marker_rect.get_ll_x() && y_overlap >= 0) {
          has_left = true;
        } else if (edge_rect.getXSpan() == 0 && edge_rect.get_ll_x() == marker_rect.get_ur_x() && y_overlap >= 0) {
          has_right = true;
        } else if (edge_rect.getYSpan() == 0 && edge_rect.get_ll_y() == marker_rect.get_ll_y() && x_overlap >= 0) {
          has_bottom = true;
        } else if (edge_rect.getYSpan() == 0 && edge_rect.get_ll_y() == marker_rect.get_ur_y() && x_overlap >= 0) {
          has_top = true;
        }
      }
      return (has_left && has_right) || (has_bottom && has_top);
    };
    for (auto& [net_idx, routing_net] : rv_layer_data.nets) {
      for (const MaxRectData& max_rect_data : rv_layer_data.getMaxRects(routing_net)) {
        PlanarRect rect = DRCUTIL.convertToPlanarRect(max_rect_data.rect);
        if (net_idx == -1) {
          continue;
        }
        bool has_spacing_table = parallel_run_length_spacing_rule.has_spacing_table;
        bool has_spacing_list = parallel_run_length_spacing_rule.has_spacing_list;

        std::vector<std::pair<GTLRectInt, int32_t>> neighbor_rect_id_list;
        {
          int32_t spacing_table_check = has_spacing_table ? parallel_run_length_spacing_rule.getMaxSpacing() : 0;
          int32_t spacing_list_check = has_spacing_list ? parallel_run_length_spacing_rule.getSpacingMaxWidth() : 0;
          int32_t check_spacing = std::max(spacing_table_check, spacing_list_check);

          PlanarRect check_rect = DRCUTIL.getEnlargedRect(rect, check_spacing);
          rv_layer_data.queryMaxRects(DRCUTIL.convertToGTLRectInt(check_rect), std::back_inserter(neighbor_rect_id_list));
        }

        for (const auto& [gtl_rect, env_max_rect_id] : neighbor_rect_id_list) {
          PlanarRect env_rect = DRCUTIL.convertToPlanarRect(gtl_rect);
          const MaxRectData& env_max_rect_data = rv_layer_data.getMaxRect(env_max_rect_id);
          int32_t env_net_idx = rv_layer_data.getNetIdxByMaxRectId(env_max_rect_id);
          if (DRCUTIL.isClosedOverlap(rect, env_rect)) {
            continue;
          }

          PlanarRect violation_rect = DRCUTIL.getSpacingRect(rect, env_rect);
          bool is_prl_violation = false, is_spacing_violation = false;
          int32_t real_prl_spacing = 0, real_spacing = 0;
          // prl rules
          int32_t rect_width = max_rect_data.isObs ? routing_layer.get_width() : rect.getWidth();
          int32_t env_rect_width = env_max_rect_data.isObs ? routing_layer.get_width() : env_rect.getWidth();
          int32_t check_width = std::max(rect_width, env_rect_width);
          if (has_spacing_table) {
            int32_t prl = DRCUTIL.getParallelLength(rect, env_rect);
            real_prl_spacing = parallel_run_length_spacing_rule.getSpacing(check_width, prl);
            is_prl_violation = DRCUTIL.getEuclideanDistance(rect, env_rect) < real_prl_spacing;
          }

          // spacing rules
          if (has_spacing_list) {
            real_spacing = parallel_run_length_spacing_rule.getSpacingWithWidth(check_width);
            is_spacing_violation = DRCUTIL.getEuclideanDistance(rect, env_rect) < real_spacing;
          }

          if (!is_prl_violation && !is_spacing_violation) {
            continue;
          }

          // sameNet
          if (net_idx == env_net_idx) {
            bool total_inside = false;
            // for violation area = 0
            bool zero_area_inside = false;
            GTLPolySetInt violation_ps;
            violation_ps += DRCUTIL.convertToGTLRectInt(violation_rect);
            for (const auto& [gtl_rect, max_rect_id] : neighbor_rect_id_list) {
              PlanarRect violation_env_rect = DRCUTIL.convertToPlanarRect(gtl_rect);
              int32_t violation_env_net_idx = rv_layer_data.getNetIdxByMaxRectId(max_rect_id);
              if (violation_env_net_idx == net_idx) {
                if (DRCUTIL.isOpenOverlap(violation_env_rect, violation_rect)) {
                  violation_ps -= gtl_rect;
                }
                if (gtl::empty(violation_ps)) {
                  total_inside = true;
                }
                if (DRCUTIL.isInside(violation_env_rect, violation_rect)) {
                  zero_area_inside = true;
                }
              }
            }

            if (violation_rect.getArea() == 0) {
              if (zero_area_inside) {
                continue;
              }
            } else if (total_inside) {
              continue;
            } else {
              GTLRectInt violation_bbox;
              violation_ps.extents(violation_bbox);
              violation_rect = DRCUTIL.convertToPlanarRect(violation_bbox);
              if (!has_opposing_polygon_edges(violation_rect, net_idx)) {
                continue;
              }
            }
          }

          // diffNet
          if (net_idx != env_net_idx && DRCUTIL.getParallelLength(rect, env_rect) > 0) {
            bool total_inside = false;
            GTLPolySetInt violation_ps;
            violation_ps += DRCUTIL.convertToGTLRectInt(violation_rect);
            for (const auto& [gtl_rect, max_rect_id] : neighbor_rect_id_list) {
              int32_t violation_env_net_idx = rv_layer_data.getNetIdxByMaxRectId(max_rect_id);
              const MaxRectData& violation_env_max_rect_data = rv_layer_data.getMaxRect(max_rect_id);
              bool is_unrelated_filler = violation_env_net_idx != net_idx && violation_env_net_idx != env_net_idx;
              bool preserve_pair_marker = violation_env_max_rect_data.isObs || max_rect_data.isSpecialNet || env_max_rect_data.isSpecialNet;
              if (is_unrelated_filler && preserve_pair_marker) {
                continue;
              }
              PlanarRect violation_env_rect = DRCUTIL.convertToPlanarRect(gtl_rect);
              if (DRCUTIL.isOpenOverlap(violation_env_rect, violation_rect)) {
                violation_ps -= gtl_rect;
              }
            }
            if (gtl::empty(violation_ps)) {
              total_inside = true;
            }
            GTLRectInt violation_bbox;
            violation_ps.extents(violation_bbox);
            PlanarRect new_violation_rect = DRCUTIL.convertToPlanarRect(violation_bbox);
            if (!DRCUTIL.isClosedOverlap(new_violation_rect, rect) || !DRCUTIL.isClosedOverlap(new_violation_rect, env_rect)) {
              total_inside = true;
            }

            if (total_inside) {
              continue;
            }
          }

          if (max_rect_data.isEnv && env_max_rect_data.isEnv) {
            if (is_prl_violation) {
              env_net_required_violation_rect_map[{net_idx, env_net_idx}][real_prl_spacing].push_back(violation_rect);
            }
            if (is_spacing_violation) {
              env_net_required_violation_rect_map[{net_idx, env_net_idx}][real_spacing].push_back(violation_rect);
            }
          } else {
            if (is_prl_violation) {
              net_required_violation_rect_map[{net_idx, env_net_idx}][real_prl_spacing].push_back(violation_rect);
            }
            if (is_spacing_violation) {
              net_required_violation_rect_map[{net_idx, env_net_idx}][real_spacing].push_back(violation_rect);
            }
          }
        }

        if (!max_rect_data.isEnv) {
          int32_t max_check_spacing = 0;
          if (has_spacing_table) {
            max_check_spacing = parallel_run_length_spacing_rule.getMaxSpacing();
          }
          if (has_spacing_list) {
            max_check_spacing = std::max(max_check_spacing, parallel_run_length_spacing_rule.getSpacingMaxWidth());
          }

          PlanarRect check_rect = DRCUTIL.getEnlargedRect(rect, max_check_spacing);
          std::vector<EnvRoutingShapeData> env_shape_list;
          rv_layer_data.queryEnvRoutingShapes(DRCUTIL.convertToGTLRectInt(check_rect), std::back_inserter(env_shape_list));
          for (const EnvRoutingShapeData& env_shape_data : env_shape_list) {
            if (!env_shape_data.isObsCovered || env_shape_data.source_type != ids::Shape::SourceType::kInstancePin) {
              continue;
            }

            PlanarRect env_rect = DRCUTIL.convertToPlanarRect(env_shape_data.rect);
            if (DRCUTIL.isClosedOverlap(rect, env_rect)) {
              continue;
            }

            int32_t check_width = std::max(rect.getWidth(), env_rect.getWidth());
            if (has_spacing_table) {
              int32_t prl = DRCUTIL.getParallelLength(rect, env_rect);
              int32_t real_prl_spacing = parallel_run_length_spacing_rule.getSpacing(check_width, prl);
              if (DRCUTIL.getEuclideanDistance(rect, env_rect) < real_prl_spacing) {
                PlanarRect violation_rect = DRCUTIL.getSpacingRect(rect, env_rect);
                net_required_violation_rect_map[{net_idx, env_shape_data.net_idx}][real_prl_spacing].push_back(violation_rect);
              }
            }
            if (has_spacing_list) {
              int32_t real_spacing = parallel_run_length_spacing_rule.getSpacingWithWidth(check_width);
              if (DRCUTIL.getEuclideanDistance(rect, env_rect) < real_spacing) {
                PlanarRect violation_rect = DRCUTIL.getSpacingRect(rect, env_rect);
                net_required_violation_rect_map[{net_idx, env_shape_data.net_idx}][real_spacing].push_back(violation_rect);
              }
            }
          }
        }
      }
    }

    std::map<std::set<int32_t>, std::map<int32_t, std::vector<PlanarRect>>> exclude_env;
    for (auto& [violation_net_set, required_violation_rect_map] : net_required_violation_rect_map) {
      for (auto& [required_size, violation_rect_list] : required_violation_rect_map) {
        for (PlanarRect& violation_rect : violation_rect_list) {
          auto& env_violations = env_net_required_violation_rect_map[violation_net_set][required_size];
          bool is_env_inside = false;
          for (auto& env_violation : env_violations) {
            bool closed_inside = (violation_rect.getXSpan() == env_violation.getXSpan()) || (violation_rect.getYSpan() == env_violation.getYSpan());
            if (DRCUTIL.isInside(env_violation, violation_rect) && closed_inside) {
              is_env_inside = true;
              break;
            }
          }
          if (is_env_inside) {
            continue;
          }
          exclude_env[violation_net_set][required_size].push_back(violation_rect);
        }
      }
    }

    for (auto& [violation_net_set, required_violation_rect_map] : exclude_env) {
      for (auto& [required_size, violation_rect_list] : required_violation_rect_map) {
        for (PlanarRect& violation_rect : violation_rect_list) {
          bool is_inside = false;
          for (PlanarRect& other_violation_rect : violation_rect_list) {
            if (other_violation_rect == violation_rect) {
              continue;
            }
            if (DRCUTIL.isInside(other_violation_rect, violation_rect)) {
              is_inside = true;
              break;
            }
          }
          if (!is_inside) {
            Violation violation;
            violation.set_violation_type(ViolationType::kParallelRunLengthSpacing);
            violation.set_is_routing(true);
            violation.set_violation_net_set(violation_net_set);
            violation.set_layer_idx(routing_layer_idx);
            violation.set_rect(violation_rect);
            violation.set_required_size(required_size);
            rv_cluster.get_violation_list().push_back(violation);
          }
        }
      }
    }
  }
}

}  // namespace idrc

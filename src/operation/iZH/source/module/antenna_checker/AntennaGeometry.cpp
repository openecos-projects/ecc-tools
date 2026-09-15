// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************

#include "AntennaGeometry.hpp"

#include "ACNormRect.hpp"
#include "ACSegmentTree.hpp"
#include "ACSweepEvent.hpp"
#include "AntennaRuleEvaluator.hpp"
#include "Utility.hpp"

namespace izh {

void AntennaGeometry::unionArea(const std::vector<idb::IdbRect>& rects, int micron_dbu, double& area_um)
{
  double unused_perimeter_um = 0.0;
  unionAreaPerimeter(rects, micron_dbu, area_um, unused_perimeter_um);
}

void AntennaGeometry::unionAreaPerimeter(const std::vector<idb::IdbRect>& rects, int micron_dbu, double& area_um, double& perimeter_um)
{
  area_um = 0.0;
  perimeter_um = 0.0;

  if (rects.empty() || micron_dbu <= 0) {
    return;
  }

  std::vector<ACNormRect> rs;
  std::vector<int64_t> ys;

  rs.reserve(rects.size());
  ys.reserve(rects.size() * 2);

  for (const auto& r : rects) {
    int64_t x1 = std::min(r.get_low_x(), r.get_high_x());
    int64_t x2 = std::max(r.get_low_x(), r.get_high_x());
    int64_t y1 = std::min(r.get_low_y(), r.get_high_y());
    int64_t y2 = std::max(r.get_low_y(), r.get_high_y());

    if (x2 <= x1 || y2 <= y1) {
      continue;
    }

    rs.push_back({x1, x2, y1, y2});
    ys.push_back(y1);
    ys.push_back(y2);
  }

  if (rs.empty()) {
    return;
  }

  const long double dbu = static_cast<long double>(micron_dbu);

  auto finalize = [&](long double area_dbu, long double perimeter_dbu) {
    if (area_dbu < 0.0L) {
      area_dbu = 0.0L;
    }
    if (perimeter_dbu < 0.0L) {
      perimeter_dbu = 0.0L;
    }
    area_um = static_cast<double>(area_dbu / (dbu * dbu));
    perimeter_um = static_cast<double>(perimeter_dbu / dbu);
  };

  if (rs.size() == 1) {
    long double w = static_cast<long double>(rs[0].x2 - rs[0].x1);
    long double h = static_cast<long double>(rs[0].y2 - rs[0].y1);
    finalize(w * h, 2.0L * (w + h));
    return;
  }

  if (rs.size() <= 64) {
    bool need_union = false;

    for (size_t i = 0; i < rs.size() && !need_union; ++i) {
      for (size_t j = i + 1; j < rs.size(); ++j) {
        if (!(rs[i].x2 < rs[j].x1 || rs[j].x2 < rs[i].x1 || rs[i].y2 < rs[j].y1 || rs[j].y2 < rs[i].y1)) {
          need_union = true;
          break;
        }
      }
    }

    if (!need_union) {
      long double area = 0.0L;
      long double per = 0.0L;

      for (const auto& r : rs) {
        long double w = static_cast<long double>(r.x2 - r.x1);
        long double h = static_cast<long double>(r.y2 - r.y1);
        area += w * h;
        per += 2.0L * (w + h);
      }

      finalize(area, per);
      return;
    }
  }

  std::sort(ys.begin(), ys.end());
  ys.erase(std::unique(ys.begin(), ys.end()), ys.end());

  const int y_interval_count = static_cast<int>(ys.size()) - 1;
  if (y_interval_count <= 0) {
    return;
  }

  std::vector<ACSweepEvent> events;
  events.reserve(rs.size() * 2);

  auto yIndex = [&](int64_t v) -> int {
    return static_cast<int>(std::lower_bound(ys.begin(), ys.end(), v) - ys.begin());
  };

  for (const auto& r : rs) {
    int y1 = yIndex(r.y1);
    int y2 = yIndex(r.y2) - 1;

    if (y1 > y2) {
      continue;
    }

    events.push_back({r.x1, y1, y2, +1});
    events.push_back({r.x2, y1, y2, -1});
  }

  if (events.empty()) {
    return;
  }

  std::sort(events.begin(), events.end(), [](const ACSweepEvent& a, const ACSweepEvent& b) {
    return a.x < b.x;
  });

  ACSegmentTree tree;
  tree.init(ys);

  long double area = 0.0L;
  long double perimeter = 0.0L;

  bool has_prev_x = false;
  int64_t prev_x = 0;

  std::vector<std::pair<int, int>> left_ranges;
  std::vector<std::pair<int, int>> right_ranges;

  auto addVerticalContribution = [&](std::vector<std::pair<int, int>>& ranges) {
    if (ranges.empty()) {
      return;
    }

    std::sort(ranges.begin(), ranges.end());

    size_t i = 0;
    while (i < ranges.size()) {
      int a = ranges[i].first;
      int b = ranges[i].second;

      size_t j = i + 1;
      while (j < ranges.size() && ranges[j].first <= b + 1) {
        b = std::max(b, ranges[j].second);
        ++j;
      }

      long double range_len = static_cast<long double>(ys[b + 1] - ys[a]);
      long double covered = tree.queryCovered(a, b);
      long double uncovered = range_len - covered;

      if (uncovered > 0.0L) {
        perimeter += uncovered;
      }

      i = j;
    }
  };

  size_t i = 0;
  while (i < events.size()) {
    int64_t x = events[i].x;

    if (has_prev_x) {
      long double dx = static_cast<long double>(x - prev_x);
      if (dx > 0.0L) {
        area += tree.coveredLength() * dx;
        perimeter += 2.0L * static_cast<long double>(tree.intervalCount()) * dx;
      }
    }

    size_t j = i;
    left_ranges.clear();
    right_ranges.clear();

    while (j < events.size() && events[j].x == x) {
      if (events[j].delta > 0) {
        left_ranges.emplace_back(events[j].y1, events[j].y2);
      } else {
        right_ranges.emplace_back(events[j].y1, events[j].y2);
      }
      ++j;
    }

    addVerticalContribution(left_ranges);

    for (size_t k = i; k < j; ++k) {
      tree.update(events[k].y1, events[k].y2, events[k].delta);
    }

    addVerticalContribution(right_ranges);

    has_prev_x = true;
    prev_x = x;
    i = j;
  }

  finalize(area, perimeter);
}

void AntennaGeometry::evaluateCut(const ACAntennaRule& cut_rule, const ACUFNode& root_data, double cut_area_um, bool diff_active,
                                  int micron_dbu, bool has_bbox, int64_t bbox_lx, int64_t bbox_ly, int64_t bbox_hx, int64_t bbox_hy,
                                  const std::string& net_name, const std::string& pin_name, const std::string& inst_name, int layer_order,
                                  double gate_area, double diff_area, double metal_area, std::vector<ACViolation>& out_violations,
                                  double& cum_cut_num)
{
  if (Utility::equalDoubleByError(cut_area_um, 0.0, ZH_ERROR)) {
    return;
  }

  const double gate_plus_diff_factor = (cut_rule.gate_plus_diff >= 0.0) ? cut_rule.gate_plus_diff : 0.0;
  const double eff_gate = root_data.gate_area + gate_plus_diff_factor * root_data.diff_area;
  if (Utility::equalDoubleByError(eff_gate, 0.0, ZH_ERROR)) {
    return;
  }

  auto factor = [&](double f, bool diffuse_only) {
    if (f >= 0.0 && (!diffuse_only || diff_active)) {
      return f;
    }
    return 1.0;
  };

  const double cut_scale = factor(cut_rule.area_factor, cut_rule.area_factor_diffuse_only);
  const double reduce_factor = Utility::getPWLValue(cut_rule.area_diff_reduce_pwl, root_data.diff_area, 1.0);
  const double minus_diff = (cut_rule.area_minus_diff >= 0.0) ? cut_rule.area_minus_diff * root_data.diff_area : 0.0;

  const double par_cut_num = (cut_scale * cut_area_um) * reduce_factor - minus_diff;
  const double par_cut_check = std::max(0.0, Utility::getRatio(par_cut_num, eff_gate));

  const double prev_cut_num = cut_rule.cum_routing_plus_cut ? root_data.cum_area_num : root_data.cum_cut_num;
  cum_cut_num = std::max(0.0, prev_cut_num + par_cut_num);
  const double cut_car = Utility::getRatio(cum_cut_num, eff_gate);

  auto emit = [&](ACViolationType type, double ratio, double threshold) {
    if (ratio > threshold) {
      ACViolation v;
      v.net_name = net_name;
      v.layer_name = cut_rule.layer_name;
      v.type = type;
      v.ratio = ratio;
      v.threshold = threshold;
      v.pin_name = pin_name;
      v.inst_name = inst_name;
      v.layer_order = layer_order;
      v.gate_area = gate_area;
      v.diff_area = diff_area;
      v.metal_area = metal_area;
      v.cut_area = cut_area_um;
      if (has_bbox) {
        v.lx = Utility::getRatio(bbox_lx, micron_dbu);
        v.ly = Utility::getRatio(bbox_ly, micron_dbu);
        v.hx = Utility::getRatio(bbox_hx, micron_dbu);
        v.hy = Utility::getRatio(bbox_hy, micron_dbu);
      }
      out_violations.push_back(v);
    }
  };

  ACThresholdPick p = AntennaRuleEvaluator::pickThreshold(cut_rule.area_ratio, cut_rule.diff_area_ratio, cut_rule.diff_area_ratio_pwl,
                                                          root_data.diff_area, root_data.diff_connected);
  if (p.available) {
    emit(p.is_diff ? ACViolationType::kAntennaDiffCutPar : ACViolationType::kAntennaCutPar, par_cut_check, p.threshold);
  }

  p = AntennaRuleEvaluator::pickThreshold(cut_rule.cum_area_ratio, cut_rule.cum_diff_area_ratio, cut_rule.cum_diff_area_ratio_pwl,
                                          root_data.diff_area, root_data.diff_connected);
  if (p.available) {
    emit(p.is_diff ? ACViolationType::kAntennaDiffCutCar : ACViolationType::kAntennaCutCar, cut_car, p.threshold);
  }
}

void AntennaGeometry::evaluateRouting(const ACAntennaRule& routing_rule, const ACUFNode& root_data, double metal_area_um,
                                      double side_area_um, bool diff_active, int micron_dbu, bool has_bbox, int64_t bbox_lx,
                                      int64_t bbox_ly, int64_t bbox_hx, int64_t bbox_hy, const std::string& net_name,
                                      const std::string& pin_name, const std::string& inst_name, int layer_order, double gate_area,
                                      double diff_area, double cut_area, std::vector<ACViolation>& out_violations, double& cum_area_num,
                                      double& cum_side_num)
{
  if (Utility::equalDoubleByError(metal_area_um, 0.0, ZH_ERROR) && Utility::equalDoubleByError(side_area_um, 0.0, ZH_ERROR)) {
    return;
  }

  const double gate_plus_diff_factor = (routing_rule.gate_plus_diff >= 0.0) ? routing_rule.gate_plus_diff : 0.0;
  const double eff_gate = root_data.gate_area + gate_plus_diff_factor * root_data.diff_area;
  if (Utility::equalDoubleByError(eff_gate, 0.0, ZH_ERROR)) {
    return;
  }

  auto factor = [&](double f, bool diffuse_only) {
    if (f >= 0.0 && (!diffuse_only || diff_active)) {
      return f;
    }
    return 1.0;
  };

  const double area_scale = factor(routing_rule.area_factor, routing_rule.area_factor_diffuse_only);
  const double side_scale = factor(routing_rule.side_area_factor, routing_rule.side_area_factor_diffuse_only);
  const double area_reduce = Utility::getPWLValue(routing_rule.area_diff_reduce_pwl, root_data.diff_area, 1.0);
  const double minus_diff = (routing_rule.area_minus_diff >= 0.0) ? routing_rule.area_minus_diff * root_data.diff_area : 0.0;

  const double par_area_num = (area_scale * metal_area_um) * area_reduce - minus_diff;
  const double par_side_num = side_scale * side_area_um;
  const double par_area_check = std::max(0.0, Utility::getRatio(par_area_num, eff_gate));
  const double par_side_check = std::max(0.0, Utility::getRatio(par_side_num, eff_gate));

  const double prev_area_num = routing_rule.cum_routing_plus_cut ? root_data.cum_cut_num : root_data.cum_area_num;
  cum_area_num = std::max(0.0, prev_area_num + par_area_num);
  cum_side_num = std::max(0.0, root_data.cum_side_num + par_side_num);

  const double car = Utility::getRatio(cum_area_num, eff_gate);
  const double csr = Utility::getRatio(cum_side_num, eff_gate);

  auto emit = [&](ACViolationType type, double ratio, double threshold) {
    if (ratio > threshold) {
      ACViolation v;
      v.net_name = net_name;
      v.layer_name = routing_rule.layer_name;
      v.type = type;
      v.ratio = ratio;
      v.threshold = threshold;
      v.pin_name = pin_name;
      v.inst_name = inst_name;
      v.layer_order = layer_order;
      v.gate_area = gate_area;
      v.diff_area = diff_area;
      v.metal_area = metal_area_um;
      v.cut_area = cut_area;
      if (has_bbox) {
        v.lx = Utility::getRatio(bbox_lx, micron_dbu);
        v.ly = Utility::getRatio(bbox_ly, micron_dbu);
        v.hx = Utility::getRatio(bbox_hx, micron_dbu);
        v.hy = Utility::getRatio(bbox_hy, micron_dbu);
      }
      out_violations.push_back(v);
    }
  };

  ACThresholdPick p = AntennaRuleEvaluator::pickThreshold(routing_rule.area_ratio, routing_rule.diff_area_ratio,
                                                          routing_rule.diff_area_ratio_pwl, root_data.diff_area, root_data.diff_connected);
  if (p.available) {
    emit(p.is_diff ? ACViolationType::kAntennaDiffPar : ACViolationType::kAntennaPar, par_area_check, p.threshold);
  }

  p = AntennaRuleEvaluator::pickThreshold(routing_rule.cum_area_ratio, routing_rule.cum_diff_area_ratio, routing_rule.cum_diff_area_ratio_pwl,
                                          root_data.diff_area, root_data.diff_connected);
  if (p.available) {
    emit(p.is_diff ? ACViolationType::kAntennaDiffCar : ACViolationType::kAntennaCar, car, p.threshold);
  }

  p = AntennaRuleEvaluator::pickThreshold(routing_rule.side_area_ratio, routing_rule.diff_side_area_ratio,
                                          routing_rule.diff_side_area_ratio_pwl, root_data.diff_area, root_data.diff_connected);
  if (p.available) {
    emit(p.is_diff ? ACViolationType::kAntennaDiffPsr : ACViolationType::kAntennaPsr, par_side_check, p.threshold);
  }

  p = AntennaRuleEvaluator::pickThreshold(routing_rule.cum_side_area_ratio, routing_rule.cum_diff_side_area_ratio,
                                          routing_rule.cum_diff_side_area_ratio_pwl, root_data.diff_area, root_data.diff_connected);
  if (p.available) {
    emit(p.is_diff ? ACViolationType::kAntennaDiffCsr : ACViolationType::kAntennaCsr, csr, p.threshold);
  }
}

}  // namespace izh

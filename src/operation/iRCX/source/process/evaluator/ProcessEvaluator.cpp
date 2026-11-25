#include "ProcessEvaluator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ircx {
namespace {
bool positive(double value)
{
  return std::isfinite(value) && value > 0;
}
double spacing(double value)
{
  return value < 0 ? std::numeric_limits<double>::infinity() : value;
}
bool applies(ProcessEffectType table, ProcessEffectType query) { return table == ProcessEffectType::kBoth || table == query; }
ProcessResult<double> temperatureAdjusted(double base, double temperature, double nominal, double first, double second, const std::string& description)
{
  const double delta = temperature - nominal;
  const double factor = 1 + first * delta + second * delta * delta;
  if (!positive(factor))
    return std::string("invalid resistance temperature factor");
  const double resistance = base * factor;
  if (!positive(resistance))
    return "invalid " + description;
  return resistance;
}
}
namespace {
ProcessResult<EffectiveWireGeometry> initialGeometry(const ProcessConductor& conductor, const WireGeometryQuery& query, ProcessEffectType domain)
{
  if (domain != ProcessEffectType::kResistance && domain != ProcessEffectType::kCapacitance)
    return std::string("geometry query requires one effect domain");
  const auto& name = conductor.get_layer_name();
  if (const auto* issue = findEvaluationIssue(conductor.get_evaluation_issues(), domain))
    return "cannot evaluate conductor " + name + ": " + issue->field + ": " + issue->message;
  if (!positive(query.width))
    return "invalid drawn width on " + name;
  if (!positive(conductor.get_thickness()))
    return "invalid thickness on " + name;
  if (!std::isfinite(query.lower_spacing) || !std::isfinite(query.upper_spacing))
    return std::string("nonfinite wire spacing");
  double fractional_change = 0;
  for (const auto& table : conductor.get_thickness_change_table_list()) {
    if (!applies(table.get_effect_type(), domain)) continue;
    const auto change = table.get_table().query(query.width, std::min(spacing(query.lower_spacing), spacing(query.upper_spacing)));
    if (!change || !std::isfinite(*change))
      return "invalid thickness variation table on " + name;
    fractional_change += *change;
  }
  const double thickness = conductor.get_thickness() * (1 + fractional_change);
  if (!positive(thickness))
    return "invalid effective thickness on " + name;
  return EffectiveWireGeometry{query.width, thickness};
}
ProcessResult<EffectiveWireGeometry> etchStep(const ProcessEtchTable& table, EffectiveWireGeometry geometry, double lower, double upper)
{
  const auto lower_etch = table.get_table().query(geometry.width, spacing(lower));
  const auto upper_etch = table.get_table().query(geometry.width, spacing(upper));
  if (!lower_etch || !upper_etch || !std::isfinite(*lower_etch) || !std::isfinite(*upper_etch))
    return std::string("invalid etch table");
  geometry.width -= *lower_etch + *upper_etch;
  geometry.center_shift += (*lower_etch - *upper_etch) / 2;
  if (!positive(geometry.width) || !std::isfinite(geometry.center_shift))
    return std::string("invalid geometry after etch table");
  return geometry;
}
ProcessResult<EffectiveWireGeometry> scalarEtch(const ProcessConductor& conductor, EffectiveWireGeometry geometry, ProcessEffectType domain)
{
  const double scalar = conductor.get_etch() + (domain == ProcessEffectType::kResistance
      ? conductor.get_resistive_only_etch() : conductor.get_capacitive_only_etch());
  geometry.width -= 2 * scalar;
  if (!positive(geometry.width))
    return "invalid width after scalar etch on " + conductor.get_layer_name();
  return geometry;
}
}
ProcessResult<EffectiveWireGeometry> evaluateWireGeometry(const ProcessConductor& conductor, const WireGeometryQuery& query, ProcessEffectType domain)
{
  auto result = initialGeometry(conductor, query, domain);
  if (!result) return result.error;
  double lower = query.lower_spacing, upper = query.upper_spacing;
  for (const auto& table : conductor.get_etch_table_list()) {
    if (!applies(table.get_effect_type(), domain)) continue;
    const auto previous = *result;
    result = etchStep(table, previous, lower, upper);
    if (!result) return result.error + " on " + conductor.get_layer_name();
    const double shrink = (previous.width - result->width) / 2;
    const double shift = result->center_shift - previous.center_shift;
    if (lower >= 0) lower += shrink + shift;
    if (upper >= 0) upper += shrink - shift;
    if ((query.lower_spacing >= 0 && lower < -1e-12) || (query.upper_spacing >= 0 && upper < -1e-12))
      return "overlapping wires after etch on " + conductor.get_layer_name();
  }
  const auto final = scalarEtch(conductor, *result, domain);
  if (!final) return final.error;
  const double shrink = (result->width - final->width) / 2;
  if ((query.lower_spacing >= 0 && lower + shrink < -1e-12) || (query.upper_spacing >= 0 && upper + shrink < -1e-12))
    return "overlapping wires after scalar etch on " + conductor.get_layer_name();
  return final;
}
namespace {
const EffectiveWireSegment* segmentAt(const std::vector<EffectiveWireSegment>& segments, double coordinate)
{
  const auto it = std::lower_bound(segments.begin(), segments.end(), coordinate,
      [](const EffectiveWireSegment& segment, double point) { return segment.end <= point; });
  return it != segments.end() && it->start <= coordinate ? &*it : nullptr;
}
ProcessResult<WireGeometryQuery> profileQuery(std::span<const WireGeometryInput> wires, const WireGeometryProfiles& profiles,
    std::size_t wire_index, const EffectiveWireSegment& segment, bool drawn)
{
  const auto& wire = wires[wire_index];
  const auto& span = wire.spans[segment.source_span];
  WireGeometryQuery query{segment.geometry.width};
  for (bool lower : {true, false}) {
    const auto neighbor_index = lower ? span.lower_neighbor : span.upper_neighbor;
    if (neighbor_index == kNoWireNeighbor) continue;
    const auto& neighbor = wires[neighbor_index];
    const EffectiveWireSegment* adjacent = nullptr;
    if (!drawn) {
      adjacent = segmentAt(profiles[neighbor_index], (segment.start + segment.end) / 2);
      if (!adjacent || adjacent->start > segment.start + 1e-12 || adjacent->end < segment.end - 1e-12)
        return std::string("neighbor geometry does not cover wire span");
    }
    const double center = wire.center + segment.geometry.center_shift;
    const double neighbor_center = neighbor.center + (adjacent ? adjacent->geometry.center_shift : 0);
    const double neighbor_width = adjacent ? adjacent->geometry.width : neighbor.width;
    const double gap = (lower ? center - neighbor_center : neighbor_center - center) - (segment.geometry.width + neighbor_width) / 2;
    const double drawn_gap = (lower ? wire.center - neighbor.center : neighbor.center - wire.center) - (wire.width + neighbor.width) / 2;
    if (!std::isfinite(gap) || (drawn_gap > 1e-12 && gap < -1e-12))
      return std::string("etch creates overlapping or nonfinite adjacent wire geometry");
    // Existing overlapping rectangles have a contact boundary, not an absent
    // neighbor. This lookup convention does not merge their electrical topology.
    (lower ? query.lower_spacing : query.upper_spacing) = std::max(0.0, gap);
  }
  return query;
}
}
ProcessResult<WireGeometryProfiles> evaluateWireGeometryProfiles(std::span<const WireGeometryInput> wires, ProcessEffectType domain)
{
  if (domain != ProcessEffectType::kResistance && domain != ProcessEffectType::kCapacitance)
    return std::string("geometry query requires one effect domain");
  std::size_t stage_count = 0;
  for (std::size_t i = 0; i < wires.size(); ++i) {
    const auto& wire = wires[i];
    if (!wire.conductor || !positive(wire.width) || !std::isfinite(wire.center) || wire.spans.empty())
      return "invalid wire geometry input " + std::to_string(i);
    stage_count = std::max(stage_count, wire.conductor->get_etch_table_list().size());
    double previous_end = -std::numeric_limits<double>::infinity();
    for (const auto& span : wire.spans) {
      if (!std::isfinite(span.start) || !std::isfinite(span.end) || span.end <= span.start || span.start < previous_end)
        return "invalid or unsorted wire span on " + wire.conductor->get_layer_name();
      previous_end = span.end;
      for (auto neighbor : {span.lower_neighbor, span.upper_neighbor}) {
        if (neighbor == kNoWireNeighbor) continue;
        if (neighbor >= wires.size() || neighbor == i || wires[neighbor].conductor != wire.conductor)
          return std::string("invalid neighbor index or mismatched process layer");
      }
    }
  }
  WireGeometryProfiles profiles(wires.size());
  for (std::size_t i = 0; i < wires.size(); ++i) {
    const auto& wire = wires[i];
    for (std::size_t j = 0; j < wire.spans.size(); ++j) {
      const auto& span = wire.spans[j];
      EffectiveWireSegment segment{j, span.start, span.end, {wire.width, wire.conductor->get_thickness()}};
      const auto query = profileQuery(wires, profiles, i, segment, true);
      if (!query) return query.error + " on wire " + std::to_string(i);
      const auto geometry = initialGeometry(*wire.conductor, *query, domain);
      if (!geometry) return geometry.error;
      segment.geometry = *geometry;
      profiles[i].push_back(segment);
    }
  }
  // The final pass records gaps from the completed scalar-etch snapshot.
  for (std::size_t stage = 0; stage <= stage_count + 1; ++stage) {
    WireGeometryProfiles next(wires.size());
    for (std::size_t i = 0; i < wires.size(); ++i) {
      const auto& wire = wires[i];
      const auto& tables = wire.conductor->get_etch_table_list();
      for (const auto& segment : profiles[i]) {
        const auto& span = wire.spans[segment.source_span];
        std::vector<double> boundaries{segment.start, segment.end};
        for (auto neighbor : {span.lower_neighbor, span.upper_neighbor}) {
          if (neighbor == kNoWireNeighbor) continue;
          const auto& adjacent = profiles[neighbor];
          auto it = std::lower_bound(adjacent.begin(), adjacent.end(), segment.start,
              [](const EffectiveWireSegment& item, double point) { return item.end <= point; });
          for (; it != adjacent.end() && it->start < segment.end; ++it) {
            if (it->start > segment.start) boundaries.push_back(it->start);
            if (it->end < segment.end) boundaries.push_back(it->end);
          }
        }
        std::sort(boundaries.begin(), boundaries.end());
        boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
        for (std::size_t j = 1; j < boundaries.size(); ++j) {
          auto piece = segment;
          piece.start = boundaries[j - 1];
          piece.end = boundaries[j];
          const auto query = profileQuery(wires, profiles, i, piece, false);
          if (!query) return query.error + " on wire " + std::to_string(i);
          if (stage < tables.size() && applies(tables[stage].get_effect_type(), domain)) {
            const auto geometry = etchStep(tables[stage], piece.geometry, query->lower_spacing, query->upper_spacing);
            if (!geometry) return geometry.error + " on " + wire.conductor->get_layer_name();
            piece.geometry = *geometry;
          } else if (stage == stage_count) {
            const auto geometry = scalarEtch(*wire.conductor, piece.geometry, domain);
            if (!geometry) return geometry.error;
            piece.geometry = *geometry;
          }
          piece.lower_spacing = query->lower_spacing;
          piece.upper_spacing = query->upper_spacing;
          next[i].push_back(piece);
        }
      }
    }
    profiles = std::move(next);
  }
  return profiles;
}
ProcessResult<double> evaluateWireResistance(const ProcessConductor& conductor, const WireGeometryQuery& query, double length, double temperature,
                                             double nominal_temperature)
{
  const auto geometry = evaluateWireGeometry(conductor, query, ProcessEffectType::kResistance);
  if (!geometry)
    return geometry.error;
  return evaluateWireResistanceFromGeometry(conductor, query, *geometry, length, temperature, nominal_temperature);
}
ProcessResult<double> evaluateWireResistanceFromGeometry(const ProcessConductor& conductor, const WireGeometryQuery& query,
    const EffectiveWireGeometry& geometry, double length, double temperature, double nominal_temperature)
{
  if (const auto* issue = findEvaluationIssue(conductor.get_evaluation_issues(), ProcessEffectType::kResistance))
    return "cannot evaluate conductor " + conductor.get_layer_name() + ": " + issue->field + ": " + issue->message;
  if (!positive(geometry.width) || !positive(geometry.thickness))
    return std::string("invalid finalized wire geometry");
  if (!std::isfinite(query.lower_spacing) || !std::isfinite(query.upper_spacing))
    return std::string("nonfinite wire spacing");
  const auto& name = conductor.get_layer_name();
  if (!positive(length))
    return "invalid wire length on " + name;
  const double width = geometry.width, thickness = geometry.thickness;
  const double space = std::min(spacing(query.lower_spacing), spacing(query.upper_spacing));
  double rho = 0, sheet = 0;
  std::optional<double> table;
  bool rho_table = false;
  switch (conductor.get_resistance_model()) {
    case WireResistanceModel::kResistivity: rho = conductor.get_resistivity(); break;
    case WireResistanceModel::kSheet: sheet = conductor.get_sheet_resistance(); break;
    case WireResistanceModel::kSheetByWidth:
      table = conductor.get_sheet_resistance_by_width_table().query(width);
      break;
    case WireResistanceModel::kSheetByWidthSpacing:
      table = conductor.get_sheet_resistance_by_width_spacing_table().query(width, space);
      break;
    case WireResistanceModel::kResistivityByWidthSpacing:
      table = conductor.get_resistivity_by_width_spacing_table().query(width, space);
      rho_table = true;
      break;
    case WireResistanceModel::kResistivityByWidthThickness:
      table = conductor.get_resistivity_by_width_thickness_table().query(thickness, width);
      rho_table = true;
      break;
    case WireResistanceModel::kNone:
      return "missing resistance model on " + name;
  }
  if (conductor.get_resistance_model() != WireResistanceModel::kResistivity && conductor.get_resistance_model() != WireResistanceModel::kSheet) {
    if (!table)
      return "missing or out-of-range resistance table on " + name;
    if (!positive(*table))
      return "invalid resistance value on " + name;
    (rho_table ? rho : sheet) = *table;
  }
  const double base = rho > 0 ? rho * length / (width * thickness)
      : sheet * length / width * conductor.get_thickness() / thickness;
  double first = 0, second = 0;
  conductor.query_tmpr_coefficient(width, first, second);
  const double nominal = conductor.get_has_nominal_tmpr() ? conductor.get_nominal_tmpr() : nominal_temperature;
  return temperatureAdjusted(base, temperature, nominal, first, second, "wire resistance on " + name);
}
ProcessResult<double> evaluateViaResistance(const ProcessVia& via, double width, double length, double temperature, double nominal_temperature)
{
  const auto& name = via.get_layer_name();
  if (const auto* issue = findEvaluationIssue(via.get_evaluation_issues(), ProcessEffectType::kResistance))
    return "cannot evaluate via " + name + ": " + issue->field + ": " + issue->message;
  if (!positive(width))
    return "invalid via width on " + name;
  if (!positive(length))
    return "invalid via length on " + name;
  const auto etch = via.query_etch(ProcessEffectType::kResistance, width, length);
  if (!etch)
    return etch.error;
  width -= 2 * etch->get_width();
  length -= 2 * etch->get_length();
  if (!positive(width))
    return "invalid via width after etch on " + name;
  if (!positive(length))
    return "invalid via length after etch on " + name;
  const double area = width * length;
  if (!positive(area))
    return "invalid via area on " + name;
  auto base = via.query_resistance(area);
  if (!base)
    return base.error;
  if (!positive(*base))
    return "invalid resistance value on " + name;
  double first = 0, second = 0;
  via.query_tmpr_coefficient(area, first, second);
  const double nominal = via.get_has_nominal_tmpr() ? via.get_nominal_tmpr() : nominal_temperature;
  return temperatureAdjusted(*base, temperature, nominal, first, second, "via resistance on " + name);
}
}  // namespace ircx

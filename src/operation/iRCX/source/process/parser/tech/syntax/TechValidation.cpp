#include "TechDriver.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <set>

namespace ircx::tech::syntax::detail {
namespace {
using K = Keyword;
using NK = NodeKind;

bool contains(std::initializer_list<K> keys, K key) {
  return std::find(keys.begin(), keys.end(), key) != keys.end();
}
Failure fail(const Node& node, const std::string& message)
{
  return {node.range, message};
}
std::string label(K key) {
  const auto* info = keyword_info(key);
  return info ? std::string(info->spelling) : "document";
}
bool is_flag(const Node& node) {
  const auto* info = keyword_info(node.keyword);
  return node.kind == NK::keyword && info && info->syntax == Syntax::flag;
}
Result<std::vector<const Node*>> numeric_vector(const Node& node)
{
  std::vector<const Node*> values;
  for (std::size_t i = 0; i < node.children.size(); ++i) {
    const auto& value = node.children[i];
    if (value.kind == NK::separator) {
      if (i == 0 || i + 1 == node.children.size() ||
          node.children[i - 1].kind != NK::number || node.children[i + 1].kind != NK::number)
        return fail(value, "comma must separate two numbers");
    } else if (value.kind == NK::number) values.push_back(&value);
    else
      return fail(value, "expected a number in " + label(node.keyword));
  }
  if (values.empty())
    return fail(node, "numeric list must not be empty");
  return values;
}
Status increasing(const std::vector<const Node*>& values, const Node& owner)
{
  if (values.empty())
    return fail(owner, "numeric list must not be empty");
  // Compare literal magnitudes only when suffixes agree; consumer resolves units.
  if (!std::ranges::all_of(values, [&](const auto* n) { return n->quantity->suffix == values.front()->quantity->suffix; }))
    return {};
  for (std::size_t i = 1; i < values.size(); ++i)
    if (!(values[i - 1]->quantity->value < values[i]->quantity->value))
      return fail(owner, "axis values must be strictly increasing");
  return {};
}
Status axis(const Node& node)
{
  auto values = numeric_vector(node);
  if (!values)
    return values.error;
  return increasing(*values, node);
}
Status one_number(const Node& node, bool allow_braces = false)
{
  if (node.braced && !allow_braces)
    return fail(node, "expected an unbraced scalar for " + label(node.keyword));
  if (node.children.size() != 1 || node.children.front().kind != NK::number)
    return fail(node, "expected exactly one number for " + label(node.keyword));
  return {};
}
std::vector<const Node*> data(const Node& node)
{
  std::vector<const Node*> result;
  for (const auto& child : node.children) if (!is_flag(child)) result.push_back(&child);
  return result;
}
Status rows(const Node& node, std::size_t columns, bool packed = false, bool sorted = false)
{
  const auto content = data(node);
  if (content.empty())
    return fail(node, "table must not be empty");
  std::vector<const Node*> first_column;
  if (packed && content.front()->kind == NK::number) {
    auto values = numeric_vector(node);
    if (!values)
      return values.error;
    if (values->size() % columns != 0)
      return fail(node, "incomplete numeric row");
    for (std::size_t i = 0; i < values->size(); i += columns)
      first_column.push_back((*values)[i]);
  } else {
    for (const auto* row : content) {
      if (row->kind != NK::list)
        return fail(*row, "expected a braced numeric row");
      auto values = numeric_vector(*row);
      if (!values)
        return values.error;
      if ((!packed && values->size() != columns) || (packed && values->size() % columns != 0))
        return fail(*row, "expected " + std::to_string(columns) + " values per row");
      for (std::size_t i = 0; i < values->size(); i += columns)
        first_column.push_back((*values)[i]);
    }
  }
  if (sorted)
    return increasing(first_column, node);
  return {};
}
Status matrix(const Node& node, std::size_t row_count, std::size_t column_count, bool flat = false)
{
  if (flat && !node.children.empty() && node.children.front().kind == NK::number) {
    auto values = numeric_vector(node);
    if (!values)
      return values.error;
    if (values->size() != row_count * column_count)
      return fail(node, "one-dimensional table size does not match its axis");
    return {};
  }
  if (node.children.size() != row_count)
    return fail(node, "matrix row count must be " + std::to_string(row_count));
  for (const auto& row : node.children) {
    if (row.kind != NK::list)
      return fail(row, "matrix column count must be " + std::to_string(column_count));
    auto values = numeric_vector(row);
    if (!values)
      return values.error;
    if (values->size() != column_count)
      return fail(row, "matrix column count must be " + std::to_string(column_count));
  }
  return {};
}
Result<const Node*> unique(const Node& node, K key, bool required = false)
{
  const Node* found = nullptr;
  for (const auto& child : node.children) if (child.keyword == key) {
      if (found)
        return fail(child, "duplicate table field " + label(key));
      found = &child;
  }
  if (!found && required)
    return fail(node, "missing table field " + label(key));
  return found;
}
Result<std::size_t> count(const Node* node)
{
  if (!node)
    return std::size_t{1};
  auto values = numeric_vector(*node);
  if (!values)
    return values.error;
  return values->size();
}
Result<std::size_t> required_count(const Node& owner, K key)
{
  auto node = unique(owner, key, true);
  if (!node)
    return node.error;
  return count(*node);
}
Status axis_matrix(const Node& owner, const Node* rows, const Node* columns, K values_key)
{
  auto values = unique(owner, values_key, true);
  if (!values)
    return values.error;
  auto row_count = count(rows);
  if (!row_count)
    return row_count.error;
  auto column_count = count(columns);
  if (!column_count)
    return column_count.error;
  return matrix(**values, *row_count, *column_count, !rows || !columns);
}
class Validator {
public:
  explicit Validator(const ParseOptions& options) : options_(options) {}
  Status check(const Node& node, K parent = K::unknown) const;

 private:
  const ParseOptions& options_;
  Status fields(const Node& node, std::initializer_list<K> allowed, std::size_t skip = 0) const
  {
    for (std::size_t i = skip; i < node.children.size(); ++i) {
      const auto& child = node.children[i];
      if (child.kind != NK::keyword)
        return fail(child, "expected a keyword in " + label(node.keyword));
      if (!contains(allowed, child.keyword))
        return fail(child, label(child.keyword) + " is not allowed in " + label(node.keyword));
      {
        auto checked = check(child, node.keyword);
        if (!checked)
          return checked.error;
      }
    }
    return {};
  }
  Status modifiers(const Node& node, std::initializer_list<K> allowed, bool inside = false, bool multiple = false) const
  {
    std::set<K> seen;
    for (const auto& child : node.children) if (is_flag(child)) {
      if (!contains(allowed, child.keyword) || child.qualifier == inside)
        return fail(child, "invalid option or option placement in " + label(node.keyword));
      if (!seen.insert(child.keyword).second || (!multiple && seen.size() > 1))
        return fail(child, "duplicate or conflicting options");
    }
    return {};
  }
  Status width_spacing_table(const Node& node) const
  {
    {
      auto checked = fields(node, {K::widths, K::spacings, K::values, K::resistive_only, K::capacitive_only, K::etch_from_top});
      if (!checked)
        return checked.error;
    }
    if (node.keyword == K::etch_vs_width_and_spacing) {
      auto checked = modifiers(node, {K::resistive_only, K::capacitive_only, K::etch_from_top});
      if (!checked)
        return checked.error;
    } else if (node.keyword == K::thickness_vs_width_and_spacing) {
      auto checked = modifiers(node, {K::resistive_only, K::capacitive_only});
      if (!checked)
        return checked.error;
    } else {
      auto checked = modifiers(node, {});
      if (!checked)
        return checked.error;
    }
    auto widths_result = unique(node, K::widths);
    if (!widths_result)
      return widths_result.error;
    const auto* widths = *widths_result;
    auto spacings_result = unique(node, K::spacings);
    if (!spacings_result)
      return spacings_result.error;
    const auto* spacings = *spacings_result;
    if (!widths && !spacings)
      return fail(node, "at least one of WIDTHS/SPACINGS is required");
    return axis_matrix(node, widths, spacings, K::values);
  }
};

Status Validator::check(const Node& node, K parent) const
{
  const auto* info = keyword_info(node.keyword);
  if (!info)
    return fail(node, "unknown keyword");
  if (info->feature == Feature::width_si_table && !options_.allow_width_si_table)
    return fail(node, label(node.keyword) + " requires the width SI table option");
  if (parent == K::unknown && !contains({K::units, K::dielectric, K::metal, K::via, K::viamodel,
      K::substrate, K::half_node_scale_factor, K::vdd_pad_rc, K::gnd_pad_rc,
      K::vdd_wire_bond_or_bump_rlc, K::gnd_wire_bond_or_bump_rlc,
      K::vdd_package_rlc, K::vss_package_rlc}, node.keyword))
    return fail(node, label(node.keyword) + " is not a top-level keyword");

  switch (node.keyword) {
    case K::units:
      return fields(node, {K::capacitance, K::inductance, K::resistance, K::length, K::current, K::voltage, K::power, K::time, K::frequency});
    case K::vdd_pad_rc: case K::gnd_pad_rc:
      return fields(node, {K::r, K::c});
    case K::vdd_wire_bond_or_bump_rlc: case K::gnd_wire_bond_or_bump_rlc:
    case K::vdd_package_rlc: case K::vss_package_rlc:
      return fields(node, {K::r, K::l, K::c});
    case K::dielectric:
      for (auto key : {K::constant, K::thickness, K::height, K::above, K::sw_t, K::tw_t}) {
        auto checked = unique(node, key);
        if (!checked)
          return checked.error;
      }
      return fields(node, {K::constant, K::thickness, K::height, K::above, K::sw_t, K::tw_t}, 1);
    case K::metal:
      return fields(node,
                    {K::minwidth,
                     K::minspace,
                     K::width_adjust,
                     K::width_space,
                     K::pitch,
                     K::thickness,
                     K::resistance,
                     K::rho,
                     K::cap_density,
                     K::em,
                     K::width_si_vs_width_and_spacing,
                     K::t_em,
                     K::tnom_em,
                     K::em_temp_rating,
                     K::em_adjust,
                     K::em_thickadjust,
                     K::width_based_em,
                     K::blech_jlc,
                     K::height,
                     K::above,
                     K::em_time_current,
                     K::polynomial_based_em_dc,
                     K::polynomial_based_em_rms,
                     K::polynomial_based_em_peak,
                     K::t,
                     K::tnom,
                     K::t0,
                     K::coeff_rt1,
                     K::coeff_rt2,
                     K::crt_vs_si_width,
                     K::thickness_vs_width_and_spacing,
                     K::thickness_vs_density,
                     K::density_box_weighting_factor,
                     K::bottom_thickness_vs_si_width,
                     K::polynomial_based_thickness_variation,
                     K::rho_vs_si_width_and_thickness,
                     K::rho_vs_width_and_spacing,
                     K::rpsq_vs_width_and_spacing,
                     K::rpsq_vs_si_width,
                     K::resistive_only_etch,
                     K::etch_vs_width_and_spacing,
                     K::capacitive_only_etch,
                     K::side_tangent,
                     K::etch,
                     K::fill_ratio,
                     K::fill_spacing,
                     K::fill_width,
                     K::fill_type,
                     K::drop_factor,
                     K::clad_rpsq,
                     K::clad_thickness},
                    1);
    case K::via:
      return fields(node,
                    {K::resistance,
                     K::upperlayer,
                     K::lowerlayer,
                     K::coeff_rt1,
                     K::coeff_rt2,
                     K::tnom,
                     K::t0,
                     K::t,
                     K::rpv_vs_area,
                     K::width,
                     K::area,
                     K::rpv_vs_num,
                     K::em,
                     K::crt_vs_area,
                     K::topology_based_em,
                     K::emv_vs_area,
                     K::em_temp_rating,
                     K::direction_based_em_temp_rating,
                     K::polynomial_based_em_dc,
                     K::polynomial_based_em_rms,
                     K::polynomial_based_em_peak,
                     K::tnom_em,
                     K::t_em},
                    1);
    case K::substrate: {
      auto type = std::string(node.name());
      std::ranges::transform(type, type.begin(), [](char c) { return c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c; });
      if (type != "BULK" && type != "EPI" && type != "PWELL" && type != "NWELL")
        return fail(node, "SUBSTRATE type must be BULK, EPI, PWELL or NWELL");
      return fields(node, {K::thermal_conductivity, K::thickness, K::resistivity, K::capacitance}, 1);
    }
    case K::viamodel:
      if (node.children.size() != 4)
        return fail(node, "VIAMODEL requires metal/via/metal rectangles");
      return {};
    case K::widths: case K::spacings: case K::length_ranges: case K::width_ranges:
    case K::via_number: case K::metal_width: case K::via_area:
      return axis(node);
    case K::width:
      if (parent == K::via) {
        auto checked = one_number(node, true);
        if (!checked)
          return checked.error;
      } else {
        if (!node.braced)
          return fail(node, "WIDTH axis must be braced");
        {
          auto checked = axis(node);
          if (!checked)
            return checked.error;
        }
      }
      return {};
    case K::thickness:
      if (parent == K::rho_vs_si_width_and_thickness) {
        if (!node.braced)
          return fail(node, "THICKNESS axis must be braced");
        {
          auto checked = axis(node);
          if (!checked)
            return checked.error;
        }
      } else
        return one_number(node);
      return {};
    case K::area:
      return one_number(node, true);  // Accept a scalar or a braced scalar.
    case K::em:
      if (parent == K::width_based_em) {
        if (!node.braced)
          return fail(node, "EM limits must be braced");
        {
          auto checked = numeric_vector(node);
          if (!checked)
            return checked.error;
        }
      } else if (parent == K::downflow || parent == K::upflow) {
        if (!node.braced)
          return fail(node, "topology EM matrix must be braced");
      } else
        return one_number(node);
      return {};
    case K::width_space:
    case K::rpsq_vs_si_width:
    case K::density_box_weighting_factor: {
      auto checked = modifiers(node, {});
      if (!checked)
        return checked.error;
    }
      return rows(node, 2);
    case K::em_time_current: {
      auto checked = modifiers(node, {});
      if (!checked)
        return checked.error;
    }
      return rows(node, 2, false, true);
    case K::rpv_vs_area:
    case K::emv_vs_area: {
      auto checked = modifiers(node, {});
      if (!checked)
        return checked.error;
    }
      return rows(node, 2, true, true);
    case K::rpv_vs_num: {
      auto checked = modifiers(node, {});
      if (!checked)
        return checked.error;
    }
      return rows(node, 2);
    case K::crt_vs_si_width:
    case K::crt_vs_area: {
      auto checked = modifiers(node, {});
      if (!checked)
        return checked.error;
    }
      return rows(node, 3);
    case K::thickness_vs_density: {
      auto checked = modifiers(node, {K::resistive_only, K::capacitive_only});
      if (!checked)
        return checked.error;
    }
      return rows(node, 2);
    case K::bottom_thickness_vs_si_width: {
      auto checked = modifiers(node, {K::resistive_only, K::capacitive_only}, true, true);
      if (!checked)
        return checked.error;
    }
      return rows(node, 2);
    case K::em_temp_rating: case K::direction_based_em_temp_rating:
      if (node.find(K::upflow) || node.find(K::downflow) || node.keyword == K::direction_based_em_temp_rating) {
        {
          auto checked = fields(node, {K::downflow, K::upflow});
          if (!checked)
            return checked.error;
        }
        if (node.children.empty())
          return fail(node, "directional rating must not be empty");
        {
          auto checked = unique(node, K::downflow);
          if (!checked)
            return checked.error;
        }
        {
          auto checked = unique(node, K::upflow);
          if (!checked)
            return checked.error;
        }
      } else {
        {
          auto checked = modifiers(node, {});
          if (!checked)
            return checked.error;
        }
        {
          auto checked = rows(node, 2);
          if (!checked)
            return checked.error;
        }
      }
      return {};
    case K::width_based_em: {
      {
        auto checked = fields(node, {K::width, K::em});
        if (!checked)
          return checked.error;
      }
      auto width_result = unique(node, K::width, true);
      if (!width_result)
        return width_result.error;
      const auto* width = *width_result;
      auto em_result = unique(node, K::em, true);
      if (!em_result)
        return em_result.error;
      const auto* em = *em_result;
      auto limits = count(em);
      if (!limits)
        return limits.error;
      auto widths = count(width);
      if (!widths)
        return widths.error;
      if (*limits != *widths + 1)
        return fail(node, "WIDTH_BASED_EM requires N widths and N+1 limits");
      return {};
    }
    case K::thickness_vs_width_and_spacing: case K::rho_vs_width_and_spacing:
    case K::rpsq_vs_width_and_spacing: case K::etch_vs_width_and_spacing:
    case K::width_si_vs_width_and_spacing:
      return width_spacing_table(node);
    case K::rho_vs_si_width_and_thickness: {
      {
        auto checked = fields(node, {K::width, K::thickness, K::values});
        if (!checked)
          return checked.error;
      }
      auto width_result = unique(node, K::width);
      if (!width_result)
        return width_result.error;
      const auto* width = *width_result;
      auto thickness_result = unique(node, K::thickness);
      if (!thickness_result)
        return thickness_result.error;
      const auto* thickness = *thickness_result;
      if (!width && !thickness)
        return fail(node, "at least one of WIDTH/THICKNESS is required");
      return axis_matrix(node, thickness, width, K::values);
    }
    case K::topology_based_em: {
      auto checked = fields(node, {K::downflow, K::upflow});
      if (!checked)
        return checked.error;
    }
      if (node.children.empty())
        return fail(node, "topology must contain a flow direction");
      {
        auto checked = unique(node, K::downflow);
        if (!checked)
          return checked.error;
      }
      {
        auto checked = unique(node, K::upflow);
        if (!checked)
          return checked.error;
      }
      return {};
    case K::upflow: case K::downflow:
      if (parent == K::topology_based_em) {
        {
          auto checked = fields(node, {K::via_number, K::via_area, K::metal_width, K::em});
          if (!checked)
            return checked.error;
        }
        auto cuts_result = unique(node, K::via_number);
        if (!cuts_result)
          return cuts_result.error;
        const auto* cuts = *cuts_result;
        auto area_result = unique(node, K::via_area);
        if (!area_result)
          return area_result.error;
        const auto* area = *area_result;
        if ((cuts != nullptr) == (area != nullptr))
          return fail(node, "use exactly one of VIA_NUMBER/VIA_AREA");
        auto cut_count = count(cuts ? cuts : area);
        if (!cut_count)
          return cut_count.error;
        auto width_count = required_count(node, K::metal_width);
        if (!width_count)
          return width_count.error;
        if (area && *width_count != 1)
          return fail(node, "area topology requires one dummy METAL_WIDTH");
        auto em = unique(node, K::em, true);
        if (!em)
          return em.error;
        return matrix(**em, *cut_count, *width_count, area != nullptr);
      } else {
        {
          auto checked = modifiers(node, {});
          if (!checked)
            return checked.error;
        }
        {
          auto checked = rows(node, 2);
          if (!checked)
            return checked.error;
        }
      }
      return {};
    case K::polynomial_based_em_dc: case K::polynomial_based_em_rms: case K::polynomial_based_em_peak: {
      {
        auto checked = fields(node, {K::length_ranges, K::width_ranges, K::em_polynomial, K::em_polynomials});
        if (!checked)
          return checked.error;
      }
      auto length_result = unique(node, K::length_ranges);
      if (!length_result)
        return length_result.error;
      const auto* length = *length_result;
      auto width_result = unique(node, K::width_ranges);
      if (!width_result)
        return width_result.error;
      const auto* width = *width_result;
      if (!length && !width)
        return fail(node, "polynomial requires a range axis");
      if (!node.find(K::em_polynomial) && !node.find(K::em_polynomials))
        return fail(node, "polynomial table has no expressions");
      return {};
    }
    case K::em_polynomial: case K::em_polynomials:
      return {};  // Bison validates expressions and IMAX, including operator precedence.
    case K::polynomial_based_thickness_variation: {
      {
        auto checked = fields(node, {K::density_polynomial_orders, K::width_polynomial_orders, K::width_ranges, K::polynomial_coefficients});
        if (!checked)
          return checked.error;
      }
      auto densities = required_count(node, K::density_polynomial_orders);
      if (!densities)
        return densities.error;
      auto widths = required_count(node, K::width_polynomial_orders);
      if (!widths)
        return widths.error;
      auto ranges = required_count(node, K::width_ranges);
      if (!ranges)
        return ranges.error;
      std::size_t count = 0;
      for (const auto& child : node.children) if (child.keyword == K::polynomial_coefficients) {
          ++count;
          {
            auto checked = matrix(child, *widths, *densities);
            if (!checked)
              return checked.error;
          }
      }
      if (count != *ranges + 1)
        return fail(node, "expected one coefficient matrix per width interval");
      return {};
    }
    case K::density_polynomial_orders: case K::width_polynomial_orders: {
      std::set<double> seen;
      auto values = numeric_vector(node);
      if (!values)
        return values.error;
      for (const auto* value : *values) {
        const auto& number = *value->quantity;
        if (!number.suffix.empty() || number.value < 0 || std::floor(number.value) != number.value || !seen.insert(number.value).second)
          return fail(*value, "polynomial orders must be distinct nonnegative integers");
      }
      return {};
    }
    case K::values: case K::polynomial_coefficients:
      return {};  // Dimensions checked against parent axes.
    case K::fill_type: {
      auto value = node.children.front().text;
      std::ranges::transform(value, value.begin(), [](char c) { return c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c; });
      if (value != "GROUNDED" && value != "FLOATING")
        return fail(node, "FILL_TYPE must be GROUNDED or FLOATING");
      return {};
    }
    case K::above: case K::upperlayer: case K::lowerlayer: case K::cap_density:
      return {};  // Fixed argument shapes are enforced by Bison.
    case K::resistive_only: case K::capacitive_only: case K::etch_from_top:
      return {};  // Parent checks placement and mutually exclusive qualifiers.
    default:
      if (info->syntax == Syntax::scalar) {
        return one_number(node);
      }
      return fail(node, "unsupported syntax for " + label(node.keyword));
  }
  return {};
}
} // namespace

Status validate(const Document& document, const ParseOptions& options)
{
  const Validator validator(options);
  for (const auto& statement : document.statements) {
    auto checked = validator.check(statement);
    if (!checked)
      return checked;
  }
  return {};
}
} // namespace ircx::tech::syntax::detail

#include "SpefAnnotationScanner.hh"

#include <cctype>
#include <fstream>
#include <optional>
#include <string_view>
#include <utility>

#include "SpefText.hh"
#include "utility/logger/Logger.hpp"

namespace spef {
namespace {

enum class AnnotationSection
{
  kNone,
  kConn,
  kCap,
  kRes,
  kInduc
};

void mergeGeometry(GeometryAttr& target,
                   const GeometryAttr& source)
{
  if (source.has_box) {
    target.has_box = true;
    target.ll_coordinate = source.ll_coordinate;
    target.ur_coordinate = source.ur_coordinate;
  }
  if (source.has_layer) {
    target.has_layer = true;
    target.layer = source.layer;
  }
  if (source.has_length) {
    target.has_length = true;
    target.length = source.length;
  }
  if (source.has_width) {
    target.has_width = true;
    target.width = source.width;
  }
  if (source.has_area) {
    target.has_area = true;
    target.area = source.area;
  }
  if (source.has_direction) {
    target.has_direction = true;
    target.direction = source.direction;
  }
}

GeometryAttr parseGeometryAnnotation(std::string_view comment)
{
  GeometryAttr geometry;
  std::optional<double> llx;
  std::optional<double> lly;
  std::optional<double> urx;
  std::optional<double> ury;

  while (true) {
    const auto token = text::takeToken(comment);
    if (token.empty()) {
      break;
    }
    if (token.front() != '$') {
      continue;
    }

    const auto equal_pos = token.find('=');
    if (equal_pos == std::string_view::npos || equal_pos <= 1) {
      continue;
    }

    const std::string_view key = token.substr(1, equal_pos - 1);
    const std::string_view value = token.substr(equal_pos + 1);

    if (key == "llx") {
      llx = text::parseDouble(value);
    } else if (key == "lly") {
      lly = text::parseDouble(value);
    } else if (key == "urx") {
      urx = text::parseDouble(value);
    } else if (key == "ury") {
      ury = text::parseDouble(value);
    } else if (key == "lvl" || key == "layer") {
      if (const auto layer = text::parseInt(value)) {
        geometry.has_layer = true;
        geometry.layer = *layer;
      }
    } else if (key == "l" || key == "length") {
      if (const auto length = text::parseDouble(value)) {
        geometry.has_length = true;
        geometry.length = *length;
      }
    } else if (key == "w" || key == "width") {
      if (const auto width = text::parseDouble(value)) {
        geometry.has_width = true;
        geometry.width = *width;
      }
    } else if (key == "a" || key == "area") {
      if (const auto area = text::parseDouble(value)) {
        geometry.has_area = true;
        geometry.area = *area;
      }
    } else if (key == "dir" || key == "direction") {
      if (const auto direction = text::parseInt(value)) {
        geometry.has_direction = true;
        geometry.direction = *direction;
      }
    }
  }

  if (llx.has_value() && lly.has_value() && urx.has_value() && ury.has_value()) {
    geometry.has_box = true;
    geometry.ll_coordinate = Coord{*llx, *lly};
    geometry.ur_coordinate = Coord{*urx, *ury};
  }

  return geometry;
}

bool parseLayerMapLine(std::string_view line,
                       Exchange& exchange)
{
  line = text::trimView(line);
  const auto index_token = text::takeToken(line);
  if (index_token.size() < 2 || index_token.front() != '*'
      || !std::isdigit(static_cast<unsigned char>(index_token[1]))) {
    return false;
  }

  const auto level = text::parseInt(index_token.substr(1));
  const auto layer_name = text::takeToken(line);
  if (!level.has_value() || layer_name.empty()) {
    return false;
  }

  LayerMapEntry entry;
  entry.level = *level;
  entry.layer_name = removeEscapes(stripQuotes(std::string(layer_name)));
  entry.raw_info = std::string(text::trimView(line));
  exchange.layer_map[entry.level] = std::move(entry);
  return true;
}

void applyConnGeometry(ConnEntry& conn,
                       const GeometryAttr& geometry)
{
  mergeGeometry(conn.geometry, geometry);
  if (geometry.has_box) {
    conn.ll_coordinate = geometry.ll_coordinate;
    conn.ur_coordinate = geometry.ur_coordinate;
  }
  if (geometry.has_layer) {
    conn.layer = geometry.layer;
  }
}

void applyResCapGeometry(ResCap& res_cap,
                         const GeometryAttr& geometry)
{
  mergeGeometry(res_cap.geometry, geometry);
}

}  // namespace

void augmentAnnotations(Exchange& exchange)
{
  std::ifstream file(exchange.file_name);
  if (!file.is_open()) {
    ECCLOG.warn(ecc::Loc::current(), "open SPEF annotation scan file failed: ", exchange.file_name);
    return;
  }

  AnnotationSection section = AnnotationSection::kNone;
  Net* current_net = nullptr;
  std::size_t next_net_idx = 0;
  std::size_t conn_idx = 0;
  std::size_t cap_idx = 0;
  std::size_t res_idx = 0;
  std::size_t induc_idx = 0;
  bool in_layer_map = false;

  auto begin_net = [&]() {
    current_net = next_net_idx < exchange.nets.size() ? &exchange.nets[next_net_idx++] : nullptr;
    section = AnnotationSection::kNone;
    conn_idx = 0;
    cap_idx = 0;
    res_idx = 0;
    induc_idx = 0;
  };

  std::string line_storage;
  while (std::getline(file, line_storage)) {
    std::string_view line(line_storage);
    std::string_view normalized = text::trimView(line);
    if (text::startsWith(normalized, "//")) {
      normalized.remove_prefix(2);
      normalized = text::trimView(normalized);
    }

    if (text::startsWith(normalized, "*LAYER_MAP")) {
      in_layer_map = true;
      continue;
    }
    if (in_layer_map) {
      if (parseLayerMapLine(normalized, exchange) || normalized.empty()) {
        continue;
      }
      in_layer_map = false;
    }

    const auto comment_pos = line.find("//");
    const std::string_view content = text::trimView(
        comment_pos == std::string_view::npos ? line : line.substr(0, comment_pos));
    const std::string_view comment =
        comment_pos == std::string_view::npos ? std::string_view{} : line.substr(comment_pos + 2);

    if (content.empty()) {
      continue;
    }

    if (text::startsWith(content, "*D_NET") || text::startsWith(content, "*D_PNET")) {
      begin_net();
      continue;
    }
    if (text::startsWith(content, "*END")) {
      current_net = nullptr;
      section = AnnotationSection::kNone;
      continue;
    }
    if (text::startsWith(content, "*CONN")) {
      section = AnnotationSection::kConn;
      conn_idx = 0;
      continue;
    }
    if (text::startsWith(content, "*CAP")) {
      section = AnnotationSection::kCap;
      cap_idx = 0;
      continue;
    }
    if (text::startsWith(content, "*RES")) {
      section = AnnotationSection::kRes;
      res_idx = 0;
      continue;
    }
    if (text::startsWith(content, "*INDUC")) {
      section = AnnotationSection::kInduc;
      induc_idx = 0;
      continue;
    }

    const bool is_numbered_entry =
        !content.empty() && std::isdigit(static_cast<unsigned char>(content.front()));
    const bool is_conn_entry = text::startsWith(content, "*P")
                               || text::startsWith(content, "*I")
                               || text::startsWith(content, "*N");
    if (current_net == nullptr || (!is_numbered_entry && !is_conn_entry)) {
      continue;
    }

    const GeometryAttr geometry = parseGeometryAnnotation(comment);
    if (section == AnnotationSection::kConn && is_conn_entry) {
      if (conn_idx < current_net->conns.size()) {
        applyConnGeometry(current_net->conns[conn_idx], geometry);
      }
      ++conn_idx;
    } else if (section == AnnotationSection::kCap && is_numbered_entry) {
      if (cap_idx < current_net->caps.size()) {
        applyResCapGeometry(current_net->caps[cap_idx], geometry);
      }
      ++cap_idx;
    } else if (section == AnnotationSection::kRes && is_numbered_entry) {
      if (res_idx < current_net->ress.size()) {
        applyResCapGeometry(current_net->ress[res_idx], geometry);
      }
      ++res_idx;
    } else if (section == AnnotationSection::kInduc && is_numbered_entry) {
      if (induc_idx < current_net->inductances.size()) {
        applyResCapGeometry(current_net->inductances[induc_idx], geometry);
      }
      ++induc_idx;
    }
  }
}

}  // namespace spef

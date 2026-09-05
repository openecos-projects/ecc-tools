#include "lef/LefLibraryImporter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <exception>
#include <chrono>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "lef/detail/LefParserMutex.h"
#include "lefiMacro.hpp"
#include "lefiMisc.hpp"
#include "lefiUnits.hpp"
#include "lefrReader.hpp"

namespace eccdb {
namespace {

using namespace LefDefParser;

using LefNameId = uint32_t;

class LefNamePool
{
 public:
  LefNameId intern(std::string_view value)
  {
    const auto found = _ids.find(value);
    if (found != _ids.end()) {
      return found->second;
    }
    const auto id = static_cast<LefNameId>(_values.size());
    _values.emplace_back(value);
    _ids.emplace(_values.back(), id);
    return id;
  }

  [[nodiscard]] std::string_view get(LefNameId id) const
  {
    if (id >= _values.size()) {
      throw std::out_of_range("invalid staged LEF name id");
    }
    return _values[id];
  }

 private:
  std::deque<std::string> _values;
  std::unordered_map<std::string_view, LefNameId> _ids;
};

std::string_view requiredName(const char* value, const char* field)
{
  if (value == nullptr || *value == '\0') {
    throw std::runtime_error(std::string(field) + " is required");
  }
  return value;
}

struct StagedRect
{
  double ll_x = 0.0;
  double ll_y = 0.0;
  double ur_x = 0.0;
  double ur_y = 0.0;
};

struct StagedPolygon
{
  std::vector<std::pair<double, double>> points;
};

struct StagedViaPlacement
{
  LefNameId via = 0;
  double x = 0.0;
  double y = 0.0;
};

struct StagedPath
{
  double width = 0.0;
  std::vector<std::pair<double, double>> points;
};

struct StagedPortLayerClause
{
  LefNameId layer = 0;
  std::vector<StagedRect> rects;
  std::vector<StagedPolygon> polygons;
  std::vector<StagedPath> paths;
};

struct StagedPort
{
  LibraryMasterPortClass port_class = LibraryMasterPortClass::kNone;
  std::vector<StagedPortLayerClause> layer_clauses;
  std::vector<StagedViaPlacement> vias;
};

struct StagedObsClause
{
  LefNameId layer = 0;
  uint32_t flags = 0;
  double spacing = 0.0;
  double design_rule_width = 0.0;
  double path_width = 0.0;
  std::vector<StagedRect> rects;
  std::vector<StagedPolygon> polygons;
  std::vector<StagedPath> paths;
};

struct StagedObs
{
  std::vector<StagedObsClause> clauses;
  std::vector<StagedViaPlacement> vias;
};

struct StagedTerm
{
  LibraryMasterTerm term;
  std::vector<StagedPort> ports;
};

struct StagedSite
{
  LibrarySite site;
  std::optional<std::pair<double, double>> size;
};

struct StagedMaster
{
  LibraryCellMaster master;
  std::optional<std::string> site;
  std::optional<std::pair<double, double>> origin;
  std::optional<std::pair<double, double>> size;
  std::vector<StagedTerm> terms;
  std::vector<StagedObs> obstructions;
};

struct Staging
{
  std::optional<int32_t> database_units_per_micron;
  std::vector<StagedSite> sites;
  std::vector<StagedMaster> masters;
  std::unordered_set<std::string> site_names;
  std::unordered_set<std::string> master_names;
  std::optional<size_t> current_master;
  std::unordered_map<std::string, size_t> diagnostics;
  LefNamePool names;
  std::exception_ptr callback_failure;
  const TechStore* direct_technology = nullptr;
  LibraryStore* direct_library = nullptr;
  int32_t direct_units = 0;
  std::optional<LibraryCellMasterId> direct_master;
  std::optional<std::string> direct_master_site;
  LefLibraryImportTiming* direct_timing = nullptr;

  // SI2 callbacks repeat layer/via names many times. Keep the lookup result
  // for the duration of this import, matching OpenDB's indexed find calls.
  std::unordered_map<std::string_view, TechLayerId> direct_layers;
  std::unordered_map<std::string_view, TechViaMasterId> direct_vias;
};

struct PreparedMaster
{
  LibraryCellMaster master;
  std::optional<std::string> site;
  std::vector<std::pair<LibraryMasterTerm, std::vector<LibraryMasterPortInput>>> terms;
  LibraryMasterObsInput obs;
};

std::string upper(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
  return value;
}

std::string requiredText(const char* value, const char* field)
{
  if (value == nullptr || *value == '\0') {
    throw std::runtime_error(std::string(field) + " is required");
  }
  return value;
}

void recordDiagnostic(Staging& staging, std::string statement, size_t count = 1)
{
  if (count != 0) {
    staging.diagnostics[std::move(statement)] += count;
  }
}

bool directMode(const Staging& staging)
{
  return staging.direct_technology != nullptr && staging.direct_library != nullptr;
}

LibraryCellMasterId directMaster(Staging& staging)
{
  if (!staging.direct_master) {
    throw std::runtime_error("SI2 MACRO child callback appears outside MACRO");
  }
  return *staging.direct_master;
}

Rect prepareRect(const StagedRect& source, int32_t units);
GeometryPolygonInput preparePolygon(const StagedPolygon& source, int32_t units);
void appendPreparedPath(std::vector<Rect>& target, const StagedPath& source, TechLayerId layer, const TechStore& technology,
                        int32_t units);
LibraryViaPlacement prepareVia(const StagedViaPlacement& source, const LefNamePool& names, const TechStore& technology, int32_t units);
int32_t toDbu(double value, int32_t units, const char* field);
int32_t fastDbu(double value, int32_t units) noexcept;
int64_t toDbu64(double value, int32_t units, const char* field);
uint32_t toUnsignedDbu(double value, int32_t units, const char* field);
LibraryMasterPortClass portClass(const char* value);
size_t repeatCount(double value, const char* field);
int directRepeatCount(double value) noexcept;
LibraryMasterPortInput directPort(const lefiGeometries& geometry, const TechStore& technology,
                                  const Staging& staging, int32_t units);
LibraryMasterObsInput directObs(const lefiGeometries& geometry, const TechStore& technology,
                                const Staging& staging, int32_t units);

LibraryMasterPortInput materializePort(const StagedPort& source, const LefNamePool& names, const TechStore& technology, int32_t units)
{
  LibraryMasterPortInput result{.port_class = source.port_class};
  result.layer_clauses.reserve(source.layer_clauses.size());
  for (const auto& source_clause : source.layer_clauses) {
    const auto layer_name = names.get(source_clause.layer);
    const auto layer = technology.findLayer(layer_name);
    if (!layer) {
      throw std::runtime_error("PORT geometry references an unknown Tech LAYER: " + std::string(layer_name));
    }
    LibraryPortLayerGeometryInput clause{.layer = layer};
    clause.geometry.rects.reserve(source_clause.rects.size() + source_clause.paths.size());
    for (const auto& source_rect : source_clause.rects) {
      clause.geometry.rects.push_back(prepareRect(source_rect, units));
    }
    clause.geometry.polygons.reserve(source_clause.polygons.size());
    for (const auto& source_polygon : source_clause.polygons) {
      clause.geometry.polygons.push_back(preparePolygon(source_polygon, units));
    }
    for (const auto& source_path : source_clause.paths) {
      appendPreparedPath(clause.geometry.rects, source_path, layer, technology, units);
    }
    if (!clause.geometry.empty()) {
      result.layer_clauses.push_back(std::move(clause));
    }
  }
  result.vias.reserve(source.vias.size());
  for (const auto& source_via : source.vias) {
    result.vias.push_back(prepareVia(source_via, names, technology, units));
  }
  return result;
}

Rect directRect(double x1, double y1, double x2, double y2, int32_t units)
{
  // Matches odb::Rect::init: scale/round first, then normalize the corners.
  return Rect{.ll_x = fastDbu(x1, units),
              .ll_y = fastDbu(y1, units),
              .ur_x = fastDbu(x2, units),
              .ur_y = fastDbu(y2, units)}
      .normalized();
}

GeometryPolygonInput directPolygon(const lefiGeomPolygon& polygon, int32_t units, double offset_x = 0.0, double offset_y = 0.0)
{
  GeometryPolygonInput result;
  result.points.reserve(static_cast<size_t>(polygon.numPoints));
  for (int index = 0; index < polygon.numPoints; ++index) {
    result.points.push_back(Point{fastDbu(polygon.x[index] + offset_x, units), fastDbu(polygon.y[index] + offset_y, units)});
  }
  return result;
}

int32_t directPathHalfWidth(double width, TechLayerId layer, const TechStore& technology, int32_t units)
{
  int32_t dbu_width = 0;
  if (width != 0.0) {
    dbu_width = fastDbu(width, units);
  } else {
    const auto& registry = technology.techRegistry().registry();
    if (registry.all_of<TechRoutingLayer>(layer.entity())) {
      dbu_width = registry.get<const TechRoutingLayer>(layer.entity()).width;
    }
  }
  return dbu_width >> 1;
}

void appendDirectPath(std::vector<Rect>& target, const lefiGeomPath& path, double width, TechLayerId layer, const TechStore& technology,
                      int32_t units, double offset_x = 0.0, double offset_y = 0.0)
{
  const int32_t half_width = directPathHalfWidth(width, layer, technology, units);
  const auto coordinate = [&](double value) { return fastDbu(value, units); };
  if (path.numPoints == 1) {
    const auto x = coordinate(path.x[0] + offset_x);
    const auto y = coordinate(path.y[0] + offset_y);
    target.push_back(Rect{x - half_width, y - half_width, x + half_width, y + half_width});
    return;
  }
  for (int index = 1; index < path.numPoints; ++index) {
    const auto x1 = coordinate(path.x[index - 1] + offset_x);
    const auto y1 = coordinate(path.y[index - 1] + offset_y);
    const auto x2 = coordinate(path.x[index] + offset_x);
    const auto y2 = coordinate(path.y[index] + offset_y);
    if (x1 == x2) {
      target.push_back(Rect{x1 - half_width, std::min(y1, y2) - half_width, x1 + half_width,
                            std::max(y1, y2) + half_width});
    } else if (y1 == y2) {
      target.push_back(Rect{std::min(x1, x2) - half_width, y1 - half_width, std::max(x1, x2) + half_width,
                            y1 + half_width});
    }
  }
}

LibraryViaPlacement directVia(const lefiGeomVia& via, const Staging& staging, int32_t units, double offset_x = 0.0,
                              double offset_y = 0.0)
{
  const auto found = staging.direct_vias.find(std::string_view{via.name});
  if (found == staging.direct_vias.end()) {
    throw std::runtime_error("Library geometry references an unknown Tech VIA");
  }
  return LibraryViaPlacement{.via = found->second,
                             .origin = Point{fastDbu(via.x + offset_x, units), fastDbu(via.y + offset_y, units)}};
}

LibraryMasterPortInput directPort(const lefiGeometries& geometry, const TechStore& technology,
                                  const Staging& staging, int32_t units)
{
  LibraryMasterPortInput result;
  LibraryPortLayerGeometryInput* clause = nullptr;
  double width = 0.0;
  for (int index = 0; index < geometry.numItems(); ++index) {
    switch (geometry.itemType(index)) {
      case lefiGeomClassE:
        result.port_class = portClass(geometry.getClass(index));
        break;
      case lefiGeomLayerE: {
        const auto found = staging.direct_layers.find(std::string_view{geometry.getLayer(index)});
        if (found == staging.direct_layers.end()) {
          throw std::runtime_error("PORT geometry references an unknown Tech LAYER");
        }
        result.layer_clauses.push_back(LibraryPortLayerGeometryInput{.layer = found->second});
        clause = &result.layer_clauses.back();
        width = 0.0;
        break;
      }
      case lefiGeomWidthE:
        width = geometry.getWidth(index);
        break;
      case lefiGeomRectE:
        clause->geometry.rects.push_back(directRect(geometry.getRect(index)->xl, geometry.getRect(index)->yl, geometry.getRect(index)->xh,
                                                    geometry.getRect(index)->yh, units));
        break;
      case lefiGeomRectIterE: {
        const auto* rect = geometry.getRectIter(index);
        const auto count_x = directRepeatCount(rect->xStart);
        const auto count_y = directRepeatCount(rect->yStart);
        for (size_t x = 0; x < count_x; ++x) for (size_t y = 0; y < count_y; ++y) {
          clause->geometry.rects.push_back(directRect(rect->xl + static_cast<double>(x) * rect->xStep,
                                                      rect->yl + static_cast<double>(y) * rect->yStep,
                                                      rect->xh + static_cast<double>(x) * rect->xStep,
                                                      rect->yh + static_cast<double>(y) * rect->yStep, units));
        }
        break;
      }
      case lefiGeomPolygonE:
        clause->geometry.polygons.push_back(directPolygon(*geometry.getPolygon(index), units));
        break;
      case lefiGeomPolygonIterE: {
        const auto* polygon = geometry.getPolygonIter(index);
        const auto count_x = directRepeatCount(polygon->xStart);
        const auto count_y = directRepeatCount(polygon->yStart);
        const lefiGeomPolygon base{polygon->numPoints, polygon->x, polygon->y, polygon->colorMask};
        for (size_t x = 0; x < count_x; ++x) for (size_t y = 0; y < count_y; ++y) {
          clause->geometry.polygons.push_back(directPolygon(base, units, static_cast<double>(x) * polygon->xStep,
                                                            static_cast<double>(y) * polygon->yStep));
        }
        break;
      }
      case lefiGeomPathE:
        appendDirectPath(clause->geometry.rects, *geometry.getPath(index), width, clause->layer, technology, units);
        break;
      case lefiGeomPathIterE: {
        const auto* path = geometry.getPathIter(index);
        const auto count_x = directRepeatCount(path->xStart);
        const auto count_y = directRepeatCount(path->yStart);
        const lefiGeomPath base{path->numPoints, path->x, path->y, path->colorMask};
        for (size_t x = 0; x < count_x; ++x) for (size_t y = 0; y < count_y; ++y) {
          appendDirectPath(clause->geometry.rects, base, width, clause->layer, technology, units,
                            static_cast<double>(x) * path->xStep, static_cast<double>(y) * path->yStep);
        }
        break;
      }
      case lefiGeomViaE:
        result.vias.push_back(directVia(*geometry.getVia(index), staging, units));
        break;
      case lefiGeomViaIterE: {
        const auto* via = geometry.getViaIter(index);
        const auto count_x = directRepeatCount(via->xStart);
        const auto count_y = directRepeatCount(via->yStart);
        const lefiGeomVia base{via->name, via->x, via->y, via->topMaskNum, via->cutMaskNum, via->bottomMaskNum};
        for (size_t x = 0; x < count_x; ++x) for (size_t y = 0; y < count_y; ++y) {
          result.vias.push_back(directVia(base, staging, units, static_cast<double>(x) * via->xStep,
                                           static_cast<double>(y) * via->yStep));
        }
        break;
      }
      case lefiGeomLayerExceptPgNetE:
      case lefiGeomLayerMinSpacingE:
      case lefiGeomLayerRuleWidthE:
        break;
      default:
        break;
    }
  }
  return result;
}

LibraryMasterObsInput directObs(const lefiGeometries& geometry, const TechStore& technology,
                                const Staging& staging, int32_t units)
{
  LibraryMasterObsInput result;
  LibraryObsLayerClauseInput* clause = nullptr;
  double path_width = 0.0;
  for (int index = 0; index < geometry.numItems(); ++index) {
    switch (geometry.itemType(index)) {
      case lefiGeomLayerE: {
        const auto found = staging.direct_layers.find(std::string_view{geometry.getLayer(index)});
        if (found == staging.direct_layers.end()) {
          throw std::runtime_error("OBS geometry references an unknown Tech LAYER");
        }
        result.layer_clauses.push_back(LibraryObsLayerClauseInput{.layer = found->second});
        clause = &result.layer_clauses.back();
        path_width = 0.0;
        break;
      }
      case lefiGeomLayerExceptPgNetE:
        clause->flags |= LibraryObsLayerFlag::kExceptPgNet;
        break;
      case lefiGeomLayerMinSpacingE:
        clause->flags |= LibraryObsLayerFlag::kHasSpacing;
        clause->spacing = fastDbu(geometry.getLayerMinSpacing(index), units);
        break;
      case lefiGeomLayerRuleWidthE:
        clause->flags |= LibraryObsLayerFlag::kHasDesignRuleWidth;
        clause->design_rule_width = fastDbu(geometry.getLayerRuleWidth(index), units);
        break;
      case lefiGeomWidthE:
        path_width = geometry.getWidth(index);
        clause->flags |= LibraryObsLayerFlag::kHasPathWidth;
        clause->path_width = fastDbu(path_width, units);
        break;
      case lefiGeomRectE: {
        const auto* rect = geometry.getRect(index);
        clause->geometry.rects.push_back(directRect(rect->xl, rect->yl, rect->xh, rect->yh, units));
        break;
      }
      case lefiGeomRectIterE: {
        const auto* rect = geometry.getRectIter(index);
        const auto count_x = directRepeatCount(rect->xStart);
        const auto count_y = directRepeatCount(rect->yStart);
        for (size_t x = 0; x < count_x; ++x) {
          for (size_t y = 0; y < count_y; ++y) {
            const auto offset_x = static_cast<double>(x) * rect->xStep;
            const auto offset_y = static_cast<double>(y) * rect->yStep;
            clause->geometry.rects.push_back(directRect(rect->xl + offset_x, rect->yl + offset_y, rect->xh + offset_x,
                                                        rect->yh + offset_y, units));
          }
        }
        break;
      }
      case lefiGeomPolygonE:
        clause->geometry.polygons.push_back(directPolygon(*geometry.getPolygon(index), units));
        break;
      case lefiGeomPolygonIterE: {
        const auto* polygon = geometry.getPolygonIter(index);
        const auto count_x = directRepeatCount(polygon->xStart);
        const auto count_y = directRepeatCount(polygon->yStart);
        const lefiGeomPolygon base{polygon->numPoints, polygon->x, polygon->y, polygon->colorMask};
        for (size_t x = 0; x < count_x; ++x) {
          for (size_t y = 0; y < count_y; ++y) {
            clause->geometry.polygons.push_back(directPolygon(base, units, static_cast<double>(x) * polygon->xStep,
                                                              static_cast<double>(y) * polygon->yStep));
          }
        }
        break;
      }
      case lefiGeomPathE:
        appendDirectPath(clause->geometry.rects, *geometry.getPath(index), path_width, clause->layer, technology, units);
        break;
      case lefiGeomPathIterE: {
        const auto* path = geometry.getPathIter(index);
        const auto count_x = directRepeatCount(path->xStart);
        const auto count_y = directRepeatCount(path->yStart);
        const lefiGeomPath base{path->numPoints, path->x, path->y, path->colorMask};
        for (size_t x = 0; x < count_x; ++x) {
          for (size_t y = 0; y < count_y; ++y) {
            appendDirectPath(clause->geometry.rects, base, path_width, clause->layer, technology, units,
                              static_cast<double>(x) * path->xStep, static_cast<double>(y) * path->yStep);
          }
        }
        break;
      }
      case lefiGeomViaE:
        result.vias.push_back(directVia(*geometry.getVia(index), staging, units));
        break;
      case lefiGeomViaIterE: {
        const auto* via = geometry.getViaIter(index);
        const auto count_x = directRepeatCount(via->xStart);
        const auto count_y = directRepeatCount(via->yStart);
        const lefiGeomVia base{via->name, via->x, via->y, via->topMaskNum, via->cutMaskNum, via->bottomMaskNum};
        for (size_t x = 0; x < count_x; ++x) {
          for (size_t y = 0; y < count_y; ++y) {
            result.vias.push_back(directVia(base, staging, units, static_cast<double>(x) * via->xStep,
                                             static_cast<double>(y) * via->yStep));
          }
        }
        break;
      }
      case lefiGeomClassE:
        break;
      default:
        break;
    }
  }
  return result;
}

LibraryMasterObsInput materializeObs(const StagedObs& source, const LefNamePool& names, const TechStore& technology, int32_t units)
{
  LibraryMasterObsInput result;
  result.layer_clauses.reserve(source.clauses.size());
  for (const auto& source_clause : source.clauses) {
    const auto layer_name = names.get(source_clause.layer);
    const auto layer = technology.findLayer(layer_name);
    if (!layer) {
      throw std::runtime_error("OBS geometry references an unknown Tech LAYER: " + std::string(layer_name));
    }
    LibraryObsLayerClauseInput clause{.layer = layer, .flags = source_clause.flags};
    if ((clause.flags & LibraryObsLayerFlag::kHasSpacing) != 0u) {
      clause.spacing = toDbu(source_clause.spacing, units, "OBS SPACING");
    }
    if ((clause.flags & LibraryObsLayerFlag::kHasDesignRuleWidth) != 0u) {
      clause.design_rule_width = toDbu(source_clause.design_rule_width, units, "OBS DESIGNRULEWIDTH");
    }
    if ((clause.flags & LibraryObsLayerFlag::kHasPathWidth) != 0u) {
      clause.path_width = toDbu(source_clause.path_width, units, "OBS WIDTH");
    }
    clause.geometry.rects.reserve(source_clause.rects.size() + source_clause.paths.size());
    for (const auto& source_rect : source_clause.rects) {
      clause.geometry.rects.push_back(prepareRect(source_rect, units));
    }
    clause.geometry.polygons.reserve(source_clause.polygons.size());
    for (const auto& source_polygon : source_clause.polygons) {
      clause.geometry.polygons.push_back(preparePolygon(source_polygon, units));
    }
    for (const auto& source_path : source_clause.paths) {
      appendPreparedPath(clause.geometry.rects, source_path, layer, technology, units);
    }
    if (!clause.geometry.empty()) {
      result.layer_clauses.push_back(std::move(clause));
    }
  }
  result.vias.reserve(source.vias.size());
  for (const auto& source_via : source.vias) {
    result.vias.push_back(prepareVia(source_via, names, technology, units));
  }
  return result;
}

LibrarySiteClass siteClass(const char* value)
{
  const auto type = upper(requiredText(value, "SITE CLASS"));
  if (type == "CORE") {
    return LibrarySiteClass::kCore;
  }
  if (type == "PAD") {
    return LibrarySiteClass::kPad;
  }
  if (type == "VIRTUAL") {
    return LibrarySiteClass::kVirtual;
  }
  if (type == "CORNER") {
    return LibrarySiteClass::kCorner;
  }
  throw std::runtime_error("unsupported LEF SITE CLASS: " + type);
}

LibraryCellMasterType masterType(const char* value)
{
  static const std::unordered_map<std::string, LibraryCellMasterType> types{
      {"COVER", LibraryCellMasterType::kCover},
      {"COVER BUMP", LibraryCellMasterType::kCoverBump},
      {"RING", LibraryCellMasterType::kRing},
      {"BLOCK", LibraryCellMasterType::kBlock},
      {"BLOCK BLACKBOX", LibraryCellMasterType::kBlockBlackbox},
      {"BLOCK SOFT", LibraryCellMasterType::kBlockSoft},
      {"PAD", LibraryCellMasterType::kPad},
      {"PAD INPUT", LibraryCellMasterType::kPadInput},
      {"PAD OUTPUT", LibraryCellMasterType::kPadOutput},
      {"PAD INOUT", LibraryCellMasterType::kPadInOut},
      {"PAD POWER", LibraryCellMasterType::kPadPower},
      {"PAD SPACER", LibraryCellMasterType::kPadSpacer},
      {"PAD AREAIO", LibraryCellMasterType::kPadAreaIo},
      {"CORE", LibraryCellMasterType::kCore},
      {"CORE FEEDTHRU", LibraryCellMasterType::kCoreFeedThru},
      {"CORE TIEHIGH", LibraryCellMasterType::kCoreTieHigh},
      {"CORE TIELOW", LibraryCellMasterType::kCoreTieLow},
      {"CORE SPACER", LibraryCellMasterType::kCoreSpacer},
      {"CORE ANTENNACELL", LibraryCellMasterType::kCoreAntennaCell},
      {"CORE WELLTAP", LibraryCellMasterType::kCoreWelltap},
      {"ENDCAP", LibraryCellMasterType::kEndcap},
      {"ENDCAP PRE", LibraryCellMasterType::kEndcapPre},
      {"ENDCAP POST", LibraryCellMasterType::kEndcapPost},
      {"ENDCAP TOPLEFT", LibraryCellMasterType::kEndcapTopLeft},
      {"ENDCAP TOPRIGHT", LibraryCellMasterType::kEndcapTopRight},
      {"ENDCAP BOTTOMLEFT", LibraryCellMasterType::kEndcapBottomLeft},
      {"ENDCAP BOTTOMRIGHT", LibraryCellMasterType::kEndcapBottomRight},
  };
  const auto type = upper(requiredText(value, "MACRO CLASS"));
  const auto found = types.find(type);
  if (found == types.end()) {
    throw std::runtime_error("unsupported LEF MACRO CLASS: " + type);
  }
  return found->second;
}

LibraryMasterTermDirection termDirection(const char* value)
{
  const auto direction = upper(requiredText(value, "PIN DIRECTION"));
  if (direction == "INPUT") {
    return LibraryMasterTermDirection::kInput;
  }
  if (direction == "OUTPUT") {
    return LibraryMasterTermDirection::kOutput;
  }
  if (direction == "OUTPUT TRISTATE") {
    return LibraryMasterTermDirection::kOutputTriState;
  }
  if (direction == "INOUT") {
    return LibraryMasterTermDirection::kInOut;
  }
  if (direction == "FEEDTHRU") {
    return LibraryMasterTermDirection::kFeedThru;
  }
  throw std::runtime_error("unsupported LEF PIN DIRECTION: " + direction);
}

LibraryMasterTermUse termUse(const char* value)
{
  const auto use = upper(requiredText(value, "PIN USE"));
  if (use == "SIGNAL" || use == "DATA") {
    return LibraryMasterTermUse::kSignal;
  }
  if (use == "ANALOG") {
    return LibraryMasterTermUse::kAnalog;
  }
  if (use == "POWER") {
    return LibraryMasterTermUse::kPower;
  }
  if (use == "GROUND") {
    return LibraryMasterTermUse::kGround;
  }
  if (use == "CLOCK") {
    return LibraryMasterTermUse::kClock;
  }
  if (use == "TIEOFF") {
    return LibraryMasterTermUse::kTieOff;
  }
  if (use == "SCAN") {
    return LibraryMasterTermUse::kScan;
  }
  if (use == "RESET") {
    return LibraryMasterTermUse::kReset;
  }
  throw std::runtime_error("unsupported LEF PIN USE: " + use);
}

LibraryMasterTermShape termShape(const char* value)
{
  const auto shape = upper(requiredText(value, "PIN SHAPE"));
  if (shape == "ABUTMENT") {
    return LibraryMasterTermShape::kAbutment;
  }
  if (shape == "RING") {
    return LibraryMasterTermShape::kRing;
  }
  if (shape == "FEEDTHRU") {
    return LibraryMasterTermShape::kFeedThru;
  }
  throw std::runtime_error("unsupported LEF PIN SHAPE: " + shape);
}

LibraryMasterPortClass portClass(const char* value)
{
  const auto type = upper(requiredText(value, "PORT CLASS"));
  if (type == "NONE") {
    return LibraryMasterPortClass::kNone;
  }
  if (type == "CORE") {
    return LibraryMasterPortClass::kCore;
  }
  if (type == "BUMP") {
    return LibraryMasterPortClass::kBump;
  }
  throw std::runtime_error("unsupported LEF PORT CLASS: " + type);
}

size_t repeatCount(double value, const char* field)
{
  if (!std::isfinite(value) || value <= 0.0 || value > 1'000'000.0 || std::floor(value) != value) {
    throw std::runtime_error(std::string(field) + " must be a positive integer");
  }
  return static_cast<size_t>(value);
}

int directRepeatCount(double value) noexcept
{
  // OpenDB treats LEF iterate counts as rounded loop bounds.
  return static_cast<int>(std::lround(value));
}

void appendRect(std::vector<StagedRect>& target, const lefiGeomRect& rect, double offset_x = 0.0, double offset_y = 0.0)
{
  if (rect.colorMask != 0) {
    throw std::runtime_error("LEF geometry MASK is not represented by the current Library model");
  }
  target.push_back(
      StagedRect{.ll_x = rect.xl + offset_x, .ll_y = rect.yl + offset_y, .ur_x = rect.xh + offset_x, .ur_y = rect.yh + offset_y});
}

void appendPolygon(std::vector<StagedPolygon>& target, const lefiGeomPolygon& polygon, double offset_x = 0.0, double offset_y = 0.0)
{
  if (polygon.colorMask != 0) {
    throw std::runtime_error("LEF geometry MASK is not represented by the current Library model");
  }
  if (polygon.numPoints < 3 || polygon.x == nullptr || polygon.y == nullptr) {
    throw std::runtime_error("LEF POLYGON requires at least three points");
  }
  StagedPolygon staged;
  staged.points.reserve(static_cast<size_t>(polygon.numPoints));
  for (int index = 0; index < polygon.numPoints; ++index) {
    staged.points.emplace_back(polygon.x[index] + offset_x, polygon.y[index] + offset_y);
  }
  target.push_back(std::move(staged));
}

void appendPath(std::vector<StagedPath>& target, const lefiGeomPath& path, double width, double offset_x = 0.0, double offset_y = 0.0)
{
  if (path.colorMask != 0) {
    throw std::runtime_error("LEF geometry MASK is not represented by the current Library model");
  }
  if (!std::isfinite(width) || width < 0.0 || path.numPoints < 1) {
    throw std::runtime_error("LEF PATH requires a non-negative WIDTH and at least one point");
  }
  StagedPath staged{.width = width};
  staged.points.reserve(static_cast<size_t>(path.numPoints));
  for (int index = 0; index < path.numPoints; ++index) {
    staged.points.emplace_back(path.x[index] + offset_x, path.y[index] + offset_y);
  }
  for (int index = 1; index < path.numPoints; ++index) {
    const double x1 = path.x[index - 1] + offset_x;
    const double y1 = path.y[index - 1] + offset_y;
    const double x2 = path.x[index] + offset_x;
    const double y2 = path.y[index] + offset_y;
    if (x1 != x2 && y1 != y2) {
      throw std::runtime_error("non-Manhattan LEF PATH cannot be represented by Library RECT geometry");
    }
  }
  target.push_back(std::move(staged));
}

void appendVia(LefNamePool& names, std::vector<StagedViaPlacement>& target, const lefiGeomVia& via, double offset_x = 0.0,
               double offset_y = 0.0)
{
  if (via.topMaskNum != 0 || via.cutMaskNum != 0 || via.bottomMaskNum != 0) {
    throw std::runtime_error("LEF VIA placement MASK is not represented by the current Library model");
  }
  target.push_back(StagedViaPlacement{.via = names.intern(requiredName(via.name, "geometry VIA name")),
                                      .x = via.x + offset_x,
                                      .y = via.y + offset_y});
}

StagedPort stagePort(const lefiGeometries& geometry, LefNamePool& names)
{
  StagedPort result;
  StagedPortLayerClause* clause = nullptr;
  double width = 0.0;
  for (int index = 0; index < geometry.numItems(); ++index) {
    switch (geometry.itemType(index)) {
      case lefiGeomClassE:
        result.port_class = portClass(geometry.getClass(index));
        break;
      case lefiGeomLayerE:
        result.layer_clauses.push_back(StagedPortLayerClause{.layer = names.intern(requiredName(geometry.getLayer(index), "PORT LAYER"))});
        clause = &result.layer_clauses.back();
        width = 0.0;
        break;
      case lefiGeomWidthE:
        if (clause == nullptr) {
          throw std::runtime_error("PORT WIDTH appears before LAYER");
        }
        width = geometry.getWidth(index);
        break;
      case lefiGeomRectE:
        if (clause == nullptr) {
          throw std::runtime_error("PORT RECT appears before LAYER");
        }
        appendRect(clause->rects, *geometry.getRect(index));
        break;
      case lefiGeomRectIterE: {
        if (clause == nullptr) {
          throw std::runtime_error("PORT RECT ITERATE appears before LAYER");
        }
        const auto* rect = geometry.getRectIter(index);
        const auto count_x = repeatCount(rect->xStart, "RECT ITERATE DO count");
        const auto count_y = repeatCount(rect->yStart, "RECT ITERATE BY count");
        const lefiGeomRect base{rect->xl, rect->yl, rect->xh, rect->yh, rect->colorMask};
        for (size_t x = 0; x < count_x; ++x) {
          for (size_t y = 0; y < count_y; ++y) {
            appendRect(clause->rects, base, static_cast<double>(x) * rect->xStep, static_cast<double>(y) * rect->yStep);
          }
        }
        break;
      }
      case lefiGeomPolygonE:
        if (clause == nullptr) {
          throw std::runtime_error("PORT POLYGON appears before LAYER");
        }
        appendPolygon(clause->polygons, *geometry.getPolygon(index));
        break;
      case lefiGeomPolygonIterE: {
        if (clause == nullptr) {
          throw std::runtime_error("PORT POLYGON ITERATE appears before LAYER");
        }
        const auto* polygon = geometry.getPolygonIter(index);
        const auto count_x = repeatCount(polygon->xStart, "POLYGON ITERATE DO count");
        const auto count_y = repeatCount(polygon->yStart, "POLYGON ITERATE BY count");
        const lefiGeomPolygon base{polygon->numPoints, polygon->x, polygon->y, polygon->colorMask};
        for (size_t x = 0; x < count_x; ++x) {
          for (size_t y = 0; y < count_y; ++y) {
            appendPolygon(clause->polygons, base, static_cast<double>(x) * polygon->xStep, static_cast<double>(y) * polygon->yStep);
          }
        }
        break;
      }
      case lefiGeomPathE:
        if (clause == nullptr) {
          throw std::runtime_error("PORT PATH appears before LAYER");
        }
        appendPath(clause->paths, *geometry.getPath(index), width);
        break;
      case lefiGeomPathIterE: {
        if (clause == nullptr) {
          throw std::runtime_error("PORT PATH ITERATE appears before LAYER");
        }
        const auto* path = geometry.getPathIter(index);
        const auto count_x = repeatCount(path->xStart, "PATH ITERATE DO count");
        const auto count_y = repeatCount(path->yStart, "PATH ITERATE BY count");
        const lefiGeomPath base{path->numPoints, path->x, path->y, path->colorMask};
        for (size_t x = 0; x < count_x; ++x) {
          for (size_t y = 0; y < count_y; ++y) {
            appendPath(clause->paths, base, width, static_cast<double>(x) * path->xStep, static_cast<double>(y) * path->yStep);
          }
        }
        break;
      }
      case lefiGeomViaE:
        appendVia(names, result.vias, *geometry.getVia(index));
        break;
      case lefiGeomViaIterE: {
        const auto* via = geometry.getViaIter(index);
        const auto count_x = repeatCount(via->xStart, "VIA ITERATE DO count");
        const auto count_y = repeatCount(via->yStart, "VIA ITERATE BY count");
        const lefiGeomVia base{via->name, via->x, via->y, via->topMaskNum, via->cutMaskNum, via->bottomMaskNum};
        for (size_t x = 0; x < count_x; ++x) {
          for (size_t y = 0; y < count_y; ++y) {
            appendVia(names, result.vias, base, static_cast<double>(x) * via->xStep, static_cast<double>(y) * via->yStep);
          }
        }
        break;
      }
      case lefiGeomLayerExceptPgNetE:
      case lefiGeomLayerMinSpacingE:
      case lefiGeomLayerRuleWidthE:
        throw std::runtime_error("OBS-only LAYER attribute appears in a PORT");
      case lefiGeomUnknown:
      case lefiGeomEnd:
      default:
        throw std::runtime_error("unsupported LEF PORT geometry item");
    }
  }
  return result;
}

StagedObs stageObs(const lefiGeometries& geometry, LefNamePool& names)
{
  StagedObs result;
  StagedObsClause* clause = nullptr;
  for (int index = 0; index < geometry.numItems(); ++index) {
    switch (geometry.itemType(index)) {
      case lefiGeomLayerE:
        result.clauses.push_back(StagedObsClause{.layer = names.intern(requiredName(geometry.getLayer(index), "OBS LAYER"))});
        clause = &result.clauses.back();
        break;
      case lefiGeomLayerExceptPgNetE:
        if (clause == nullptr) {
          throw std::runtime_error("OBS EXCEPTPGNET appears before LAYER");
        }
        clause->flags |= LibraryObsLayerFlag::kExceptPgNet;
        break;
      case lefiGeomLayerMinSpacingE:
        if (clause == nullptr) {
          throw std::runtime_error("OBS SPACING appears before LAYER");
        }
        clause->flags |= LibraryObsLayerFlag::kHasSpacing;
        clause->spacing = geometry.getLayerMinSpacing(index);
        break;
      case lefiGeomLayerRuleWidthE:
        if (clause == nullptr) {
          throw std::runtime_error("OBS DESIGNRULEWIDTH appears before LAYER");
        }
        clause->flags |= LibraryObsLayerFlag::kHasDesignRuleWidth;
        clause->design_rule_width = geometry.getLayerRuleWidth(index);
        break;
      case lefiGeomWidthE:
        if (clause == nullptr) {
          throw std::runtime_error("OBS WIDTH appears before LAYER");
        }
        clause->flags |= LibraryObsLayerFlag::kHasPathWidth;
        clause->path_width = geometry.getWidth(index);
        break;
      case lefiGeomRectE:
        if (clause == nullptr) {
          throw std::runtime_error("OBS RECT appears before LAYER");
        }
        appendRect(clause->rects, *geometry.getRect(index));
        break;
      case lefiGeomRectIterE: {
        if (clause == nullptr) {
          throw std::runtime_error("OBS RECT ITERATE appears before LAYER");
        }
        const auto* rect = geometry.getRectIter(index);
        const auto count_x = repeatCount(rect->xStart, "RECT ITERATE DO count");
        const auto count_y = repeatCount(rect->yStart, "RECT ITERATE BY count");
        const lefiGeomRect base{rect->xl, rect->yl, rect->xh, rect->yh, rect->colorMask};
        for (size_t x = 0; x < count_x; ++x) {
          for (size_t y = 0; y < count_y; ++y) {
            appendRect(clause->rects, base, static_cast<double>(x) * rect->xStep, static_cast<double>(y) * rect->yStep);
          }
        }
        break;
      }
      case lefiGeomPolygonE:
        if (clause == nullptr) {
          throw std::runtime_error("OBS POLYGON appears before LAYER");
        }
        appendPolygon(clause->polygons, *geometry.getPolygon(index));
        break;
      case lefiGeomPolygonIterE: {
        if (clause == nullptr) {
          throw std::runtime_error("OBS POLYGON ITERATE appears before LAYER");
        }
        const auto* polygon = geometry.getPolygonIter(index);
        const auto count_x = repeatCount(polygon->xStart, "POLYGON ITERATE DO count");
        const auto count_y = repeatCount(polygon->yStart, "POLYGON ITERATE BY count");
        const lefiGeomPolygon base{polygon->numPoints, polygon->x, polygon->y, polygon->colorMask};
        for (size_t x = 0; x < count_x; ++x) {
          for (size_t y = 0; y < count_y; ++y) {
            appendPolygon(clause->polygons, base, static_cast<double>(x) * polygon->xStep, static_cast<double>(y) * polygon->yStep);
          }
        }
        break;
      }
      case lefiGeomPathE:
        if (clause == nullptr) {
          throw std::runtime_error("OBS PATH appears before LAYER");
        }
        appendPath(clause->paths, *geometry.getPath(index), clause->path_width);
        break;
      case lefiGeomPathIterE: {
        if (clause == nullptr) {
          throw std::runtime_error("OBS PATH ITERATE appears before LAYER");
        }
        const auto* path = geometry.getPathIter(index);
        const auto count_x = repeatCount(path->xStart, "PATH ITERATE DO count");
        const auto count_y = repeatCount(path->yStart, "PATH ITERATE BY count");
        const lefiGeomPath base{path->numPoints, path->x, path->y, path->colorMask};
        for (size_t x = 0; x < count_x; ++x) {
          for (size_t y = 0; y < count_y; ++y) {
            appendPath(clause->paths, base, clause->path_width, static_cast<double>(x) * path->xStep, static_cast<double>(y) * path->yStep);
          }
        }
        break;
      }
      case lefiGeomViaE:
        appendVia(names, result.vias, *geometry.getVia(index));
        break;
      case lefiGeomViaIterE: {
        const auto* via = geometry.getViaIter(index);
        const auto count_x = repeatCount(via->xStart, "VIA ITERATE DO count");
        const auto count_y = repeatCount(via->yStart, "VIA ITERATE BY count");
        const lefiGeomVia base{via->name, via->x, via->y, via->topMaskNum, via->cutMaskNum, via->bottomMaskNum};
        for (size_t x = 0; x < count_x; ++x) {
          for (size_t y = 0; y < count_y; ++y) {
            appendVia(names, result.vias, base, static_cast<double>(x) * via->xStep, static_cast<double>(y) * via->yStep);
          }
        }
        break;
      }
      case lefiGeomClassE:
        throw std::runtime_error("PORT-only CLASS appears in OBS geometry");
      case lefiGeomUnknown:
      case lefiGeomEnd:
      default:
        throw std::runtime_error("unsupported LEF OBS geometry item");
    }
  }
  return result;
}

void recordUnsupportedMacroFields(Staging& staging, const lefiMacro& source)
{
  recordDiagnostic(staging, "MACRO FOREIGN", static_cast<size_t>(source.numForeigns()));
  recordDiagnostic(staging, "MACRO PROPERTY", static_cast<size_t>(source.numProperties()));
  recordDiagnostic(staging, "MACRO SITE pattern", static_cast<size_t>(source.numSitePattern()));
  recordDiagnostic(staging, "MACRO GENERATOR/GENERATE", static_cast<size_t>(source.hasGenerator() + source.hasGenerate()));
  recordDiagnostic(staging, "MACRO POWER", static_cast<size_t>(source.hasPower()));
  recordDiagnostic(staging, "MACRO EEQ/LEQ", static_cast<size_t>(source.hasEEQ() + source.hasLEQ()));
  recordDiagnostic(staging, "MACRO SOURCE", static_cast<size_t>(source.hasSource()));
  recordDiagnostic(staging, "MACRO CLOCKTYPE", static_cast<size_t>(source.hasClockType()));
  recordDiagnostic(staging, "MACRO FUNCTION", static_cast<size_t>(source.isBuffer() + source.isInverter()));
  recordDiagnostic(staging, "MACRO FIXEDMASK", static_cast<size_t>(source.isFixedMask()));
}

void recordUnsupportedPinFields(Staging& staging, const lefiPin& source)
{
  recordDiagnostic(staging, "PIN FOREIGN", static_cast<size_t>(source.numForeigns()));
  recordDiagnostic(staging, "PIN PROPERTY", static_cast<size_t>(source.numProperties()));
  recordDiagnostic(staging, "PIN ANTENNAMODEL", static_cast<size_t>(source.numAntennaModel()));
  recordDiagnostic(staging, "PIN ANTENNASIZE", static_cast<size_t>(source.numAntennaSize()));
  recordDiagnostic(staging, "PIN ANTENNAMETALAREA", static_cast<size_t>(source.numAntennaMetalArea()));
  recordDiagnostic(staging, "PIN ANTENNAMETALLENGTH", static_cast<size_t>(source.numAntennaMetalLength()));
  recordDiagnostic(staging, "PIN ANTENNAPARTIALMETALAREA", static_cast<size_t>(source.numAntennaPartialMetalArea()));
  recordDiagnostic(staging, "PIN ANTENNAPARTIALMETALSIDEAREA", static_cast<size_t>(source.numAntennaPartialMetalSideArea()));
  recordDiagnostic(staging, "PIN ANTENNAPARTIALCUTAREA", static_cast<size_t>(source.numAntennaPartialCutArea()));
  recordDiagnostic(staging, "PIN ANTENNADIFFAREA", static_cast<size_t>(source.numAntennaDiffArea()));
  recordDiagnostic(staging, "PIN LEQ/MUSTJOIN", static_cast<size_t>(source.hasLEQ() + source.hasMustjoin()));
  recordDiagnostic(staging, "PIN NETEXPR", static_cast<size_t>(source.hasNetExpr()));
  recordDiagnostic(staging, "PIN SUPPLY/GROUND SENSITIVITY",
                   static_cast<size_t>(source.hasSupplySensitivity() + source.hasGroundSensitivity()));
  const size_t electrical = static_cast<size_t>(
      source.hasOutMargin() + source.hasOutResistance() + source.hasInMargin() + source.hasPower() + source.hasLeakage()
      + source.hasMaxload() + source.hasMaxdelay() + source.hasCapacitance() + source.hasResistance() + source.hasPulldownres()
      + source.hasTieoffr() + source.hasVHI() + source.hasVLO() + source.hasRiseVoltage() + source.hasFallVoltage() + source.hasRiseThresh()
      + source.hasFallThresh() + source.hasRiseSatcur() + source.hasFallSatcur() + source.hasCurrentSource() + source.hasTables()
      + source.hasRiseSlewLimit() + source.hasFallSlewLimit());
  recordDiagnostic(staging, "PIN legacy electrical attribute", electrical);
  recordDiagnostic(staging, "PIN TAPERRULE", static_cast<size_t>(source.hasTaperRule()));
}

template <typename Function>
int guardedCallback(lefiUserData user_data, Function&& function) noexcept
{
  auto* staging = static_cast<Staging*>(user_data);
  if (staging == nullptr) {
    return PARSE_ERROR;
  }
  try {
    function(*staging);
    return PARSE_OK;
  } catch (...) {
    staging->callback_failure = std::current_exception();
    return PARSE_ERROR;
  }
}

template <typename Function>
int timedCallback(lefiUserData user_data, uint64_t LefLibraryImportTiming::*field, Function&& function) noexcept
{
  const auto start = std::chrono::steady_clock::now();
  const int result = guardedCallback(user_data, std::forward<Function>(function));
  auto* staging = static_cast<Staging*>(user_data);
  if (staging != nullptr && staging->direct_timing != nullptr) {
    staging->direct_timing->*field += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
  }
  return result;
}

int unitsCallback(lefrCallbackType_e type, lefiUnits* source, lefiUserData user_data) noexcept
{
  return guardedCallback(user_data, [&](Staging& staging) {
    if (type != lefrUnitsCbkType || source == nullptr) {
      throw std::runtime_error("invalid SI2 UNITS callback");
    }
    if (!source->hasDatabase()) {
      return;
    }
    const double value = source->databaseNumber();
    if (!std::isfinite(value) || value <= 0.0 || value > std::numeric_limits<int32_t>::max() || std::floor(value) != value) {
      throw std::runtime_error("LEF DATABASE MICRONS must be a positive integer");
    }
    const auto units = static_cast<int32_t>(value);
    if (staging.database_units_per_micron && *staging.database_units_per_micron != units) {
      throw std::runtime_error("conflicting LEF DATABASE MICRONS statements");
    }
    staging.database_units_per_micron = units;
  });
}

int siteCallback(lefrCallbackType_e type, lefiSite* source, lefiUserData user_data) noexcept
{
  return timedCallback(user_data, &LefLibraryImportTiming::site_callback_microseconds, [&](Staging& staging) {
    if (type != lefrSiteCbkType || source == nullptr) {
      throw std::runtime_error("invalid SI2 SITE callback");
    }
    StagedSite target{.site = LibrarySite{.name = requiredText(source->name(), "SITE name")}};
    if (!staging.site_names.emplace(target.site.name).second) {
      throw std::runtime_error("duplicate LEF SITE name: " + target.site.name);
    }
    if (source->hasClass()) {
      target.site.site_class = siteClass(source->siteClass());
    }
    target.site.symmetry_x = source->hasXSymmetry();
    target.site.symmetry_y = source->hasYSymmetry();
    target.site.symmetry_r90 = source->has90Symmetry();
    if (source->hasSize()) {
      const auto width = source->sizeX();
      const auto height = source->sizeY();
      if (!std::isfinite(width) || !std::isfinite(height) || width < 0.0 || height < 0.0) {
        throw std::runtime_error("SITE SIZE must be non-negative");
      }
      target.size = std::pair{width, height};
    }
    recordDiagnostic(staging, "SITE ROWPATTERN", static_cast<size_t>(source->numSites()));
    if (directMode(staging)) {
      if (target.size) {
        target.site.width = toDbu(target.size->first, staging.direct_units, "SITE width");
        target.site.height = toDbu(target.size->second, staging.direct_units, "SITE height");
      }
      static_cast<void>(staging.direct_library->siteStorage().createSiteTrusted(std::move(target.site)));
    } else {
      staging.sites.push_back(std::move(target));
    }
  });
}

int macroBeginCallback(lefrCallbackType_e type, const char* name, lefiUserData user_data) noexcept
{
  return timedCallback(user_data, &LefLibraryImportTiming::macro_callback_microseconds, [&](Staging& staging) {
    if (type != lefrMacroBeginCbkType || staging.current_master || staging.direct_master) {
      throw std::runtime_error("invalid SI2 MACRO begin callback");
    }
    const std::string master_name = requiredText(name, "MACRO name");
    if (!staging.master_names.emplace(master_name).second) {
      throw std::runtime_error("duplicate LEF MACRO name: " + master_name);
    }
    if (directMode(staging)) {
      staging.direct_master = staging.direct_library->cellMasterStorage().createCellMasterTrusted(LibraryCellMaster{.name = master_name});
    } else {
      staging.masters.push_back(StagedMaster{.master = LibraryCellMaster{.name = master_name}});
      staging.current_master = staging.masters.size() - 1;
    }
  });
}

StagedMaster& currentMaster(Staging& staging)
{
  if (!staging.current_master || *staging.current_master >= staging.masters.size()) {
    throw std::runtime_error("SI2 MACRO child callback appears outside MACRO");
  }
  return staging.masters[*staging.current_master];
}

void appendStagedTerm(Staging& staging, StagedTerm term)
{
  auto& master = currentMaster(staging);
  const auto existing = std::find_if(master.terms.begin(), master.terms.end(),
                                     [&](const StagedTerm& candidate) { return candidate.term.name == term.term.name; });
  if (existing == master.terms.end()) {
    master.terms.push_back(std::move(term));
    return;
  }

  if (existing->term.direction != term.term.direction || existing->term.use != term.term.use || existing->term.shape != term.term.shape) {
    throw std::runtime_error("duplicate LEF PIN has conflicting attributes: " + master.master.name + "/" + term.term.name);
  }

  existing->ports.reserve(existing->ports.size() + term.ports.size());
  for (auto& port : term.ports) {
    existing->ports.push_back(std::move(port));
  }
  recordDiagnostic(staging, "MACRO duplicate PIN normalized");
}

int macroCallback(lefrCallbackType_e type, lefiMacro* source, lefiUserData user_data) noexcept
{
  return timedCallback(user_data, &LefLibraryImportTiming::macro_callback_microseconds, [&](Staging& staging) {
    if (type != lefrMacroCbkType || source == nullptr) {
      throw std::runtime_error("invalid SI2 MACRO callback");
    }
    const auto macro_name = requiredText(source->name(), "MACRO name");
    if (directMode(staging)) {
      auto master_id = directMaster(staging);
      auto target = staging.direct_library->cellMasterStorage().cellMaster(master_id);
      if (target.name != macro_name) {
        throw std::runtime_error("SI2 MACRO callback name mismatch");
      }
      if (source->hasClass()) {
        target.type = masterType(source->macroClass());
        target.core_filler = target.type == LibraryCellMasterType::kCoreSpacer;
        target.pad_filler = target.type == LibraryCellMasterType::kPadSpacer;
      }
      target.symmetry_x = source->hasXSymmetry();
      target.symmetry_y = source->hasYSymmetry();
      target.symmetry_r90 = source->has90Symmetry();
      if (source->hasOrigin()) {
        target.origin_x = toDbu64(source->originX(), staging.direct_units, "MACRO ORIGIN x");
        target.origin_y = toDbu64(source->originY(), staging.direct_units, "MACRO ORIGIN y");
      }
      if (source->hasSize()) {
        if (source->sizeX() < 0.0 || source->sizeY() < 0.0) {
          throw std::runtime_error("MACRO SIZE must be non-negative");
        }
        target.width = toUnsignedDbu(source->sizeX(), staging.direct_units, "MACRO width");
        target.height = toUnsignedDbu(source->sizeY(), staging.direct_units, "MACRO height");
      }
      if (source->hasSiteName()) {
        const auto site_name = requiredText(source->siteName(), "MACRO SITE");
        const auto site_id = staging.direct_library->siteStorage().findSite(site_name);
        if (!site_id) {
          throw std::runtime_error("MACRO references an unknown SITE: " + std::string(site_name));
        }
        target.site = site_id;
      }
      staging.direct_library->cellMasterStorage().updateCellMasterTrusted(master_id, std::move(target));
      recordUnsupportedMacroFields(staging, *source);
      return;
    }
    auto& target = currentMaster(staging);
    if (target.master.name != macro_name) {
      throw std::runtime_error("SI2 MACRO callback name mismatch");
    }
    if (source->hasClass()) {
      target.master.type = masterType(source->macroClass());
      target.master.core_filler = target.master.type == LibraryCellMasterType::kCoreSpacer;
      target.master.pad_filler = target.master.type == LibraryCellMasterType::kPadSpacer;
    }
    target.master.symmetry_x = source->hasXSymmetry();
    target.master.symmetry_y = source->hasYSymmetry();
    target.master.symmetry_r90 = source->has90Symmetry();
    if (source->hasOrigin()) {
      target.origin = std::pair{source->originX(), source->originY()};
    }
    if (source->hasSize()) {
      if (source->sizeX() < 0.0 || source->sizeY() < 0.0) {
        throw std::runtime_error("MACRO SIZE must be non-negative");
      }
      target.size = std::pair{source->sizeX(), source->sizeY()};
    }
    if (source->hasSiteName()) {
      target.site = requiredText(source->siteName(), "MACRO SITE");
    }
    recordUnsupportedMacroFields(staging, *source);
  });
}

int macroEndCallback(lefrCallbackType_e type, const char* name, lefiUserData user_data) noexcept
{
  return timedCallback(user_data, &LefLibraryImportTiming::macro_callback_microseconds, [&](Staging& staging) {
    if (type != lefrMacroEndCbkType) {
      throw std::runtime_error("invalid SI2 MACRO end callback");
    }
    if (directMode(staging)) {
      const auto master_id = directMaster(staging);
      if (staging.direct_library->cellMasterStorage().cellMaster(master_id).name != requiredText(name, "MACRO END name")) {
        throw std::runtime_error("SI2 MACRO END name mismatch");
      }
      staging.direct_master.reset();
      return;
    }
    auto& target = currentMaster(staging);
    if (target.master.name != requiredText(name, "MACRO END name")) {
      throw std::runtime_error("SI2 MACRO END name mismatch");
    }
    staging.current_master.reset();
  });
}

int pinCallback(lefrCallbackType_e type, lefiPin* source, lefiUserData user_data) noexcept
{
  return timedCallback(user_data, &LefLibraryImportTiming::pin_callback_microseconds, [&](Staging& staging) {
    if (type != lefrPinCbkType || source == nullptr) {
      throw std::runtime_error("invalid SI2 PIN callback");
    }
    LibraryMasterTerm target{.name = requiredText(source->name(), "PIN name")};
    if (source->hasDirection()) {
      target.direction = termDirection(source->direction());
    }
    if (source->hasUse()) {
      target.use = termUse(source->use());
    }
    if (source->hasShape()) {
      target.shape = termShape(source->shape());
    }
    recordUnsupportedPinFields(staging, *source);
    if (directMode(staging)) {
      const auto master_id = directMaster(staging);
      const auto existing = staging.direct_library->masterTermStorage().findMasterTerm(master_id, target.name);
      LibraryMasterTermId term_id;
      if (!existing) {
        term_id = staging.direct_library->masterTermStorage().createMasterTermTrusted(master_id, std::move(target));
      } else {
        const auto& current = staging.direct_library->masterTermStorage().masterTerm(existing);
        if (current.direction != target.direction || current.use != target.use || current.shape != target.shape) {
          throw std::runtime_error("duplicate LEF PIN has conflicting attributes: "
                                   + staging.direct_library->cellMasterStorage().cellMaster(master_id).name + "/" + target.name);
        }
        term_id = existing;
        recordDiagnostic(staging, "MACRO duplicate PIN normalized");
      }
      for (int index = 0; index < source->numPorts(); ++index) {
        const auto* port = source->port(index);
        if (port == nullptr) {
          throw std::runtime_error("SI2 returned a null PORT geometry");
        }
        const auto prepare_start = std::chrono::steady_clock::now();
        auto input = directPort(*port, *staging.direct_technology, staging, staging.direct_units);
        if (staging.direct_timing != nullptr) {
          staging.direct_timing->geometry_prepare_microseconds += static_cast<uint64_t>(
              std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - prepare_start).count());
        }
        const auto start = std::chrono::steady_clock::now();
        static_cast<void>(staging.direct_library->masterPortStorage().createMasterPortTrusted(term_id, std::move(input)));
        if (staging.direct_timing != nullptr) {
          staging.direct_timing->geometry_write_microseconds += static_cast<uint64_t>(
              std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
        }
      }
    } else {
      StagedTerm staged{.term = std::move(target)};
      staged.ports.reserve(static_cast<size_t>(source->numPorts()));
      for (int index = 0; index < source->numPorts(); ++index) {
        const auto* port = source->port(index);
        if (port == nullptr) {
          throw std::runtime_error("SI2 returned a null PORT geometry");
        }
        staged.ports.push_back(stagePort(*port, staging.names));
      }
      appendStagedTerm(staging, std::move(staged));
    }
  });
}

int obstructionCallback(lefrCallbackType_e type, lefiObstruction* source, lefiUserData user_data) noexcept
{
  return timedCallback(user_data, &LefLibraryImportTiming::obstruction_callback_microseconds, [&](Staging& staging) {
    if (type != lefrObstructionCbkType || source == nullptr || source->geometries() == nullptr) {
      throw std::runtime_error("invalid SI2 OBS callback");
    }
    if (directMode(staging)) {
      const auto prepare_start = std::chrono::steady_clock::now();
      auto input = directObs(*source->geometries(), *staging.direct_technology, staging, staging.direct_units);
      if (staging.direct_timing != nullptr) {
        staging.direct_timing->geometry_prepare_microseconds += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - prepare_start).count());
      }
      const auto start = std::chrono::steady_clock::now();
      staging.direct_library->cellMasterStorage().appendObsTrusted(directMaster(staging), std::move(input));
      if (staging.direct_timing != nullptr) {
        staging.direct_timing->geometry_write_microseconds += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
      }
    } else {
      const auto obs = stageObs(*source->geometries(), staging.names);
      currentMaster(staging).obstructions.push_back(obs);
    }
  });
}

int densityCallback(lefrCallbackType_e type, lefiDensity* source, lefiUserData user_data) noexcept
{
  return timedCallback(user_data, &LefLibraryImportTiming::macro_callback_microseconds, [&](Staging& staging) {
    if (type != lefrDensityCbkType || source == nullptr) {
      throw std::runtime_error("invalid SI2 MACRO DENSITY callback");
    }
    if (directMode(staging)) {
      static_cast<void>(directMaster(staging));
    } else {
      static_cast<void>(currentMaster(staging));
    }
    recordDiagnostic(staging, "MACRO DENSITY");
  });
}

int timingCallback(lefrCallbackType_e type, lefiTiming* source, lefiUserData user_data) noexcept
{
  return timedCallback(user_data, &LefLibraryImportTiming::macro_callback_microseconds, [&](Staging& staging) {
    if (type != lefrTimingCbkType || source == nullptr) {
      throw std::runtime_error("invalid SI2 MACRO TIMING callback");
    }
    if (directMode(staging)) {
      static_cast<void>(directMaster(staging));
    } else {
      static_cast<void>(currentMaster(staging));
    }
    recordDiagnostic(staging, "MACRO TIMING");
  });
}

class ParserSession
{
 public:
  ParserSession()
  {
    if (lefrInit() != 0) {
      throw std::runtime_error("failed to initialize SI2 LEF parser session");
    }
    lefrSetUnitsCbk(unitsCallback);
    lefrSetSiteCbk(siteCallback);
    lefrSetMacroBeginCbk(macroBeginCallback);
    lefrSetMacroCbk(macroCallback);
    lefrSetMacroEndCbk(macroEndCallback);
    lefrSetPinCbk(pinCallback);
    lefrSetObstructionCbk(obstructionCallback);
    lefrSetDensityCbk(densityCallback);
    lefrSetTimingCbk(timingCallback);
  }

  ~ParserSession() { static_cast<void>(lefrClear()); }

  ParserSession(const ParserSession&) = delete;
  ParserSession& operator=(const ParserSession&) = delete;
};

struct FileCloser
{
  void operator()(FILE* file) const noexcept
  {
    if (file != nullptr) {
      static_cast<void>(std::fclose(file));
    }
  }
};

void parseFile(const std::filesystem::path& path, Staging& staging)
{
  if (path.empty()) {
    throw std::invalid_argument("LEF path must not be empty");
  }
  ParserSession session;
  std::unique_ptr<FILE, FileCloser> file(std::fopen(path.c_str(), "r"));
  if (!file) {
    throw std::runtime_error("cannot open LEF file: " + path.string());
  }
  staging.callback_failure = nullptr;
  const auto parser_start = std::chrono::steady_clock::now();
  const auto result = lefrRead(file.get(), path.c_str(), &staging);
  if (staging.direct_timing != nullptr) {
    staging.direct_timing->parser_microseconds += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - parser_start).count());
  }
  if (staging.callback_failure) {
    std::rethrow_exception(staging.callback_failure);
  }
  if (result != 0) {
    throw std::runtime_error("SI2 failed to parse LEF file: " + path.string());
  }
  if (staging.current_master || staging.direct_master) {
    throw std::runtime_error("SI2 completed with an unterminated MACRO");
  }
}

int32_t technologyUnits(const TechStore& technology)
{
  if (!technology.globalStorage().hasUnits()) {
    throw std::runtime_error("Library LEF import requires Tech DATABASE MICRONS units");
  }
  const auto& units = technology.globalStorage().getUnits();
  if ((units.flags & TechGlobalUnitsFlag::kHasDatabaseUnitsPerMicron) == 0u || units.database_units_per_micron <= 0) {
    throw std::runtime_error("Library LEF import requires Tech DATABASE MICRONS units");
  }
  return units.database_units_per_micron;
}

int32_t toDbu(double value, int32_t units, const char* field)
{
  const double scaled = value * static_cast<double>(units);
  if (!std::isfinite(value) || !std::isfinite(scaled) || scaled < static_cast<double>(std::numeric_limits<int32_t>::min())
      || scaled > static_cast<double>(std::numeric_limits<int32_t>::max())) {
    throw std::runtime_error(std::string(field) + " is outside int32 DBU range");
  }
  return static_cast<int32_t>(std::llround(scaled));
}

int32_t fastDbu(double value, int32_t units) noexcept
{
  // Same conversion as OpenDB's dbdist(double): no per-coordinate validation.
  return static_cast<int32_t>(std::lround(value * static_cast<double>(units)));
}

int64_t toDbu64(double value, int32_t units, const char* field)
{
  const long double scaled = static_cast<long double>(value) * static_cast<long double>(units);
  if (!std::isfinite(value) || !std::isfinite(scaled) || scaled < static_cast<long double>(std::numeric_limits<int64_t>::min())
      || scaled > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
    throw std::runtime_error(std::string(field) + " is outside int64 DBU range");
  }
  return static_cast<int64_t>(std::llround(scaled));
}

uint32_t toUnsignedDbu(double value, int32_t units, const char* field)
{
  if (!std::isfinite(value) || value < 0.0) {
    throw std::runtime_error(std::string(field) + " must be non-negative");
  }
  const double scaled = value * static_cast<double>(units);
  if (!std::isfinite(scaled) || scaled > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
    throw std::runtime_error(std::string(field) + " is outside uint32 DBU range");
  }
  return static_cast<uint32_t>(std::llround(scaled));
}

Rect prepareRect(const StagedRect& source, int32_t units)
{
  // Sky130 SRAM LEFs use both corner orders. Keep the complete rectangle and
  // normalize it at the LEF boundary before GeometryPool validation.
  const Rect result{.ll_x = toDbu(source.ll_x, units, "geometry x"),
                    .ll_y = toDbu(source.ll_y, units, "geometry y"),
                    .ur_x = toDbu(source.ur_x, units, "geometry x"),
                    .ur_y = toDbu(source.ur_y, units, "geometry y")};
  return result.normalized();
}

GeometryPolygonInput preparePolygon(const StagedPolygon& source, int32_t units)
{
  if (source.points.size() < 3u) {
    throw std::runtime_error("POLYGON requires at least three points");
  }
  GeometryPolygonInput result;
  result.points.reserve(source.points.size());
  for (const auto [x, y] : source.points) {
    result.points.push_back(Point{.x = toDbu(x, units, "POLYGON x"), .y = toDbu(y, units, "POLYGON y")});
  }
  return result;
}

int32_t checkedPathCoordinate(int64_t value)
{
  if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()) {
    throw std::runtime_error("PATH rectangle is outside int32 DBU range");
  }
  return static_cast<int32_t>(value);
}

int32_t pathWidth(const StagedPath& source, TechLayerId layer, const TechStore& technology, int32_t units)
{
  if (source.width > 0.0) {
    const auto width = toDbu(source.width, units, "PATH WIDTH");
    if (width <= 0) {
      throw std::runtime_error("PATH WIDTH rounds to zero DBU");
    }
    return width;
  }

  const auto& registry = technology.techRegistry().registry();
  if (!registry.all_of<TechRoutingLayer>(layer.entity())) {
    throw std::runtime_error("PATH without WIDTH requires a ROUTING layer");
  }
  const auto& routing = registry.get<const TechRoutingLayer>(layer.entity());
  if ((routing.flags & TechRoutingLayerFlag::kHasWidth) == 0u || routing.width <= 0) {
    throw std::runtime_error("PATH without WIDTH requires a default ROUTING layer WIDTH");
  }
  return routing.width;
}

void appendPreparedPath(std::vector<Rect>& target, const StagedPath& source, TechLayerId layer, const TechStore& technology,
                        int32_t units)
{
  const int64_t width = pathWidth(source, layer, technology, units);
  const int64_t low_half = width / 2;
  const int64_t high_half = width - low_half;
  if (source.points.size() == 1) {
    const int64_t x = toDbu(source.points.front().first, units, "PATH x");
    const int64_t y = toDbu(source.points.front().second, units, "PATH y");
    target.push_back(Rect{checkedPathCoordinate(x - low_half), checkedPathCoordinate(y - low_half), checkedPathCoordinate(x + high_half),
                          checkedPathCoordinate(y + high_half)});
    return;
  }
  for (size_t index = 1; index < source.points.size(); ++index) {
    const int64_t x1 = toDbu(source.points[index - 1].first, units, "PATH x");
    const int64_t y1 = toDbu(source.points[index - 1].second, units, "PATH y");
    const int64_t x2 = toDbu(source.points[index].first, units, "PATH x");
    const int64_t y2 = toDbu(source.points[index].second, units, "PATH y");
    Rect rect;
    if (x1 == x2) {
      rect = Rect{checkedPathCoordinate(x1 - low_half), checkedPathCoordinate(std::min(y1, y2) - low_half),
                  checkedPathCoordinate(x1 + high_half), checkedPathCoordinate(std::max(y1, y2) + high_half)};
    } else if (y1 == y2) {
      rect = Rect{checkedPathCoordinate(std::min(x1, x2) - low_half), checkedPathCoordinate(y1 - low_half),
                  checkedPathCoordinate(std::max(x1, x2) + high_half), checkedPathCoordinate(y1 + high_half)};
    } else {
      throw std::runtime_error("PATH ceases to be Manhattan after DBU conversion");
    }
    target.push_back(rect);
  }
}

LibraryViaPlacement prepareVia(const StagedViaPlacement& source, const LefNamePool& names, const TechStore& technology, int32_t units)
{
  const auto via_name = names.get(source.via);
  const auto via = technology.viaMasterStorage().findViaMaster(via_name);
  if (!via) {
    throw std::runtime_error("Library geometry references an unknown Tech VIA: " + std::string(via_name));
  }
  return LibraryViaPlacement{.via = via, .origin = Point{toDbu(source.x, units, "VIA x"), toDbu(source.y, units, "VIA y")}};
}

std::vector<LibrarySite> prepareSites(const Staging& staging, int32_t units)
{
  std::vector<LibrarySite> result;
  result.reserve(staging.sites.size());
  for (const auto& source : staging.sites) {
    auto site = source.site;
    if (source.size) {
      site.width = toDbu(source.size->first, units, "SITE width");
      site.height = toDbu(source.size->second, units, "SITE height");
    }
    result.push_back(std::move(site));
  }
  return result;
}

std::vector<PreparedMaster> prepareMasters(const Staging& staging, const TechStore& technology, int32_t units)
{
  std::vector<PreparedMaster> result;
  result.reserve(staging.masters.size());
  for (const auto& source : staging.masters) {
    if (source.site && !staging.site_names.contains(*source.site)) {
      throw std::runtime_error("MACRO references an unknown SITE: " + *source.site);
    }
    PreparedMaster target{.master = source.master, .site = source.site};
    if (source.origin) {
      target.master.origin_x = toDbu64(source.origin->first, units, "MACRO ORIGIN x");
      target.master.origin_y = toDbu64(source.origin->second, units, "MACRO ORIGIN y");
    }
    if (source.size) {
      target.master.width = toUnsignedDbu(source.size->first, units, "MACRO width");
      target.master.height = toUnsignedDbu(source.size->second, units, "MACRO height");
    }
    target.terms.reserve(source.terms.size());
    for (const auto& source_term : source.terms) {
      std::vector<LibraryMasterPortInput> ports;
      ports.reserve(source_term.ports.size());
      for (const auto& source_port : source_term.ports) {
        LibraryMasterPortInput port{.port_class = source_port.port_class};
        port.layer_clauses.reserve(source_port.layer_clauses.size());
        for (const auto& source_clause : source_port.layer_clauses) {
          const auto layer_name = staging.names.get(source_clause.layer);
          const auto layer = technology.findLayer(layer_name);
          if (!layer) {
            throw std::runtime_error("PORT geometry references an unknown Tech LAYER: " + std::string(layer_name));
          }
          LibraryPortLayerGeometryInput clause{.layer = layer};
          clause.geometry.rects.reserve(source_clause.rects.size() + source_clause.paths.size());
          for (const auto& source_rect : source_clause.rects) {
            clause.geometry.rects.push_back(prepareRect(source_rect, units));
          }
          clause.geometry.polygons.reserve(source_clause.polygons.size());
          for (const auto& source_polygon : source_clause.polygons) {
            clause.geometry.polygons.push_back(preparePolygon(source_polygon, units));
          }
          for (const auto& source_path : source_clause.paths) {
            appendPreparedPath(clause.geometry.rects, source_path, layer, technology, units);
          }
          if (!clause.geometry.empty()) {
            port.layer_clauses.push_back(std::move(clause));
          }
        }
        port.vias.reserve(source_port.vias.size());
        for (const auto& source_via : source_port.vias) {
          port.vias.push_back(prepareVia(source_via, staging.names, technology, units));
        }
        ports.push_back(std::move(port));
      }
      target.terms.emplace_back(source_term.term, std::move(ports));
    }
    for (const auto& source_obs : source.obstructions) {
      for (const auto& source_clause : source_obs.clauses) {
        const auto layer_name = staging.names.get(source_clause.layer);
        const auto layer = technology.findLayer(layer_name);
        if (!layer) {
          throw std::runtime_error("OBS geometry references an unknown Tech LAYER: " + std::string(layer_name));
        }
        LibraryObsLayerClauseInput clause{.layer = layer, .flags = source_clause.flags};
        if ((clause.flags & LibraryObsLayerFlag::kHasSpacing) != 0u) {
          clause.spacing = toDbu(source_clause.spacing, units, "OBS SPACING");
        }
        if ((clause.flags & LibraryObsLayerFlag::kHasDesignRuleWidth) != 0u) {
          clause.design_rule_width = toDbu(source_clause.design_rule_width, units, "OBS DESIGNRULEWIDTH");
        }
        if ((clause.flags & LibraryObsLayerFlag::kHasPathWidth) != 0u) {
          clause.path_width = toDbu(source_clause.path_width, units, "OBS WIDTH");
        }
        clause.geometry.rects.reserve(source_clause.rects.size() + source_clause.paths.size());
        for (const auto& source_rect : source_clause.rects) {
          clause.geometry.rects.push_back(prepareRect(source_rect, units));
        }
        clause.geometry.polygons.reserve(source_clause.polygons.size());
        for (const auto& source_polygon : source_clause.polygons) {
          clause.geometry.polygons.push_back(preparePolygon(source_polygon, units));
        }
        for (const auto& source_path : source_clause.paths) {
          appendPreparedPath(clause.geometry.rects, source_path, layer, technology, units);
        }
        if (!clause.geometry.empty()) {
          target.obs.layer_clauses.push_back(std::move(clause));
        }
      }
      for (const auto& source_via : source_obs.vias) {
        target.obs.vias.push_back(prepareVia(source_via, staging.names, technology, units));
      }
    }
    result.push_back(std::move(target));
  }
  return result;
}

void ensureEmptyTarget(const LibraryStore& library)
{
  const auto* entities = library.libraryRegistry().registry().storage<LibraryEntity>();
  if (entities != nullptr && entities->free_list() != 0u) {
    throw std::logic_error("direct LEF library importer requires a new empty LibraryStore");
  }
  if (library.geometryPool().rectangleCount() != 0u || library.geometryPool().polygonCount() != 0u
      || library.geometryPool().pointCount() != 0u) {
    throw std::logic_error("direct LEF library importer requires an empty GeometryPool");
  }
}

void rollbackImport(LibraryStore& library, GeometryPoolCheckpoint geometry_checkpoint) noexcept
{
  auto& registry = library.libraryRegistry().registry();
  std::vector<LibraryEntity> entities;
  for (const auto [entity] : registry.storage<LibraryEntity>().each()) {
    entities.push_back(entity);
  }
  for (const auto entity : entities) {
    registry.destroy(entity);
  }
  try {
    library.geometryPool().rollback(geometry_checkpoint);
  } catch (...) {
  }
}

void commitImport(LibraryStore& library, std::vector<LibrarySite> sites, std::vector<PreparedMaster> masters)
{
  std::unordered_map<std::string, LibrarySiteId> site_ids;
  const auto geometry_checkpoint = library.geometryPool().checkpoint();
  try {
    for (auto& site : sites) {
      const auto name = site.name;
      site_ids.emplace(name, library.siteStorage().createSite(std::move(site)));
    }
    for (auto& source : masters) {
      if (source.site) {
        const auto found = site_ids.find(*source.site);
        if (found == site_ids.end()) {
          throw std::runtime_error("MACRO references an unknown SITE: " + *source.site);
        }
        source.master.site = found->second;
      }
      const auto master = library.cellMasterStorage().createCellMaster(std::move(source.master));
      for (auto& [term_component, ports] : source.terms) {
        const auto term = library.masterTermStorage().createMasterTerm(master, std::move(term_component));
        for (auto& port : ports) {
          static_cast<void>(library.masterPortStorage().createMasterPort(term, std::move(port)));
        }
      }
      if (!source.obs.layer_clauses.empty() || !source.obs.vias.empty()) {
        library.cellMasterStorage().setObs(master, std::move(source.obs));
      }
    }
  } catch (...) {
    rollbackImport(library, geometry_checkpoint);
    throw;
  }
}

std::vector<LefLibraryImportDiagnostic> buildDiagnostics(const Staging& staging)
{
  std::vector<LefLibraryImportDiagnostic> result;
  result.reserve(staging.diagnostics.size());
  for (const auto& [statement, count] : staging.diagnostics) {
    result.push_back(LefLibraryImportDiagnostic{.statement = statement, .occurrence_count = count});
  }
  std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) { return lhs.statement < rhs.statement; });
  return result;
}

}  // namespace

void LefLibraryImporter::import(const std::filesystem::path& file)
{
  const std::array files{file};
  import(std::span<const std::filesystem::path>(files));
}

void LefLibraryImporter::import(std::span<const std::filesystem::path> files)
{
  if (_used) {
    throw std::logic_error("direct LEF library importer is one-shot");
  }
  _used = true;
  if (files.empty()) {
    throw std::invalid_argument("direct LEF library importer requires at least one file");
  }
  ensureEmptyTarget(_library);
  const auto units = technologyUnits(_technology);

  Staging staging;
  staging.direct_technology = &_technology;
  staging.direct_library = &_library;
  staging.direct_units = units;
  staging.direct_timing = &_timing;
  const auto& tech_registry = _technology.techRegistry().registry();
  const auto layer_view = tech_registry.view<const TechLayerInfo>();
  staging.direct_layers.reserve(_technology.layerSequence().size());
  for (const auto entity : layer_view) {
    const auto& info = layer_view.get<const TechLayerInfo>(entity);
    staging.direct_layers.emplace(info.name, TechLayerId{entity});
  }
  const auto via_view = tech_registry.view<const TechViaMaster, const TechViaGeometry>();
  staging.direct_vias.reserve(_technology.viaMasterStorage().viaMasters().size());
  for (const auto entity : via_view) {
    if (!tech_registry.all_of<TechNdrViaDefinition>(entity)) {
      staging.direct_vias.emplace(via_view.get<const TechViaMaster>(entity).name, TechViaMasterId{entity});
    }
  }
  _timing = {};
  const auto geometry_checkpoint = _library.geometryPool().checkpoint();
  {
    try {
      const std::scoped_lock lock(lef_detail::parserMutex());
      for (const auto& file : files) {
        parseFile(file, staging);
      }
      if (staging.database_units_per_micron && *staging.database_units_per_micron != units) {
        throw std::runtime_error("Library LEF DATABASE MICRONS conflicts with the Tech database");
      }
    } catch (...) {
      rollbackImport(_library, geometry_checkpoint);
      throw;
    }
  }
  _diagnostics = buildDiagnostics(staging);
}

}  // namespace eccdb

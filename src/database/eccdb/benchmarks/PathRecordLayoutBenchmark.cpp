#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if defined(ECCDB_PATH_RECORD_INLINE_TWO)
#include <boost/container/small_vector.hpp>
#endif

#include "def/DefDesignImporter.h"
#include "design/DesignStore.h"
#include "design/routing/component/WireRouting.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

using Clock = std::chrono::steady_clock;

enum class RecordKind : uint8_t
{
  kPoint,
  kExtension,
  kTechVia,
  kDesignVia,
  kRectangleMin,
  kRectangleMax
};

struct RouteRecord
{
  uint64_t data = 0;
  uint32_t aux = 0;
  RecordKind kind = RecordKind::kPoint;
};
static_assert(sizeof(RouteRecord) == 16);

#if defined(ECCDB_PATH_RECORD_INLINE_TWO)
using RecordList = boost::container::small_vector<RouteRecord, 2>;
#else
using RecordList = std::vector<RouteRecord>;
#endif

struct RouteExtra
{
  uint32_t record_index = 0;
  std::variant<DesignWirePathExtra, DesignWireVia> value;
};

struct RoutePath
{
  TechRoutingLayerId layer;
  uint32_t flags = 0;
  RecordList records;
  std::vector<RouteExtra> extras;
};

struct RouteWire
{
  std::vector<RoutePath> paths;
};

struct GeometrySummary
{
  uint64_t paths = 0;
  uint64_t points = 0;
  uint64_t vias = 0;
  uint64_t rectangles = 0;
  uint64_t fingerprint = 1469598103934665603ull;

  bool operator==(const GeometrySummary&) const = default;
};

struct LayoutSize
{
  uint64_t capacity_bytes = 0;
  uint64_t buffers = 0;
};

uint64_t packPoint(Point point)
{
  return (static_cast<uint64_t>(std::bit_cast<uint32_t>(point.x)) << 32u)
         | std::bit_cast<uint32_t>(point.y);
}

Point unpackPoint(uint64_t value)
{
  return {std::bit_cast<int32_t>(static_cast<uint32_t>(value >> 32u)),
          std::bit_cast<int32_t>(static_cast<uint32_t>(value))};
}

void mix(GeometrySummary& summary, uint64_t value)
{
  summary.fingerprint = (summary.fingerprint ^ value) * 1099511628211ull;
}

void mixText(GeometrySummary& summary, std::string_view value)
{
  mix(summary, value.size());
  for (const unsigned char character : value) {
    mix(summary, character);
  }
}

void mixPathExtra(GeometrySummary& summary, int32_t width, uint32_t mask,
                  std::string_view taper_rule, std::string_view shape, int32_t style)
{
  mix(summary, std::bit_cast<uint32_t>(width));
  mix(summary, mask);
  mixText(summary, taper_rule);
  mixText(summary, shape);
  mix(summary, std::bit_cast<uint32_t>(style));
}

void mixPathExtra(GeometrySummary& summary, const DesignWirePathExtra& extra)
{
  mixPathExtra(summary, extra.width, extra.mask, extra.taper_rule, extra.shape, extra.style);
}

void mixViaOptions(GeometrySummary& summary, const DesignWireVia& via)
{
  mix(summary, static_cast<uint8_t>(via.orientation));
  mix(summary, via.flags);
  mix(summary, via.top_mask);
  mix(summary, via.cut_mask);
  mix(summary, via.bottom_mask);
  mix(summary, via.rows);
  mix(summary, via.columns);
  mix(summary, std::bit_cast<uint32_t>(via.step_x));
  mix(summary, std::bit_cast<uint32_t>(via.step_y));
}

bool hasViaOptions(const DesignWireVia& via)
{
  return via.orientation != DesignOrientation::kN || via.flags != 0u || via.top_mask != 0u
         || via.cut_mask != 0u || via.bottom_mask != 0u || via.rows != 1u || via.columns != 1u
         || via.step_x != 0 || via.step_y != 0;
}

bool hasPathExtra(const DesignRouteShapeAttributes* extra)
{
  return extra != nullptr && (extra->width != 0 || extra->mask != 0u || !extra->taper_rule.empty()
                              || !extra->shape.empty() || extra->style != 0);
}

void appendPathHeader(GeometrySummary& summary, TechRoutingLayerId layer, uint32_t flags)
{
  ++summary.paths;
  mix(summary, 0x50415448u);
  mix(summary, layer.packed());
  mix(summary, flags);
}

void appendPoint(GeometrySummary& summary, Point position, uint32_t flags, int32_t extension)
{
  ++summary.points;
  mix(summary, 0x504F494Eu);
  mix(summary, packPoint(position));
  mix(summary, flags);
  if ((flags & DesignWirePointFlag::kHasExtension) != 0u) {
    mix(summary, std::bit_cast<uint32_t>(extension));
  }
}

void appendVia(GeometrySummary& summary, uint64_t id, bool design_via, uint32_t point_index,
               const DesignWireVia* options)
{
  ++summary.vias;
  mix(summary, design_via ? 0x44564941u : 0x54564941u);
  mix(summary, id);
  mix(summary, point_index);
  if (options != nullptr) {
    mixViaOptions(summary, *options);
  }
}

void appendRectangle(GeometrySummary& summary, uint32_t point_index, Rect delta)
{
  ++summary.rectangles;
  mix(summary, 0x52454354u);
  mix(summary, point_index);
  mix(summary, packPoint({delta.ll_x, delta.ll_y}));
  mix(summary, packPoint({delta.ur_x, delta.ur_y}));
}

void appendShapeSummary(GeometrySummary& summary, const DesignRouteShape& shape,
                        const DesignRouteShapeAttributes* attributes)
{
  appendPathHeader(summary, shape.layer, shape.flags);
  if (hasPathExtra(attributes)) {
    mixPathExtra(summary, *attributes);
  }
  std::visit(
      [&](const auto& geometry) {
        using Geometry = std::remove_cvref_t<decltype(geometry)>;
        if constexpr (std::is_same_v<Geometry, DesignRouteSegment>) {
          appendPoint(summary, geometry.begin.position, geometry.begin.flags, geometry.begin.extension);
          appendPoint(summary, geometry.end.position, geometry.end.flags, geometry.end.extension);
        } else if constexpr (std::is_same_v<Geometry, DesignRouteVia>) {
          appendPoint(summary, geometry.origin.position, geometry.origin.flags, geometry.origin.extension);
          appendVia(summary, geometry.via.tech_via ? geometry.via.tech_via.packed() : geometry.via.design_via.packed(),
                    static_cast<bool>(geometry.via.design_via), 0u,
                    hasViaOptions(geometry.via) ? &geometry.via : nullptr);
        } else if constexpr (std::is_same_v<Geometry, DesignRoutePatch>) {
          appendPoint(summary, geometry.anchor.position, geometry.anchor.flags, geometry.anchor.extension);
          appendRectangle(summary, 0u, geometry.rectangle);
        } else {
          appendPoint(summary, geometry.point.position, geometry.point.flags, geometry.point.extension);
        }
      },
      shape.geometry);
}

GeometrySummary summarizeShapes(const DesignRoutingStorage& storage, const std::vector<DesignWireId>& ids)
{
  GeometrySummary summary;
  for (const auto wire : ids) {
      storage.forEachShape(wire, [&](DesignRouteShapeId id, const DesignRouteShape& shape,
                                   const DesignRouteShapeAttributes* attributes) {
      static_cast<void>(id);
      appendShapeSummary(summary, shape, attributes);
    });
  }
  return summary;
}

RoutePath makeShapePath(const DesignRouteShape& shape, const DesignRouteShapeAttributes* attributes)
{
  RoutePath result{.layer = shape.layer, .flags = shape.flags};
  if (hasPathExtra(attributes)) result.extras.push_back({std::numeric_limits<uint32_t>::max(), *attributes});
  std::visit(
      [&](const auto& geometry) {
        using Geometry = std::remove_cvref_t<decltype(geometry)>;
        const auto append_point = [&](DesignWirePoint point) {
          result.records.push_back({packPoint(point.position), point.flags, RecordKind::kPoint});
          if ((point.flags & DesignWirePointFlag::kHasExtension) != 0u) {
            result.records.push_back({std::bit_cast<uint32_t>(point.extension), 0u, RecordKind::kExtension});
          }
        };
        if constexpr (std::is_same_v<Geometry, DesignRouteSegment>) {
          append_point(geometry.begin);
          append_point(geometry.end);
        } else if constexpr (std::is_same_v<Geometry, DesignRouteVia>) {
          append_point(geometry.origin);
          const auto record_index = static_cast<uint32_t>(result.records.size());
          const auto& via = geometry.via;
          result.records.push_back({via.tech_via ? via.tech_via.packed() : via.design_via.packed(), 0u,
                                    via.tech_via ? RecordKind::kTechVia : RecordKind::kDesignVia});
          if (hasViaOptions(via)) result.extras.push_back({record_index, via});
        } else if constexpr (std::is_same_v<Geometry, DesignRoutePatch>) {
          append_point(geometry.anchor);
          result.records.push_back({packPoint({geometry.rectangle.ll_x, geometry.rectangle.ll_y}), 0u,
                                    RecordKind::kRectangleMin});
          result.records.push_back({packPoint({geometry.rectangle.ur_x, geometry.rectangle.ur_y}), 0u,
                                    RecordKind::kRectangleMax});
        } else {
          append_point(geometry.point);
        }
      },
      shape.geometry);
  return result;
}

GeometrySummary summarizeCandidate(const std::vector<RouteWire>& wires)
{
  GeometrySummary summary;
  for (const auto& wire : wires) {
    for (const auto& path : wire.paths) {
      appendPathHeader(summary, path.layer, path.flags);
      std::size_t extra_index = 0;
      if (!path.extras.empty() && path.extras.front().record_index == std::numeric_limits<uint32_t>::max()) {
        mixPathExtra(summary, std::get<DesignWirePathExtra>(path.extras.front().value));
        ++extra_index;
      }
      for (std::size_t index = 0; index < path.records.size(); ++index) {
        const auto& record = path.records[index];
        switch (record.kind) {
          case RecordKind::kPoint: {
            int32_t extension = 0;
            if ((record.aux & DesignWirePointFlag::kHasExtension) != 0u) {
              extension = std::bit_cast<int32_t>(static_cast<uint32_t>(path.records.at(++index).data));
            }
            appendPoint(summary, unpackPoint(record.data), record.aux, extension);
            break;
          }
          case RecordKind::kTechVia:
          case RecordKind::kDesignVia: {
            const DesignWireVia* options = nullptr;
            if (extra_index < path.extras.size() && path.extras[extra_index].record_index == index) {
              options = &std::get<DesignWireVia>(path.extras[extra_index++].value);
            }
            appendVia(summary, record.data, record.kind == RecordKind::kDesignVia, record.aux, options);
            break;
          }
          case RecordKind::kRectangleMin: {
            const auto lower = unpackPoint(record.data);
            const auto upper = unpackPoint(path.records.at(++index).data);
            appendRectangle(summary, record.aux, {lower.x, lower.y, upper.x, upper.y});
            break;
          }
          case RecordKind::kExtension:
          case RecordKind::kRectangleMax:
            throw std::logic_error("orphaned route record");
        }
      }
      if (extra_index != path.extras.size()) {
        throw std::logic_error("orphaned optional route data");
      }
    }
  }
  return summary;
}

LayoutSize candidateSize(const std::vector<RouteWire>& wires)
{
  LayoutSize result{.capacity_bytes = wires.size() * sizeof(RouteWire)};
  for (const auto& wire : wires) {
    result.capacity_bytes += wire.paths.capacity() * sizeof(RoutePath);
    result.buffers += !wire.paths.empty();
    for (const auto& path : wire.paths) {
      const auto begin = reinterpret_cast<uintptr_t>(&path);
      const auto data = reinterpret_cast<uintptr_t>(path.records.data());
      const bool inline_records = data >= begin && data < begin + sizeof(path);
      if (!inline_records) {
        result.capacity_bytes += path.records.capacity() * sizeof(RouteRecord);
        result.buffers += !path.records.empty();
      }
      result.capacity_bytes += path.extras.capacity() * sizeof(RouteExtra);
      result.buffers += !path.extras.empty();
      for (const auto& extra : path.extras) {
        if (const auto* value = std::get_if<DesignWirePathExtra>(&extra.value)) {
          const auto dynamicBytes = [](const std::string& text) {
            const auto pointer = reinterpret_cast<uintptr_t>(text.data());
            const auto object = reinterpret_cast<uintptr_t>(&text);
            return pointer >= object && pointer < object + sizeof(text) ? 0u : text.capacity() + 1u;
          };
          result.capacity_bytes += dynamicBytes(value->shape) + dynamicBytes(value->taper_rule);
        }
      }
    }
  }
  return result;
}

LayoutSize currentSize(const DesignRoutingStorage& storage, const std::vector<DesignWireId>& ids)
{
  LayoutSize result{.capacity_bytes = ids.size() * sizeof(DesignWire)};
  const auto addVector = [&result](const auto& values) {
    result.capacity_bytes += values.capacity() * sizeof(typename std::decay_t<decltype(values)>::value_type);
    result.buffers += !values.empty();
  };
  for (const auto id : ids) {
    addVector(storage.wire(id).shapes);
    storage.forEachShape(id, [&](DesignRouteShapeId, const DesignRouteShape&, const DesignRouteShapeAttributes* extra) {
      result.capacity_bytes += sizeof(DesignRouteShape);
      if (extra == nullptr) return;
      result.capacity_bytes += sizeof(DesignRouteShapeAttributes);
      const auto dynamicBytes = [](const std::string& text) {
        const auto pointer = reinterpret_cast<uintptr_t>(text.data());
        const auto object = reinterpret_cast<uintptr_t>(&text);
        return pointer >= object && pointer < object + sizeof(text) ? 0u : text.capacity() + 1u;
      };
      result.capacity_bytes += dynamicBytes(extra->taper_rule) + dynamicBytes(extra->shape);
    });
  }
  return result;
}

template <typename Function>
double elapsedMs(Function&& function)
{
  const auto begin = Clock::now();
  function();
  return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

std::size_t percentile(const std::vector<std::size_t>& sorted, std::size_t percent)
{
  return sorted.empty() ? 0u : sorted[(sorted.size() - 1u) * percent / 100u];
}

}  // namespace
}  // namespace eccdb

int main(int argc, char** argv)
{
  using namespace eccdb;
  try {
    if (argc < 3 || argc > 5) {
      std::cerr << "usage: eccdb_path_record_benchmark DESIGN.lef ROUTED.def [stride] [regular-only]\n";
      return 2;
    }
    const auto stride = argc >= 4 ? std::stoul(argv[3]) : 1ul;
    if (stride == 0u || (argc == 5 && std::string_view(argv[4]) != "regular-only")) {
      throw std::invalid_argument("stride must be positive; optional filter is regular-only");
    }
    TechStore technology;
    LefTechImporter(technology).import(argv[1]);
    LibraryStore library(technology.techRegistry());
    LefLibraryImporter(technology, library).import(argv[1]);
    DesignStore design(technology.techRegistry(), library.libraryRegistry());
    const double load_ms = elapsedMs([&] { DefDesignImporter(design).import(argv[2]); });
    auto& storage = design.routingStorage();
    const auto all_ids = storage.wires();
    std::vector<DesignWireId> ids;
    std::size_t eligible = 0;
    for (const auto id : all_ids) {
      if (argc == 5 && design.netlistStorage().isSpecialNet(storage.wire(id).net)) continue;
      if (eligible++ % stride == 0u) ids.push_back(id);
    }
    std::vector<RouteWire> candidate;
    candidate.reserve(ids.size());
    std::vector<std::size_t> primitive_lengths;
    std::vector<std::size_t> record_lengths;
    const double build_ms = elapsedMs([&] {
      for (const auto id : ids) {
        RouteWire wire;
        wire.paths.reserve(storage.wire(id).shapes.size());
        storage.forEachShape(id, [&](DesignRouteShapeId, const DesignRouteShape& shape,
                                     const DesignRouteShapeAttributes* attributes) {
          primitive_lengths.push_back(1u);
          wire.paths.push_back(makeShapePath(shape, attributes));
          record_lengths.push_back(wire.paths.back().records.size());
        });
        candidate.push_back(std::move(wire));
      }
    });
    const auto expected = summarizeShapes(storage, ids);
    const auto actual = summarizeCandidate(candidate);
    if (expected != actual) {
      throw std::runtime_error("candidate route geometry differs from the source");
    }

    volatile uint64_t sink = 0;
    const double shape_walk_ms = elapsedMs([&] {
      for (int repeat = 0; repeat < 3; ++repeat) sink = sink + summarizeShapes(storage, ids).fingerprint;
    }) / 3.0;
    const double candidate_walk_ms = elapsedMs([&] {
      for (int repeat = 0; repeat < 3; ++repeat) sink = sink + summarizeCandidate(candidate).fingerprint;
    }) / 3.0;

    auto largest = std::max_element(candidate.begin(), candidate.end(),
                                    [](const RouteWire& lhs, const RouteWire& rhs) {
                                      return lhs.paths.size() < rhs.paths.size();
                                    });
    double shape_update_ms = 0.0;
    double candidate_remove_ms = 0.0;
    std::size_t edited_shapes = 0;
    if (largest != candidate.end() && !largest->paths.empty()) {
      const auto wire_index = static_cast<std::size_t>(largest - candidate.begin());
      edited_shapes = largest->paths.size();
      const int repeats = edited_shapes > 1000u ? 10 : 100;
      const auto shape_ids = storage.wire(ids[wire_index]).shapes;
      if (!shape_ids.empty()) {
        const auto shape_id = shape_ids.front();
        const auto original = storage.shape(shape_id);
        auto edited = original;
        if (auto* segment = std::get_if<DesignRouteSegment>(&edited.geometry)) {
          ++segment->end.position.x;
        }
        shape_update_ms = elapsedMs([&] {
          for (int repeat = 0; repeat < repeats; ++repeat) {
            storage.updateShape(shape_id, edited);
            storage.updateShape(shape_id, original);
          }
        }) / (2.0 * repeats);
      }
      auto editable = candidate[wire_index];
      candidate_remove_ms = elapsedMs([&] {
        for (int repeat = 0; repeat < repeats; ++repeat) {
          auto removed = std::move(editable.paths.front());
          editable.paths.erase(editable.paths.begin());
          editable.paths.insert(editable.paths.begin(), std::move(removed));
          sink = sink + editable.paths.size();
        }
      }) / repeats;
    }

    std::sort(primitive_lengths.begin(), primitive_lengths.end());
    std::sort(record_lengths.begin(), record_lengths.end());
    const auto short_paths = std::upper_bound(primitive_lengths.begin(), primitive_lengths.end(), 4u)
                             - primitive_lengths.begin();
    const auto current_size = currentSize(storage, ids);
    const auto candidate_size = candidateSize(candidate);
#if defined(ECCDB_PATH_RECORD_INLINE_TWO)
    std::cout << "layout=inline_two ";
#else
    std::cout << "layout=vector ";
#endif
    std::cout << "eligible_wires=" << eligible << " sampled_wires=" << ids.size()
              << " shapes=" << expected.paths << " points=" << expected.points
              << " vias=" << expected.vias << " rectangles=" << expected.rectangles << '\n';
    std::cout << "path_primitives_p50=" << percentile(primitive_lengths, 50)
              << " p90=" << percentile(primitive_lengths, 90)
              << " p99=" << percentile(primitive_lengths, 99)
              << " max=" << percentile(primitive_lengths, 100)
              << " paths_le_4=" << short_paths << '\n';
    std::cout << "path_records_p50=" << percentile(record_lengths, 50)
              << " p90=" << percentile(record_lengths, 90)
              << " p99=" << percentile(record_lengths, 99)
              << " max=" << percentile(record_lengths, 100) << '\n';
    std::cout << "current_capacity_bytes=" << current_size.capacity_bytes
              << " current_buffers=" << current_size.buffers
              << " candidate_capacity_bytes=" << candidate_size.capacity_bytes
              << " candidate_buffers=" << candidate_size.buffers << '\n';
    std::cout << "def_load_ms=" << load_ms << " candidate_build_ms=" << build_ms
              << " shape_walk_ms=" << shape_walk_ms
              << " candidate_walk_ms=" << candidate_walk_ms << '\n';
    std::cout << "edit_wire_shapes=" << edited_shapes << " shape_update_ms=" << shape_update_ms
              << " candidate_erase_restore_ms=" << candidate_remove_ms << " sink=" << sink << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

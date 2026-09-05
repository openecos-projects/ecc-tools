#include "design/routing/pool/DesignRoutingPool.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <malloc.h>
#include <stdexcept>
#include <utility>

namespace eccdb {
namespace {

uint32_t checkedPoolEnd(std::size_t value)
{
  if (value > std::numeric_limits<uint32_t>::max()) {
    throw std::overflow_error("design routing pool exceeds uint32_t");
  }
  return static_cast<uint32_t>(value);
}

bool needsPointExtra(const DesignWirePoint& point)
{
  return point.flags != 0u || point.extension != 0;
}

bool needsViaExtra(const DesignWireVia& via)
{
  return via.flags != 0u || via.top_mask != 0u || via.cut_mask != 0u || via.bottom_mask != 0u || via.rows != 1u
         || via.columns != 1u || via.step_x != 0 || via.step_y != 0;
}

std::size_t dynamicStringCapacityBytes(const std::string& value) noexcept
{
  const auto data = reinterpret_cast<uintptr_t>(value.data());
  const auto object = reinterpret_cast<uintptr_t>(&value);
  return data >= object && data < object + sizeof(value) ? 0u : value.capacity() + 1u;
}

template <typename T>
DesignRoutingPoolArrayStatistics arrayStatistics(const std::vector<T>& values) noexcept
{
  return DesignRoutingPoolArrayStatistics{.count = values.size(),
                                          .capacity = values.capacity(),
                                          .element_size = sizeof(T),
                                          .capacity_bytes = values.capacity() * sizeof(T),
                                          .allocation_usable_bytes
                                          = values.capacity() == 0u ? 0u : malloc_usable_size(const_cast<T*>(values.data()))};
}

}  // namespace

DesignRoutingPool::AppendTransaction::~AppendTransaction()
{
  if (_pool != nullptr) {
    _pool->rollback(_checkpoint);
  }
}

DesignRoutingPool::Checkpoint DesignRoutingPool::checkpoint() const noexcept
{
  return Checkpoint{.layer_count = _routing_layers.size(),
                    .path_count = _paths.size(),
                    .point_count = _points.size(),
                    .point_extra_count = _point_extras.size(),
                    .via_count = _vias.size(),
                    .via_extra_count = _via_extras.size(),
                    .rectangle_count = _rectangles.size(),
                    .extra_count = _path_extras.size()};
}

void DesignRoutingPool::rollback(Checkpoint value) noexcept
{
  _routing_layers.resize(value.layer_count);
  _paths.resize(value.path_count);
  _points.resize(value.point_count);
  _point_extras.resize(value.point_extra_count);
  _vias.resize(value.via_count);
  _via_extras.resize(value.via_extra_count);
  _rectangles.resize(value.rectangle_count);
  _path_extras.resize(value.extra_count);
}

void DesignRoutingPool::clear() noexcept
{
  _routing_layers.clear();
  _paths.clear();
  _points.clear();
  _point_extras.clear();
  _vias.clear();
  _via_extras.clear();
  _rectangles.clear();
  _path_extras.clear();
}

void DesignRoutingPool::shrinkToFit()
{
  _routing_layers.shrink_to_fit();
  _paths.shrink_to_fit();
  _points.shrink_to_fit();
  _point_extras.shrink_to_fit();
  _vias.shrink_to_fit();
  _via_extras.shrink_to_fit();
  _rectangles.shrink_to_fit();
  _path_extras.shrink_to_fit();
}

DesignRoutingPoolView DesignRoutingPool::serializedView() const noexcept
{
  return DesignRoutingPoolView{.routing_layers = _routing_layers,
                               .paths = _paths,
                               .points = _points,
                               .point_extras = _point_extras,
                               .vias = _vias,
                               .via_extras = _via_extras,
                               .rectangles = _rectangles,
                               .path_extras = _path_extras};
}

void DesignRoutingPool::restoreSerialized(DesignRoutingPoolData data)
{
  _routing_layers = std::move(data.routing_layers);
  _paths = std::move(data.paths);
  _points = std::move(data.points);
  _point_extras = std::move(data.point_extras);
  _vias = std::move(data.vias);
  _via_extras = std::move(data.via_extras);
  _rectangles = std::move(data.rectangles);
  _path_extras = std::move(data.path_extras);

  constexpr uint32_t kKnownPathFlags = DesignWirePathFlag::kHasWidth | DesignWirePathFlag::kHasMask
                                       | DesignWirePathFlag::kTaper | DesignWirePathFlag::kHasTaperRule
                                       | DesignWirePathFlag::kHasShape | DesignWirePathFlag::kHasStyle;
  constexpr uint32_t kPathValueFlags = DesignWirePathFlag::kHasWidth | DesignWirePathFlag::kHasMask
                                       | DesignWirePathFlag::kHasTaperRule | DesignWirePathFlag::kHasShape
                                       | DesignWirePathFlag::kHasStyle;

  uint32_t previous_point_end = 0;
  uint32_t previous_via_end = 0;
  uint32_t previous_rectangle_end = 0;
  for (std::size_t index = 0; index < _paths.size(); ++index) {
    const auto& path = _paths[index];
    if (path.meta.layer_ordinal >= _routing_layers.size() || path.meta.reserved != 0u
        || (path.meta.flags & ~kKnownPathFlags) != 0u || path.point_end.value <= previous_point_end
        || path.point_end.value > _points.size() || path.via_end.value < previous_via_end || path.via_end.value > _vias.size()
        || path.rectangle_end.value < previous_rectangle_end || path.rectangle_end.value > _rectangles.size()) {
      clear();
      throw std::runtime_error("binary design routing pool has invalid path ranges");
    }
    previous_point_end = path.point_end.value;
    previous_via_end = path.via_end.value;
    previous_rectangle_end = path.rectangle_end.value;
  }
  if (previous_point_end != _points.size() || previous_via_end != _vias.size() || previous_rectangle_end != _rectangles.size()) {
    clear();
    throw std::runtime_error("binary design routing pool has unowned primitives");
  }

  const auto validSparseIndexes = [](const auto& entries, std::size_t limit, auto index_of) {
    uint32_t previous = 0;
    bool first = true;
    for (const auto& entry : entries) {
      const auto index = index_of(entry);
      if (index >= limit || (!first && index <= previous)) {
        return false;
      }
      first = false;
      previous = index;
    }
    return true;
  };
  if (!validSparseIndexes(_point_extras, _points.size(), [](const auto& entry) { return entry.point_index; })
      || !validSparseIndexes(_via_extras, _vias.size(), [](const auto& entry) { return entry.via_index; })
      || !validSparseIndexes(_path_extras, _paths.size(), [](const auto& entry) { return entry.path_index; })) {
    clear();
    throw std::runtime_error("binary design routing pool has invalid sparse extras");
  }

  constexpr uint32_t kKnownPointFlags = DesignWirePointFlag::kHasExtension | DesignWirePointFlag::kVirtual;
  for (const auto& extra : _point_extras) {
    const bool has_extension = (extra.flags & DesignWirePointFlag::kHasExtension) != 0u;
    const bool is_virtual = (extra.flags & DesignWirePointFlag::kVirtual) != 0u;
    if ((extra.flags & ~kKnownPointFlags) != 0u || (!has_extension && extra.extension != 0) || (has_extension && is_virtual)) {
      clear();
      throw std::runtime_error("binary design routing pool has invalid point extras");
    }
  }

  constexpr uint32_t kKnownViaFlags = DesignWireViaFlag::kHasMask | DesignWireViaFlag::kHasArray;
  std::size_t via_extra_index = 0;
  for (std::size_t index = 0; index < _vias.size(); ++index) {
    const auto& via = _vias[index];
    const bool has_extra = via_extra_index < _via_extras.size() && _via_extras[via_extra_index].via_index == index;
    if (via.meta.reserved != 0u || (via.meta.flags & ~kKnownViaFlags) != 0u
        || via.meta.reference_kind > DesignRoutingViaReferenceKind::kDesign || !isValidDesignOrientation(
               static_cast<DesignOrientation>(via.meta.orientation))
        || has_extra != (via.meta.flags != 0u)) {
      clear();
      throw std::runtime_error("binary design routing pool has invalid VIA records");
    }
    if (has_extra) {
      const auto& extra = _via_extras[via_extra_index++];
      const bool has_mask = (via.meta.flags & DesignWireViaFlag::kHasMask) != 0u;
      const bool has_array = (via.meta.flags & DesignWireViaFlag::kHasArray) != 0u;
      const bool any_mask = extra.top_mask != 0u || extra.cut_mask != 0u || extra.bottom_mask != 0u;
      if (has_mask != any_mask || (has_array && (extra.rows == 0u || extra.columns == 0u))
          || (!has_array
              && (extra.rows != 1u || extra.columns != 1u || extra.step_x != 0 || extra.step_y != 0))) {
        clear();
        throw std::runtime_error("binary design routing pool has inconsistent VIA extras");
      }
    }
  }

  std::size_t path_extra_index = 0;
  for (std::size_t index = 0; index < _paths.size(); ++index) {
    const auto& path = _paths[index];
    const bool has_extra = path_extra_index < _path_extras.size() && _path_extras[path_extra_index].path_index == index;
    if (has_extra != ((path.meta.flags & kPathValueFlags) != 0u)) {
      clear();
      throw std::runtime_error("binary design routing pool has inconsistent path extras");
    }
    if (has_extra) {
      const auto& extra = _path_extras[path_extra_index++].value;
      const bool has_width = (path.meta.flags & DesignWirePathFlag::kHasWidth) != 0u;
      const bool has_mask = (path.meta.flags & DesignWirePathFlag::kHasMask) != 0u;
      const bool has_taper_rule = (path.meta.flags & DesignWirePathFlag::kHasTaperRule) != 0u;
      const bool has_shape = (path.meta.flags & DesignWirePathFlag::kHasShape) != 0u;
      const bool has_style = (path.meta.flags & DesignWirePathFlag::kHasStyle) != 0u;
      if ((!has_width && extra.width != 0) || has_mask != (extra.mask != 0u)
          || has_taper_rule == extra.taper_rule.empty() || has_shape == extra.shape.empty()
          || (!has_style && extra.style != 0)) {
        clear();
        throw std::runtime_error("binary design routing pool has invalid path extras");
      }
    }
  }
}

DesignRoutingPool::AppendTransaction DesignRoutingPool::append(DesignWireRoutingInput input)
{
  if (input.path_records.empty()) {
    throw std::invalid_argument("cannot append empty wire routing input");
  }

  const auto old = checkpoint();
  const auto point_base = checkedPoolEnd(_points.size());
  const auto via_base = checkedPoolEnd(_vias.size());
  const auto rectangle_base = checkedPoolEnd(_rectangles.size());
  const auto path_begin = checkedPoolEnd(_paths.size());
  try {
    for (const auto& source : input.points) {
      const auto point_index = checkedPoolEnd(_points.size());
      _points.push_back(DesignRoutingPointRecord{.position = source.position});
      if (needsPointExtra(source)) {
        _point_extras.push_back(DesignRoutingPointExtraEntry{.point_index = point_index,
                                                             .flags = source.flags,
                                                             .extension = source.extension});
      }
    }
    for (const auto& source : input.vias) {
      if (source.flags > std::numeric_limits<uint8_t>::max()) {
        throw std::overflow_error("wire VIA flags exceed uint8_t");
      }
      const bool has_tech_via = static_cast<bool>(source.tech_via);
      const bool has_design_via = static_cast<bool>(source.design_via);
      if (has_tech_via == has_design_via) {
        throw std::logic_error("wire VIA must have exactly one compact reference");
      }
      const auto via_index = checkedPoolEnd(_vias.size());
      const auto reference = has_design_via ? static_cast<uint64_t>(source.design_via.packed())
                                            : static_cast<uint64_t>(source.tech_via.packed());
      _vias.push_back(DesignRoutingViaRecord{
          .point_index = source.point_index,
          .meta = DesignRoutingViaMeta{.orientation = static_cast<uint8_t>(source.orientation),
                                       .flags = static_cast<uint8_t>(source.flags),
                                       .reference_kind = has_design_via ? DesignRoutingViaReferenceKind::kDesign
                                                                        : DesignRoutingViaReferenceKind::kTech},
          .reference = reference});
      if (needsViaExtra(source)) {
        _via_extras.push_back(DesignRoutingViaExtraEntry{.via_index = via_index,
                                                         .top_mask = source.top_mask,
                                                         .cut_mask = source.cut_mask,
                                                         .bottom_mask = source.bottom_mask,
                                                         .rows = source.rows,
                                                         .columns = source.columns,
                                                         .step_x = source.step_x,
                                                         .step_y = source.step_y});
      }
    }
    _rectangles.insert(_rectangles.end(), std::make_move_iterator(input.rectangles.begin()),
                       std::make_move_iterator(input.rectangles.end()));
    for (std::size_t index = 0; index < input.path_records.size(); ++index) {
      const auto& source = input.path_records[index];
      const auto layer = std::find(_routing_layers.begin(), _routing_layers.end(), source.layer);
      std::size_t layer_ordinal = static_cast<std::size_t>(layer - _routing_layers.begin());
      if (layer == _routing_layers.end()) {
        if (_routing_layers.size() >= std::numeric_limits<uint16_t>::max()) {
          throw std::overflow_error("routing layer ordinal exceeds uint16_t");
        }
        layer_ordinal = _routing_layers.size();
        _routing_layers.push_back(source.layer);
      }
      if (source.flags > std::numeric_limits<uint8_t>::max()) {
        throw std::overflow_error("wire path flags exceed uint8_t");
      }

      const auto global_path_index = checkedPoolEnd(_paths.size());
      if (source.extra_index != kInvalidDesignWirePathExtraIndex) {
        if (source.extra_index >= input.extras.size()) {
          throw std::logic_error("wire path extra index is inconsistent");
        }
        _path_extras.push_back(DesignRoutingPathExtraEntry{.path_index = global_path_index,
                                                           .value = std::move(input.extras[source.extra_index])});
      }
      _paths.push_back(
          DesignRoutingPathRecord{.meta = DesignRoutingPathMeta{.layer_ordinal = static_cast<uint16_t>(layer_ordinal),
                                                                .flags = static_cast<uint8_t>(source.flags)},
                                  .point_end = {checkedPoolEnd(static_cast<std::size_t>(point_base) + source.point_end.value)},
                                  .via_end = {checkedPoolEnd(static_cast<std::size_t>(via_base) + source.via_end.value)},
                                  .rectangle_end
                                  = {checkedPoolEnd(static_cast<std::size_t>(rectangle_base) + source.rectangle_end.value)}});
    }
    return AppendTransaction{*this,
                             old,
                             DesignWireRoutingHandle{.path_begin = path_begin,
                                                     .path_count = checkedPoolEnd(input.path_records.size())}};
  } catch (...) {
    rollback(old);
    throw;
  }
}

DesignWirePathView DesignRoutingPool::path(DesignWireRoutingHandle handle, std::size_t relative_index) const
{
  if (relative_index >= handle.path_count) {
    throw std::out_of_range("wire path index is out of range");
  }
  const auto index = static_cast<std::size_t>(handle.path_begin) + relative_index;
  if (index >= _paths.size()) {
    throw std::logic_error("wire routing handle is inconsistent with its pool");
  }
  return pathAt(index);
}

DesignWirePathView DesignRoutingPool::pathAt(std::size_t index) const
{
  if (index >= _paths.size()) {
    throw std::out_of_range("global wire path index is out of range");
  }
  const auto& record = _paths[index];
  if (record.meta.layer_ordinal >= _routing_layers.size()) {
    throw std::logic_error("global wire path layer ordinal is inconsistent");
  }
  const auto point_begin = index == 0u ? 0u : _paths[index - 1u].point_end.value;
  const auto via_begin = index == 0u ? 0u : _paths[index - 1u].via_end.value;
  const auto rectangle_begin = index == 0u ? 0u : _paths[index - 1u].rectangle_end.value;
  if (point_begin > record.point_end.value || record.point_end.value > _points.size() || via_begin > record.via_end.value
      || record.via_end.value > _vias.size() || rectangle_begin > record.rectangle_end.value
      || record.rectangle_end.value > _rectangles.size()) {
    throw std::logic_error("global wire routing ranges are inconsistent");
  }

  const DesignWirePathExtra* extra = nullptr;
  constexpr uint32_t kValueFlags = DesignWirePathFlag::kHasWidth | DesignWirePathFlag::kHasMask
                                   | DesignWirePathFlag::kHasTaperRule | DesignWirePathFlag::kHasShape
                                   | DesignWirePathFlag::kHasStyle;
  if ((record.meta.flags & kValueFlags) != 0u) {
    const auto found = std::lower_bound(_path_extras.begin(), _path_extras.end(), index,
                                        [](const auto& entry, std::size_t path_index) {
                                          return entry.path_index < path_index;
                                        });
    if (found == _path_extras.end() || found->path_index != index) {
      throw std::logic_error("global wire path extra is missing");
    }
    extra = &found->value;
  }

  const auto point_extra_first = std::lower_bound(_point_extras.begin(), _point_extras.end(), point_begin,
                                                   [](const auto& entry, uint32_t point_index) {
                                                     return entry.point_index < point_index;
                                                   });
  const auto point_extra_last = std::lower_bound(point_extra_first, _point_extras.end(), record.point_end.value,
                                                  [](const auto& entry, uint32_t point_index) {
                                                    return entry.point_index < point_index;
                                                  });
  const auto via_extra_first = std::lower_bound(_via_extras.begin(), _via_extras.end(), via_begin,
                                                 [](const auto& entry, uint32_t via_index) {
                                                   return entry.via_index < via_index;
                                                 });
  const auto via_extra_last = std::lower_bound(via_extra_first, _via_extras.end(), record.via_end.value,
                                                [](const auto& entry, uint32_t via_index) {
                                                  return entry.via_index < via_index;
                                                });

  return DesignWirePathView::create(
      _routing_layers[record.meta.layer_ordinal],
      record.meta.flags,
      extra,
      DesignWirePointRange::fromPool(
          std::span<const DesignRoutingPointRecord>{_points.data(), _points.size()}.subspan(
              point_begin, record.point_end.value - point_begin),
          std::span<const DesignRoutingPointExtraEntry>{point_extra_first, point_extra_last}, point_begin),
      DesignWireViaRange::fromPool(
          std::span<const DesignRoutingViaRecord>{_vias.data(), _vias.size()}.subspan(via_begin, record.via_end.value - via_begin),
          std::span<const DesignRoutingViaExtraEntry>{via_extra_first, via_extra_last}, via_begin),
      std::span<const DesignWireRectangle>{_rectangles.data(), _rectangles.size()}.subspan(
          rectangle_begin, record.rectangle_end.value - rectangle_begin));
}

DesignRoutingPoolStatistics DesignRoutingPool::statistics() const noexcept
{
  std::size_t dynamic_string_capacity_bytes = 0;
  for (const auto& extra : _path_extras) {
    dynamic_string_capacity_bytes += dynamicStringCapacityBytes(extra.value.taper_rule);
    dynamic_string_capacity_bytes += dynamicStringCapacityBytes(extra.value.shape);
  }
  return DesignRoutingPoolStatistics{.routing_layers = arrayStatistics(_routing_layers),
                                     .paths = arrayStatistics(_paths),
                                     .path_extras = arrayStatistics(_path_extras),
                                     .points = arrayStatistics(_points),
                                     .point_extras = arrayStatistics(_point_extras),
                                     .vias = arrayStatistics(_vias),
                                     .via_extras = arrayStatistics(_via_extras),
                                     .rectangles = arrayStatistics(_rectangles),
                                     .dynamic_string_capacity_bytes = dynamic_string_capacity_bytes};
}

}  // namespace eccdb

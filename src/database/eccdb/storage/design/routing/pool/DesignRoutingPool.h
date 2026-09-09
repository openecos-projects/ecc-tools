#pragma once

#include <algorithm>
#include <cstddef>
#include <concepts>
#include <functional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "design/routing/component/RoutingComponents.h"
#include "design/routing/pool/WireRoutingInput.h"

namespace eccdb {

struct DesignRoutingPoolArrayStatistics
{
  std::size_t count = 0;
  std::size_t capacity = 0;
  std::size_t element_size = 0;
  std::size_t capacity_bytes = 0;
  std::size_t allocation_usable_bytes = 0;
};

struct DesignRoutingPoolStatistics
{
  DesignRoutingPoolArrayStatistics routing_layers;
  DesignRoutingPoolArrayStatistics paths;
  DesignRoutingPoolArrayStatistics path_extras;
  DesignRoutingPoolArrayStatistics points;
  DesignRoutingPoolArrayStatistics point_extras;
  DesignRoutingPoolArrayStatistics vias;
  DesignRoutingPoolArrayStatistics via_extras;
  DesignRoutingPoolArrayStatistics rectangles;
  std::size_t dynamic_string_capacity_bytes = 0;
};

struct DesignRoutingPoolData
{
  std::vector<TechRoutingLayerId> routing_layers;
  std::vector<DesignRoutingPathRecord> paths;
  std::vector<DesignRoutingPointRecord> points;
  std::vector<DesignRoutingPointExtraEntry> point_extras;
  std::vector<DesignRoutingViaRecord> vias;
  std::vector<DesignRoutingViaExtraEntry> via_extras;
  std::vector<DesignWireRectangle> rectangles;
  std::vector<DesignRoutingPathExtraEntry> path_extras;
};

struct DesignRoutingPoolView
{
  std::span<const TechRoutingLayerId> routing_layers;
  std::span<const DesignRoutingPathRecord> paths;
  std::span<const DesignRoutingPointRecord> points;
  std::span<const DesignRoutingPointExtraEntry> point_extras;
  std::span<const DesignRoutingViaRecord> vias;
  std::span<const DesignRoutingViaExtraEntry> via_extras;
  std::span<const DesignWireRectangle> rectangles;
  std::span<const DesignRoutingPathExtraEntry> path_extras;
};

// Read-only path slice for high-throughput consumers. The compact records stay
// in their design-wide arrays; global_begin values join the sparse extras to
// their path-local records without reconstructing semantic value objects.
struct DesignRoutingCompactPathView
{
  TechRoutingLayerId layer;
  uint32_t flags = 0;
  const DesignWirePathExtra* extra = nullptr;
  std::span<const DesignRoutingPointRecord> points;
  std::span<const DesignRoutingPointExtraEntry> point_extras;
  uint32_t point_global_begin = 0;
  std::span<const DesignRoutingViaRecord> vias;
  std::span<const DesignRoutingViaExtraEntry> via_extras;
  uint32_t via_global_begin = 0;
  std::span<const DesignWireRectangle> rectangles;
};

// Design-wide compact routing storage. Its arrays and mutation transaction
// API are private: DesignRoutingStorage coordinates entity lifecycle, while
// clients query semantic views through DesignRoutingStorage.
class DesignRoutingPool
{
  struct Checkpoint
  {
    std::size_t layer_count = 0;
    std::size_t path_count = 0;
    std::size_t point_count = 0;
    std::size_t point_extra_count = 0;
    std::size_t via_count = 0;
    std::size_t via_extra_count = 0;
    std::size_t rectangle_count = 0;
    std::size_t extra_count = 0;
  };

 public:
  class AppendTransaction
  {
   public:
    AppendTransaction(DesignRoutingPool& pool, Checkpoint checkpoint, DesignWireRoutingHandle handle) noexcept
        : _pool(&pool), _checkpoint(checkpoint), _handle(handle)
    {
    }
    AppendTransaction(const AppendTransaction&) = delete;
    AppendTransaction& operator=(const AppendTransaction&) = delete;
    AppendTransaction(AppendTransaction&& other) noexcept
        : _pool(std::exchange(other._pool, nullptr)), _checkpoint(other._checkpoint), _handle(other._handle)
    {
    }
    AppendTransaction& operator=(AppendTransaction&&) = delete;
    ~AppendTransaction();

    [[nodiscard]] DesignWireRoutingHandle handle() const noexcept { return _handle; }
    void commit() noexcept { _pool = nullptr; }

   private:
    DesignRoutingPool* _pool = nullptr;
    Checkpoint _checkpoint;
    DesignWireRoutingHandle _handle;
  };

  [[nodiscard]] DesignRoutingPoolStatistics statistics() const noexcept;
  [[nodiscard]] DesignRoutingPoolView serializedView() const noexcept;
  void restoreSerialized(DesignRoutingPoolData data);
  [[nodiscard]] AppendTransaction append(DesignWireRoutingInput input);
  [[nodiscard]] DesignWirePathView path(DesignWireRoutingHandle handle, std::size_t relative_index) const;

  template <typename Function>
    requires std::invocable<Function&, DesignWirePathView>
  void forEachPath(DesignWireRoutingHandle handle, Function&& function) const
  {
    forEachCompactPath(handle, [&](DesignRoutingCompactPathView path) {
      std::invoke(function,
                  DesignWirePathView::create(
                      path.layer, path.flags, path.extra,
                      DesignWirePointRange::fromPool(path.points, path.point_extras, path.point_global_begin),
                      DesignWireViaRange::fromPool(path.vias, path.via_extras, path.via_global_begin), path.rectangles));
    });
  }

  template <typename Function>
    requires std::invocable<Function&, DesignRoutingCompactPathView>
  void forEachCompactPath(DesignWireRoutingHandle handle, Function&& function) const
  {
    const auto path_begin = static_cast<std::size_t>(handle.path_begin);
    const auto path_count = static_cast<std::size_t>(handle.path_count);
    if (path_begin > _paths.size() || path_count > _paths.size() - path_begin) {
      throw std::logic_error("wire routing handle is inconsistent with its pool");
    }
    if (path_count == 0u) {
      return;
    }

    const auto path_end = path_begin + path_count;
    auto point_begin = path_begin == 0u ? 0u : _paths[path_begin - 1u].point_end.value;
    auto via_begin = path_begin == 0u ? 0u : _paths[path_begin - 1u].via_end.value;
    auto rectangle_begin = path_begin == 0u ? 0u : _paths[path_begin - 1u].rectangle_end.value;
    auto path_extra = std::lower_bound(_path_extras.cbegin(), _path_extras.cend(), path_begin,
                                       [](const auto& entry, std::size_t index) { return entry.path_index < index; });
    auto point_extra = std::lower_bound(_point_extras.cbegin(), _point_extras.cend(), point_begin,
                                        [](const auto& entry, uint32_t index) { return entry.point_index < index; });
    auto via_extra = std::lower_bound(_via_extras.cbegin(), _via_extras.cend(), via_begin,
                                      [](const auto& entry, uint32_t index) { return entry.via_index < index; });

    constexpr uint32_t kValueFlags = DesignWirePathFlag::kHasWidth | DesignWirePathFlag::kHasMask
                                     | DesignWirePathFlag::kHasTaperRule | DesignWirePathFlag::kHasShape
                                     | DesignWirePathFlag::kHasStyle;
    for (auto path_index = path_begin; path_index < path_end; ++path_index) {
      const auto& record = _paths[path_index];
      if (record.meta.layer_ordinal >= _routing_layers.size()) {
        throw std::logic_error("global wire path layer ordinal is inconsistent");
      }
      if (point_begin > record.point_end.value || record.point_end.value > _points.size()
          || via_begin > record.via_end.value || record.via_end.value > _vias.size()
          || rectangle_begin > record.rectangle_end.value || record.rectangle_end.value > _rectangles.size()) {
        throw std::logic_error("global wire routing ranges are inconsistent");
      }

      while (path_extra != _path_extras.cend() && path_extra->path_index < path_index) {
        ++path_extra;
      }
      const DesignWirePathExtra* extra = nullptr;
      if ((record.meta.flags & kValueFlags) != 0u) {
        if (path_extra == _path_extras.cend() || path_extra->path_index != path_index) {
          throw std::logic_error("global wire path extra is missing");
        }
        extra = &path_extra->value;
      }
      if (path_extra != _path_extras.cend() && path_extra->path_index == path_index) {
        ++path_extra;
      }

      auto point_extra_end = point_extra;
      while (point_extra_end != _point_extras.cend() && point_extra_end->point_index < record.point_end.value) {
        ++point_extra_end;
      }
      auto via_extra_end = via_extra;
      while (via_extra_end != _via_extras.cend() && via_extra_end->via_index < record.via_end.value) {
        ++via_extra_end;
      }

      std::invoke(function, DesignRoutingCompactPathView{
                                .layer = _routing_layers[record.meta.layer_ordinal],
                                .flags = record.meta.flags,
                                .extra = extra,
                                .points = std::span<const DesignRoutingPointRecord>{_points}.subspan(
                                    point_begin, record.point_end.value - point_begin),
                                .point_extras = std::span<const DesignRoutingPointExtraEntry>{point_extra, point_extra_end},
                                .point_global_begin = static_cast<uint32_t>(point_begin),
                                .vias = std::span<const DesignRoutingViaRecord>{_vias}.subspan(
                                    via_begin, record.via_end.value - via_begin),
                                .via_extras = std::span<const DesignRoutingViaExtraEntry>{via_extra, via_extra_end},
                                .via_global_begin = static_cast<uint32_t>(via_begin),
                                .rectangles = std::span<const DesignWireRectangle>{_rectangles}.subspan(
                                    rectangle_begin, record.rectangle_end.value - rectangle_begin)});

      point_begin = record.point_end.value;
      via_begin = record.via_end.value;
      rectangle_begin = record.rectangle_end.value;
      point_extra = point_extra_end;
      via_extra = via_extra_end;
    }
  }

  void clear() noexcept;
  void shrinkToFit();

 private:
  [[nodiscard]] Checkpoint checkpoint() const noexcept;
  void rollback(Checkpoint checkpoint) noexcept;
  [[nodiscard]] DesignWirePathView pathAt(std::size_t global_index) const;

  std::vector<TechRoutingLayerId> _routing_layers;
  std::vector<DesignRoutingPathRecord> _paths;
  std::vector<DesignRoutingPointRecord> _points;
  std::vector<DesignRoutingPointExtraEntry> _point_extras;
  std::vector<DesignRoutingViaRecord> _vias;
  std::vector<DesignRoutingViaExtraEntry> _via_extras;
  std::vector<DesignWireRectangle> _rectangles;
  std::vector<DesignRoutingPathExtraEntry> _path_extras;
};

}  // namespace eccdb

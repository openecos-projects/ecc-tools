#pragma once

#include <cstddef>
#include <iterator>
#include <span>
#include <string_view>
#include <utility>

#include "design/routing/pool/RoutingPoolRecords.h"

namespace eccdb {

template <typename Range, typename Value>
class DesignWireValueIterator
{
 public:
  using iterator_concept = std::forward_iterator_tag;
  using iterator_category = std::forward_iterator_tag;
  using value_type = Value;
  using difference_type = std::ptrdiff_t;
  using reference = Value;

  DesignWireValueIterator() noexcept = default;
  DesignWireValueIterator(const Range* range, std::size_t index) noexcept : _range(range), _index(index) {}

  [[nodiscard]] Value operator*() const { return (*_range)[_index]; }
  DesignWireValueIterator& operator++() noexcept
  {
    ++_index;
    return *this;
  }
  DesignWireValueIterator operator++(int) noexcept
  {
    auto copy = *this;
    ++*this;
    return copy;
  }
  bool operator==(const DesignWireValueIterator&) const = default;

 private:
  const Range* _range = nullptr;
  std::size_t _index = 0;
};

class DesignWirePointRange
{
 public:
  using value_type = DesignWirePoint;
  using const_iterator = DesignWireValueIterator<DesignWirePointRange, DesignWirePoint>;

  [[nodiscard]] std::size_t size() const noexcept { return _owned.empty() ? _compact.size() : _owned.size(); }
  [[nodiscard]] bool empty() const noexcept { return size() == 0u; }
  [[nodiscard]] DesignWirePoint operator[](std::size_t index) const;
  [[nodiscard]] DesignWirePoint front() const { return (*this)[0]; }
  [[nodiscard]] const_iterator begin() const noexcept { return const_iterator{this, 0u}; }
  [[nodiscard]] const_iterator end() const noexcept { return const_iterator{this, size()}; }

  [[nodiscard]] static DesignWirePointRange fromValues(std::span<const DesignWirePoint> values) noexcept
  {
    return DesignWirePointRange{values};
  }
  [[nodiscard]] static DesignWirePointRange fromPool(std::span<const DesignRoutingPointRecord> compact,
                                                     std::span<const DesignRoutingPointExtraEntry> extras,
                                                     uint32_t global_begin) noexcept
  {
    return DesignWirePointRange{compact, extras, global_begin};
  }

 private:
  explicit DesignWirePointRange(std::span<const DesignWirePoint> owned) noexcept : _owned(owned) {}
  DesignWirePointRange(std::span<const DesignRoutingPointRecord> compact,
                       std::span<const DesignRoutingPointExtraEntry> extras, uint32_t global_begin) noexcept
      : _compact(compact), _extras(extras), _global_begin(global_begin)
  {
  }

  std::span<const DesignWirePoint> _owned;
  std::span<const DesignRoutingPointRecord> _compact;
  std::span<const DesignRoutingPointExtraEntry> _extras;
  uint32_t _global_begin = 0;
};

class DesignWireViaRange
{
 public:
  using value_type = DesignWireVia;
  using const_iterator = DesignWireValueIterator<DesignWireViaRange, DesignWireVia>;

  [[nodiscard]] std::size_t size() const noexcept { return _owned.empty() ? _compact.size() : _owned.size(); }
  [[nodiscard]] bool empty() const noexcept { return size() == 0u; }
  [[nodiscard]] DesignWireVia operator[](std::size_t index) const;
  [[nodiscard]] DesignWireVia front() const { return (*this)[0]; }
  [[nodiscard]] const_iterator begin() const noexcept { return const_iterator{this, 0u}; }
  [[nodiscard]] const_iterator end() const noexcept { return const_iterator{this, size()}; }

  [[nodiscard]] static DesignWireViaRange fromValues(std::span<const DesignWireVia> values) noexcept
  {
    return DesignWireViaRange{values};
  }
  [[nodiscard]] static DesignWireViaRange fromPool(std::span<const DesignRoutingViaRecord> compact,
                                                   std::span<const DesignRoutingViaExtraEntry> extras,
                                                   uint32_t global_begin) noexcept
  {
    return DesignWireViaRange{compact, extras, global_begin};
  }

 private:
  explicit DesignWireViaRange(std::span<const DesignWireVia> owned) noexcept : _owned(owned) {}
  DesignWireViaRange(std::span<const DesignRoutingViaRecord> compact,
                     std::span<const DesignRoutingViaExtraEntry> extras, uint32_t global_begin) noexcept
      : _compact(compact), _extras(extras), _global_begin(global_begin)
  {
  }

  std::span<const DesignWireVia> _owned;
  std::span<const DesignRoutingViaRecord> _compact;
  std::span<const DesignRoutingViaExtraEntry> _extras;
  uint32_t _global_begin = 0;
};

class DesignWirePathView
{
 public:
  [[nodiscard]] TechRoutingLayerId layer() const noexcept { return _layer; }
  [[nodiscard]] uint32_t flags() const noexcept { return _flags; }
  [[nodiscard]] int32_t width() const noexcept { return _extra == nullptr ? 0 : _extra->width; }
  [[nodiscard]] uint32_t mask() const noexcept { return _extra == nullptr ? 0u : _extra->mask; }
  [[nodiscard]] std::string_view taperRule() const noexcept { return _extra == nullptr ? std::string_view{} : _extra->taper_rule; }
  [[nodiscard]] std::string_view shape() const noexcept { return _extra == nullptr ? std::string_view{} : _extra->shape; }
  [[nodiscard]] int32_t style() const noexcept { return _extra == nullptr ? 0 : _extra->style; }
  [[nodiscard]] DesignWirePointRange points() const noexcept { return _points; }
  [[nodiscard]] DesignWireViaRange vias() const noexcept { return _vias; }
  [[nodiscard]] std::span<const DesignWireRectangle> rectangles() const noexcept { return _rectangles; }

  [[nodiscard]] static DesignWirePathView create(TechRoutingLayerId layer, uint32_t flags,
                                                 const DesignWirePathExtra* extra, DesignWirePointRange points,
                                                 DesignWireViaRange vias,
                                                 std::span<const DesignWireRectangle> rectangles) noexcept
  {
    return DesignWirePathView{layer, flags, extra, std::move(points), std::move(vias), rectangles};
  }

 private:
  DesignWirePathView(TechRoutingLayerId layer, uint32_t flags, const DesignWirePathExtra* extra,
                     DesignWirePointRange points, DesignWireViaRange vias,
                     std::span<const DesignWireRectangle> rectangles) noexcept
      : _layer(layer), _flags(flags), _extra(extra), _points(std::move(points)), _vias(std::move(vias)), _rectangles(rectangles)
  {
  }

  TechRoutingLayerId _layer;
  uint32_t _flags;
  const DesignWirePathExtra* _extra;
  DesignWirePointRange _points;
  DesignWireViaRange _vias;
  std::span<const DesignWireRectangle> _rectangles;
};

}  // namespace eccdb

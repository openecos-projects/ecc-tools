#pragma once

#include "GeometryTypes.h"

namespace ecc::geometry {

class ShapeIdAllocator
{
 public:
  explicit ShapeIdAllocator(ShapeId next_id = 1);

  ShapeId allocate();
  void reserve_seen_id(ShapeId id);
  void reset(ShapeId next_id = 1);
  ShapeId next_id() const;

 private:
  ShapeId _next_id;
};

inline ShapeIdAllocator::ShapeIdAllocator(ShapeId next_id) : _next_id(next_id == 0 ? 1 : next_id)
{
}

inline ShapeId ShapeIdAllocator::allocate()
{
  return _next_id++;
}

inline void ShapeIdAllocator::reserve_seen_id(ShapeId id)
{
  if (id >= _next_id) {
    _next_id = id + 1;
  }
}

inline void ShapeIdAllocator::reset(ShapeId next_id)
{
  _next_id = next_id == 0 ? 1 : next_id;
}

inline ShapeId ShapeIdAllocator::next_id() const
{
  return _next_id;
}

}  // namespace ecc::geometry

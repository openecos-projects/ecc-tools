#pragma once

#include <memory>
#include <span>
#include <vector>

#include "ProcessResult.hpp"

namespace ircx {

// Physical coordinates in micrometres. Layer selection and layout scaling are
// the caller's responsibility. Shapes describe occupied metal, not net bounds.
struct DensityRect
{
  double x_min, y_min, x_max, y_max;
};

struct DensitySpan
{
  double start, end;
  double density_start, density_end;
};

// Immutable union of rectangles. Queries are safe to share between threads.
class MetalDensity
{
 public:
  static ProcessResult<MetalDensity> build(std::span<const DensityRect> shapes);
  ProcessResult<double> density(double x, double y, double window = 50) const;
  // The square window moves along the selected axis; center is the fixed
  // transverse coordinate. Density is linear on each returned interval.
  ProcessResult<std::vector<DensitySpan>> profile(bool horizontal, double center, double start, double end, double window = 50) const;

 private:
  struct Index;
  explicit MetalDensity(std::shared_ptr<const Index> index) : _index(std::move(index)) {}
  std::shared_ptr<const Index> _index;
};
}  // namespace ircx

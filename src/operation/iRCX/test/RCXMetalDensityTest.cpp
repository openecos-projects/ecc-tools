#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

#include "MetalDensity.hpp"

namespace {
int failures = 0;
void check(bool condition, const char* message)
{
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
bool near(double a, double b)
{
  return std::abs(a - b) < 2e-12;
}
double value(const std::vector<ircx::DensitySpan>& spans, double position)
{
  for (const auto& span : spans) {
    if (span.start <= position && position <= span.end) {
      const double fraction = (position - span.start) / (span.end - span.start);
      return span.density_start + fraction * (span.density_end - span.density_start);
    }
  }
  return -1;
}
}  // namespace

int main()
{
  using namespace ircx;
  const std::vector<DensityRect> overlap{{-1, -1, 1, 0}, {0, -1, 1, 1}, {-1, -1, 1, 0}};
  auto metal = MetalDensity::build(overlap);
  check(bool(metal), "build overlapping metal");
  if (!metal)
    return 1;
  const auto fraction = metal->density(0, 0, 2);
  check(fraction && near(*fraction, 0.75), "union area excludes duplicate and overlapping metal");
  auto empty = MetalDensity::build({});
  check(empty && near(*empty->density(0, 0, 50), 0), "empty layer has zero density");
  check(!metal->density(0, 0, 0), "zero window is rejected");
  check(!metal->density(std::numeric_limits<double>::infinity(), 0, 2), "nonfinite position is rejected");
  check(!MetalDensity::build(std::vector<DensityRect>{{0, 0, 0, 1}}), "zero-area shape is rejected");
  check(!MetalDensity::build(std::vector<DensityRect>{{1, 0, 0, 1}}), "reversed shape is rejected");
  check(!MetalDensity::build(std::vector<DensityRect>{{0, 0, 1, std::numeric_limits<double>::quiet_NaN()}}), "nonfinite shape is rejected");

  auto square = MetalDensity::build(std::vector<DensityRect>{{0, 0, 2, 2}});
  auto profile = square->profile(true, 1, -2, 4, 2);
  check(profile && profile->size() == 4, "moving window has exact slope breakpoints");
  if (profile) {
    for (double x : {-2.0, -1.0, -0.5, 0.0, 1.0, 2.0, 2.5, 3.0, 4.0}) {
      const double expected = std::max(0.0, 1 - std::abs(x - 1) / 2);
      check(near(value(*profile, x), expected), "piecewise linear density matches analytic overlap");
    }
  }
  check(!square->profile(true, 1, 2, 1, 2), "reversed profile is rejected");
  const std::vector<DensityRect> tiles{{0, 0, 1, 1}, {1, 0, 2, 1}, {0, 1, 1, 2}, {1, 1, 2, 2}, {0, 0, 2, 1}};
  auto fragmented = MetalDensity::build(tiles);
  auto tiled_profile = fragmented->profile(true, 1, -2, 4, 2);
  check(tiled_profile && profile && tiled_profile->size() == profile->size(), "fragmentation does not add density boundaries");
  for (double x = -2; x <= 4; x += 0.125) {
    check(near(value(*tiled_profile, x), value(*profile, x)), "fragmentation preserves density");
    auto rotated = square->density(1, x, 2);
    check(rotated && near(*rotated, value(*profile, x)), "density is rotation invariant");
  }
  auto scaled = MetalDensity::build(std::vector<DensityRect>{{0, 0, 1.8, 1.8}});
  auto scaled_profile = scaled->profile(false, 0.9, -1.8, 3.6, 1.8);
  for (double x = -1.5; x <= 3.5; x += 0.125)
    check(near(value(*scaled_profile, x * 0.9), value(*profile, x)), "scaling geometry and window preserves density");
  check(near(*square->density(0, 0, 2), 0.25), "window area is not clipped to occupied bounds");

  // An independent integer occupancy oracle catches area and profile mistakes.
  std::mt19937 random(1403);
  std::vector<DensityRect> rectangles;
  bool cells[16][16]{};
  for (int i = 0; i < 80; ++i) {
    int x = random() % 15, y = random() % 15;
    int right = x + 1 + random() % (16 - x), top = y + 1 + random() % (16 - y);
    rectangles.push_back({double(x), double(y), double(right), double(top)});
    for (int a = x; a < right; ++a)
      for (int b = y; b < top; ++b)
        cells[a][b] = true;
  }
  auto random_metal = MetalDensity::build(rectangles);
  check(bool(random_metal), "random rectangle union builds");
  if (random_metal) {
    for (int x = -2; x <= 18; ++x)
      for (int y = -2; y <= 18; ++y) {
        int occupied = 0;
        for (int a = std::max(0, x - 2); a < std::min(16, x + 2); ++a)
          for (int b = std::max(0, y - 2); b < std::min(16, y + 2); ++b)
            occupied += cells[a][b];
        auto observed = random_metal->density(x, y, 4);
        check(observed && near(*observed, occupied / 16.0), "union density matches occupancy oracle");
        auto moving = random_metal->profile(true, y, -2, 18, 4);
        check(moving && near(value(*moving, x), occupied / 16.0), "moving density matches occupancy oracle");
      }
  }
  return failures ? 1 : 0;
}

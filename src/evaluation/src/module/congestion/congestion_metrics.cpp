#include "congestion_metrics.h"

#include <algorithm>
#include <cmath>

namespace ieval {

float weightedAverageOverflow(std::vector<int32_t> values)
{
  if (values.empty()) {
    return -1.0f;
  }

  std::sort(values.begin(), values.end(), std::greater<int32_t>());

  const size_t size = values.size();
  const size_t idx_0_5_percent = std::max(size_t(1), static_cast<size_t>(std::ceil(size * 0.005)));
  const size_t idx_1_percent = std::max(size_t(1), static_cast<size_t>(std::ceil(size * 0.01)));
  const size_t idx_2_percent = std::max(size_t(1), static_cast<size_t>(std::ceil(size * 0.02)));
  const size_t idx_5_percent = std::max(size_t(1), static_cast<size_t>(std::ceil(size * 0.05)));

  float sum_05 = 0.0f;
  float sum_1 = 0.0f;
  float sum_2 = 0.0f;
  float sum_5 = 0.0f;

  for (size_t i = 0; i < idx_0_5_percent; ++i) {
    sum_05 += values[i];
  }
  sum_1 = sum_05;

  for (size_t i = idx_0_5_percent; i < idx_1_percent; ++i) {
    sum_1 += values[i];
  }
  sum_2 = sum_1;

  for (size_t i = idx_1_percent; i < idx_2_percent; ++i) {
    sum_2 += values[i];
  }
  sum_5 = sum_2;

  for (size_t i = idx_2_percent; i < idx_5_percent; ++i) {
    sum_5 += values[i];
  }

  return (sum_05 * 0.4f + sum_1 * 0.3f + sum_2 * 0.2f + sum_5 * 0.1f) / 4.0f;
}

}  // namespace ieval

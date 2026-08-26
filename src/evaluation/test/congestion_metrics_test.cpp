#include "congestion_metrics.h"

#include <cmath>
#include <iostream>
#include <vector>

int main()
{
  if (ieval::weightedAverageOverflow({}) != -1.0f) {
    std::cerr << "empty overflow data should produce the unavailable sentinel\n";
    return 1;
  }

  if (std::fabs(ieval::weightedAverageOverflow({8}) - 2.0f) > 1e-6f) {
    std::cerr << "single-value weighted average changed\n";
    return 1;
  }

  return 0;
}

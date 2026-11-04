#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace ircx::pattern {
// Integrate the union of the two directed visibility supports. Values are
// total capacitances on [lo,hi]; coordinates may be DBU or microns, consistently.
class ReciprocalCap
{
  struct Event
  {
    double x, rate;
    int direction, count;
  };
  std::vector<Event> events_;

 public:
  void add(bool forward, double lo, double hi, double cap)
  {
    if (!(std::isfinite(lo) && std::isfinite(hi) && std::isfinite(cap)) || hi <= lo || cap < 0)
      throw std::invalid_argument("invalid reciprocal capacitance interval");
    const double rate = cap / (hi - lo);
    const int direction = forward ? 0 : 1;
    events_.push_back({lo, rate, direction, 1});
    events_.push_back({hi, -rate, direction, -1});
  }
  double value() const
  {
    if (events_.empty())
      return 0;
    auto events = events_;
    std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.x < b.x; });
    std::array<double, 2> rate{};
    std::array<int, 2> count{};
    double last = events.front().x, total = 0;
    for (std::size_t i = 0; i < events.size();) {
      const double x = events[i].x;
      const int directions = int(count[0] > 0) + int(count[1] > 0);
      if (directions)
        total += (x - last) * ((count[0] > 0 ? rate[0] : 0) + (count[1] > 0 ? rate[1] : 0)) / directions;
      while (i < events.size() && events[i].x == x) {
        const auto& event = events[i++];
        rate[event.direction] += event.rate;
        count[event.direction] += event.count;
      }
      for (int j = 0; j < 2; ++j)
        if (count[j] == 0)
          rate[j] = 0;
      last = x;
    }
    return std::max(0.0, total);
  }
};
}  // namespace ircx::pattern

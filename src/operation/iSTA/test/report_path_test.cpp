// SPDX-License-Identifier: MulanPSL-2.0
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ReportPathSelector.hpp"
#include "TimingTolerance.hpp"
#include "path/TimingPath.hpp"

using namespace ista;

void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}

int main()
{
  TimingPath worst, same_end, next, last;
  worst.set_start_point("b");
  worst.set_end_point("shared");
  worst.set_slack(-4.0);
  same_end.set_start_point("a");
  same_end.set_end_point("shared");
  same_end.set_slack(-3.0);
  next.set_start_point("c");
  next.set_end_point("other");
  next.set_slack(-2.0);
  last.set_start_point("d");
  last.set_end_point("last");
  last.set_slack(-1.0);
  std::vector<std::pair<std::string, TimingPath*>> candidates{{"g2", &last}, {"g2", &same_end}, {"g1", &next}, {"g1", &worst}};
  auto selected = ReportPathSelector::selectCandidates(candidates, 2, 1);
  require(selected.size() == 2 && selected[0].second == &worst && selected[1].second == &next, "endpoint quota must span groups before applying global limit");
  require(candidates[0].second == &last && worst.get_slack() == -4.0, "selection changed caller input or timing values");
  selected = ReportPathSelector::selectCandidates(candidates, 3, 2);
  require(selected.size() == 3 && selected[0].second == &worst && selected[1].second == &same_end && selected[2].second == &next, "nworst=2 selection changed");
  require(ReportPathSelector::selectCandidates({}, 2, 1).empty() && ReportPathSelector::selectCandidates(candidates, 0, 1).empty()
              && ReportPathSelector::selectCandidates(candidates, 2, 0).empty(),
          "empty/zero limits changed");
  // Numeric slack takes priority; epsilon equivalence is not transitive.
  worst.set_slack(0.0);
  next.set_slack(ista::kTimingTolerance * 0.5);
  selected = ReportPathSelector::selectCandidates({{"z", &worst}, {"a", &next}}, 2, 1);
  require(selected[0].second == &worst, "report sort discarded an actual slack difference");
  last.set_slack(1.5 * ista::kTimingTolerance);
  next.set_slack(0.75 * ista::kTimingTolerance);
  std::vector<int> permutation{0, 1, 2};
  const std::vector<std::pair<std::string, TimingPath*>> triple{{"c", &worst}, {"b", &next}, {"a", &last}};
  do {
    selected = ReportPathSelector::selectCandidates({triple[permutation[0]], triple[permutation[1]], triple[permutation[2]]}, 3, 3);
    require(selected[0].second == &worst && selected[1].second == &next && selected[2].second == &last, "three near slacks formed a comparator cycle");
  } while (std::next_permutation(permutation.begin(), permutation.end()));
  worst.set_slack(-std::numeric_limits<double>::infinity());
  next.set_slack(std::numeric_limits<double>::infinity());
  last.set_slack(std::numeric_limits<double>::quiet_NaN());
  selected = ReportPathSelector::selectCandidates({{"a", &last}, {"b", &next}, {"c", &worst}}, 3, 3);
  require(selected[0].second == &worst && selected[1].second == &next && selected[2].second == &last, "non-finite slack ordering is inconsistent");
  worst.set_slack(-1.0);
  next.set_slack(-1.0);
  selected = ReportPathSelector::selectCandidates({{"g", &next}, {"g", &worst}}, 2, 1);
  require(selected[0].second == &worst, "startpoint tie break changed");
  next.set_start_point("b");
  selected = ReportPathSelector::selectCandidates({{"g", &worst}, {"g", &next}}, 2, 1);
  require(selected[0].second == &next, "endpoint tie break changed");
}

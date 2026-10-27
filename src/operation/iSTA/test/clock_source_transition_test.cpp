// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>

#include "Database.hpp"
#include "graph/TimingStateQuery.hpp"

int main()
{
  using namespace ista;
  Database database;
  database.sdc().get_clock_map()["clk"].get_source_list().push_back("root");
  // Clock pin <- wire <- inverter <- wire <- root.  The immediate
  // predecessor of CK is rise, but its source waveform edge is fall.
  for (AnalysisType analysis : {AnalysisType::kMin, AnalysisType::kMax}) {
    auto addWaveform = [&](TransType source, TransType local) {
      database.graph().points()["CK"].get_clock_state("clk").waveforms[source].arrival_map[analysis][local] = 0.0;
    };
    addWaveform(TransType::kFall, TransType::kRise);
    addWaveform(TransType::kRise, TransType::kFall);
    if (clockSourceTransitions(database, "CK", "clk", analysis, TransType::kRise) != std::vector<TransType>{TransType::kFall}
        || clockSourceTransitions(database, "CK", "clk", analysis, TransType::kFall) != std::vector<TransType>{TransType::kRise}
        || clockSourceTransitions(database, "root", "clk", analysis, TransType::kRise) != std::vector<TransType>{TransType::kRise}) {
      throw std::runtime_error("clock source transition lost inverter polarity");
    }
  }
}

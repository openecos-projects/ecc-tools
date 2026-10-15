// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>

#include "Database.hpp"
#include "Utility.hpp"

int main()
{
  using namespace ista;
  Database database;
  database.get_timing_constraint().get_clock_map()["clk"].get_source_list().push_back("root");
  // Clock pin <- wire <- inverter <- wire <- root.  The immediate
  // predecessor of CK is rise, but its source waveform edge is fall.
  for (AnalysisType analysis : {AnalysisType::kMin, AnalysisType::kMax}) {
    auto connect = [&](std::string sink, TransType output, std::string source, TransType input) {
      auto& state = database.get_timing_point_map()[sink].get_clock_state("clk");
      state.physical_predecessor_map[analysis][output] = source;
      state.physical_predecessor_trans_type_map[analysis][output] = input;
    };
    connect("CK", TransType::kRise, "inv:Y", TransType::kRise);
    connect("inv:Y", TransType::kRise, "inv:A", TransType::kFall);
    connect("inv:A", TransType::kFall, "root", TransType::kFall);
    connect("CK", TransType::kFall, "inv:Y", TransType::kFall);
    connect("inv:Y", TransType::kFall, "inv:A", TransType::kRise);
    connect("inv:A", TransType::kRise, "root", TransType::kRise);
    if (Utility::getClockSourceTransition(database, "CK", "clk", analysis, TransType::kRise) != TransType::kFall
        || Utility::getClockSourceTransition(database, "CK", "clk", analysis, TransType::kFall) != TransType::kRise
        || Utility::getClockSourceTransition(database, "root", "clk", analysis, TransType::kRise) != TransType::kRise) {
      throw std::runtime_error("clock source transition lost inverter polarity");
    }
  }
}

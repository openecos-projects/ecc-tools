// SPDX-License-Identifier: MulanPSL-2.0
#include "ElectricalReportWriter.hpp"

#include <iomanip>

#include "ReportFormatter.hpp"
#include "Utility.hpp"

namespace ista {
void ElectricalReportWriter::write() const
{
  for (auto rule : {ElectricalRule::kTransition, ElectricalRule::kCapacitance}) {
    const std::string name = rule == ElectricalRule::kTransition ? "max_transition" : "max_capacitance";
    const std::string path = _config.tr_temp_directory_path + name;
    int count = 0;
    for (const auto& check : _database.electricalChecks())
      if (check.rule == rule && check.violated())
        ++count;
    if (_config.output_timing_reports) {
      auto out = STAUTIL.openOutputFile(path + ".rpt");
      out << name << " checks (library units; transition limits use each pin's trip points)\n"
          << "Pin  Net  Clock  Path  Mode  Edge  Actual  Limit  Slack  Status\n"
          << std::setprecision(9);
      for (const auto& check : _database.electricalChecks())
        if (check.rule == rule)
          out << check.pin << "  " << check.net << "  " << check.clock << "  " << (check.clock_path ? "clock" : "data") << "  "
              << GetAnalysisTypeName()(check.analysis) << "  " << GetTransTypeName()(check.edge) << "  " << check.actual << "  " << check.limit << "  "
              << check.slack() << "  " << (check.violated() ? "VIOLATED" : "MET") << '\n';
      out << "Violations: " << count << '\n';
      STAUTIL.finishOutputFile(out);
    }
    if (_config.output_timing_features) {
      auto out = STAUTIL.openOutputFile(path + ".json");
      out << "{\"violation_count\":" << count << ",\"checks\":[";
      bool first = true;
      for (const auto& check : _database.electricalChecks()) {
        if (check.rule != rule)
          continue;
        out << (first ? "" : ",") << "{\"pin\":\"" << escapeJsonString(check.pin) << "\",\"net\":\"" << escapeJsonString(check.net) << "\",\"clock\":\""
            << escapeJsonString(check.clock) << "\",\"path\":\"" << (check.clock_path ? "clock" : "data") << "\",\"analysis\":\""
            << GetAnalysisTypeName()(check.analysis) << "\",\"edge\":\"" << GetTransTypeName()(check.edge) << "\",\"actual\":";
        outputJsonNumber(out, check.actual);
        out << ",\"limit\":";
        outputJsonNumber(out, check.limit);
        out << ",\"slack\":";
        outputJsonNumber(out, check.slack());
        out << "}";
        first = false;
      }
      out << "]}\n";
      STAUTIL.finishOutputFile(out);
    }
  }
}
}  // namespace ista

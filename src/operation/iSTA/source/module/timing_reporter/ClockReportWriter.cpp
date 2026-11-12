// SPDX-License-Identifier: MulanPSL-2.0
#include "ClockReportWriter.hpp"

#include <iomanip>

#include "ReportFormatter.hpp"
#include "TimingTolerance.hpp"
#include "Utility.hpp"
namespace ista {
void ClockReportWriter::write() const
{
  for (auto type : {TimingCheckType::kPeriod, TimingCheckType::kWidth}) {
    const std::string name = type == TimingCheckType::kPeriod ? "min_period" : "min_pulse_width";
    const std::string path = _config.tr_temp_directory_path + name;
    int count = 0;
    for (const auto& check : _database.clockChecks())
      if (check.type == type && check.slack() < -kTimingTolerance)
        ++count;
    if (_config.output_timing_reports) {
      auto out = STAUTIL.openOutputFile(path + ".rpt");
      out << name << " checks (ns)\nPin  Clock  Edge  Required  Actual  Derate  CRPR  Slack  Status\n" << std::setprecision(9);
      for (const auto& check : _database.clockChecks())
        if (check.type == type)
          out << check.pin << "  " << check.clock << "  " << GetTransTypeName()(check.edge) << "  " << check.required << "  " << check.actual << "  "
              << check.derate << "  " << check.reconvergence << "  " << check.slack() << "  " << (check.slack() < -kTimingTolerance ? "VIOLATED" : "MET")
              << '\n';
      out << "Violations: " << count << '\n';
      STAUTIL.finishOutputFile(out);
    }
    if (_config.output_timing_features) {
      auto out = STAUTIL.openOutputFile(path + ".json");
      out << "{\"violation_count\":" << count << ",\"checks\":[";
      bool first = true;
      for (const auto& check : _database.clockChecks()) {
        if (check.type != type)
          continue;
        out << (first ? "" : ",") << "{\"pin\":\"" << escapeJsonString(check.pin) << "\",\"clock\":\"" << escapeJsonString(check.clock) << "\",\"edge\":\""
            << GetTransTypeName()(check.edge) << "\",\"required\":";
        outputJsonNumber(out, check.required);
        out << ",\"actual\":";
        outputJsonNumber(out, check.actual);
        out << ",\"derate\":";
        outputJsonNumber(out, check.derate);
        out << ",\"reconvergence\":";
        outputJsonNumber(out, check.reconvergence);
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

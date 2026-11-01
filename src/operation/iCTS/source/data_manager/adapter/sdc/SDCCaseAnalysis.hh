// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

namespace ista {
class SdcCommand;
}

namespace icts {
struct SdcClockData;
}

namespace icts::sdc_reader {

// Record symbolic case constraints in this clock-only interpreter, before IDB binding.
auto RegisterCaseAnalysisRecorder(ista::SdcCommand& interpreter, SdcClockData& data) -> void;

}  // namespace icts::sdc_reader

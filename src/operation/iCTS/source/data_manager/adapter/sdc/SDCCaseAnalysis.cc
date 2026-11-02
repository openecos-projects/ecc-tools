// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file SDCCaseAnalysis.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-10-28
 * @brief Records evaluated case constraints without binding an STA network.
 */

#include "SDCCaseAnalysis.hh"

#if __has_include(<tcl8.6/tcl.h>)
#include <tcl8.6/tcl.h>
#else
#include <tcl.h>
#endif

#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "SdcCommand.hpp"
#include "SdcTclCmd.hpp"
#include "query/SdcObject.hpp"
#include "timing/TimingConstraints.hh"

namespace icts::sdc_reader {
namespace {

auto rejectCaseAnalysis(SdcClockData& data, Tcl_Interp* interp, SdcConstraintStatusCode code, const std::string& detail) -> int
{
  if (data.ok()) {
    data.status = code;
  }
  data.issues.push_back({code, "set_case_analysis", detail});
  data.diagnostics.push_back(std::string{"set_case_analysis:"} + detail);
  Tcl_SetObjResult(interp, Tcl_NewStringObj(data.diagnostics.back().c_str(), -1));
  return TCL_ERROR;
}

auto caseAnalysisValue(const std::string& text, int& value) -> SdcConstraintStatusCode
{
  std::istringstream stream(text);
  int parsed = 0;
  stream >> parsed;
  if (!stream.fail() && (parsed == 0 || parsed == 1)) {
    stream >> std::ws;
    if (stream.eof()) {
      value = parsed;
      return SdcConstraintStatusCode::kOk;
    }
  }
  return text == "rise" || text == "fall" || text == "rising" || text == "falling" ? SdcConstraintStatusCode::kUnsupported
                                                                                   : SdcConstraintStatusCode::kMalformed;
}

class CaseAnalysisRecorder
{
 public:
  CaseAnalysisRecorder(const char* /*command_name*/, ClientData client_data) : _data(static_cast<SdcClockData*>(client_data)) {}

  auto execute(Tcl_Interp* interp, int argc, Tcl_Obj* const* argv) -> int
  {
    auto& data = *_data;
    if (argc != 3) {
      return rejectCaseAnalysis(data, interp, SdcConstraintStatusCode::kMalformed, "value_and_objects_required");
    }
    const std::span<Tcl_Obj* const> arguments(argv, 3U);
    SdcCaseAnalysis analysis;
    const auto status = caseAnalysisValue(Tcl_GetString(arguments[1]), analysis.value);
    if (status != SdcConstraintStatusCode::kOk) {
      return rejectCaseAnalysis(data, interp, status, "only_constant_zero_or_one_case_values_supported");
    }
    using ista::sdc::SdcObject;
    using ista::sdc::SdcObjectType;
    for (auto* object : SdcObject::collection(arguments[2])) {
      const auto type = SdcObject::getType(object);
      SdcObjectRef ref{.pattern = Tcl_GetString(object), .from_collection_cmd = type != SdcObjectType::kNone};
      switch (type) {
        case SdcObjectType::kPort:
          ref.kind = SdcObjectKind::kPort;
          break;
        case SdcObjectType::kPin:
          ref.kind = SdcObjectKind::kPin;
          break;
        case SdcObjectType::kNet:
          ref.kind = SdcObjectKind::kNet;
          break;
        case SdcObjectType::kNone:
          break;
        default:
          return rejectCaseAnalysis(data, interp, SdcConstraintStatusCode::kMalformed, "expected_pin_port_or_net_collection");
      }
      if (ref.pattern.empty()) {
        return rejectCaseAnalysis(data, interp, SdcConstraintStatusCode::kUnresolvedReference, "empty_object_name");
      }
      analysis.objects.push_back(std::move(ref));
    }
    if (analysis.objects.empty()) {
      return rejectCaseAnalysis(data, interp, SdcConstraintStatusCode::kUnresolvedReference, "empty_object_collection");
    }
    data.case_analyses.push_back(std::move(analysis));
    Tcl_ResetResult(interp);
    return TCL_OK;
  }

 private:
  // Borrowed from the reader for this Tcl command invocation.
  SdcClockData* _data;
};

auto recordCaseAnalysis(ClientData client_data, Tcl_Interp* interp, int argc, Tcl_Obj* const* argv) -> int
{
  // Reuse the shared frontend's command execution/error boundary. Record a
  // typed failure before outer Tcl control flow can consume its error result.
  const auto result = ista::sdc::executeTclCommand<CaseAnalysisRecorder>(client_data, interp, argc, argv);
  auto& data = *static_cast<SdcClockData*>(client_data);
  if (result != TCL_OK && data.ok()) {
    return rejectCaseAnalysis(data, interp, SdcConstraintStatusCode::kMalformed, Tcl_GetStringResult(interp));
  }
  return result;
}

}  // namespace

auto RegisterCaseAnalysisRecorder(ista::SdcCommand& interpreter, SdcClockData& data) -> void
{
  interpreter.createCmd("set_case_analysis", recordCaseAnalysis, &data);
}

}  // namespace icts::sdc_reader

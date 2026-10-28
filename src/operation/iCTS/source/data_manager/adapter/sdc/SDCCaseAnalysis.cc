// SPDX-License-Identifier: MulanPSL-2.0
#include "SDCCaseAnalysis.hh"

#include <stdexcept>
#include <string>
#include <utility>

#include "SdcCommand.hpp"
#include "clock_parser/SDCClockParser.hh"
#include "query/SdcObject.hpp"

namespace icts::sdc_reader {
namespace {

auto RejectCaseAnalysis(SdcClockData& data, Tcl_Interp* interp, SdcConstraintStatusCode code, const std::string& detail) -> int
{
  if (data.ok()) {
    data.status = code;
  }
  data.issues.push_back({code, "set_case_analysis", detail});
  data.diagnostics.push_back("set_case_analysis:" + detail);
  Tcl_SetObjResult(interp, Tcl_NewStringObj(data.diagnostics.back().c_str(), -1));
  return TCL_ERROR;
}

auto RecordCaseAnalysis(ClientData client_data, Tcl_Interp* interp, int argc, Tcl_Obj* const argv[]) -> int
{
  auto& data = *static_cast<SdcClockData*>(client_data);
  if (argc != 3) {
    return RejectCaseAnalysis(data, interp, SdcConstraintStatusCode::kMalformed, "value_and_objects_required");
  }
  SdcCaseAnalysis analysis;
  const auto status = ParseCaseAnalysisValue(Tcl_GetString(argv[1]), analysis.value);
  if (status != SdcConstraintStatusCode::kOk) {
    return RejectCaseAnalysis(data, interp, status, "only_constant_zero_or_one_case_values_supported");
  }
  try {
    using ista::sdc::SdcObject;
    using ista::sdc::SdcObjectType;
    for (auto* object : SdcObject::collection(argv[2])) {
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
          return RejectCaseAnalysis(data, interp, SdcConstraintStatusCode::kMalformed, "expected_pin_port_or_net_collection");
      }
      if (ref.pattern.empty()) {
        return RejectCaseAnalysis(data, interp, SdcConstraintStatusCode::kUnresolvedReference, "empty_object_name");
      }
      analysis.objects.push_back(std::move(ref));
    }
  } catch (const std::invalid_argument& error) {
    return RejectCaseAnalysis(data, interp, SdcConstraintStatusCode::kMalformed, error.what());
  }
  if (analysis.objects.empty()) {
    return RejectCaseAnalysis(data, interp, SdcConstraintStatusCode::kUnresolvedReference, "empty_object_collection");
  }
  data.case_analyses.push_back(std::move(analysis));
  Tcl_ResetResult(interp);
  return TCL_OK;
}

}  // namespace

auto RegisterCaseAnalysisRecorder(ista::SdcCommand& interpreter, SdcClockData& data) -> void
{
  Tcl_CreateObjCommand(interpreter.getInterp(), "set_case_analysis", RecordCaseAnalysis, &data, nullptr);
}

}  // namespace icts::sdc_reader

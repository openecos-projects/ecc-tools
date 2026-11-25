#pragma once

#include <initializer_list>
#include <string_view>

#include "ProcessUnits.hpp"

namespace ircx::process_detail {

bool isOneOf(std::string_view key, std::initializer_list<std::string_view> keys);
Failure fail(const Field& field, std::string code, std::string message);
const Field* find(const Field& field, std::string_view key);
Status uniqueFields(const Field& field, std::initializer_list<std::string_view> repeatable = {});
Result<double> scalar(const Field& field, const ProcessUnits& units, Dimension dimension = Dimension::kScalar);
Status positive(double value, const Field& field);
Status validateDeferredModel(const Field& field, const ProcessUnits& units);
Result<ProcessEffectType> effect(const Field& field);
Result<ProcessTable2D> table2D(const Field& field, const ProcessUnits& units, std::string_view row, std::string_view column, Dimension values,
                               bool positive_values = false);
Status table1D(const Field& field, const ProcessUnits& units, Dimension key_dimension, Dimension value_dimension, ProcessTable1D& first,
               ProcessTable1D* second = nullptr, bool positive_values = false);
Result<ProcessConductor> buildConductor(const Field& field, SyntaxKind kind, const ProcessUnits& units, std::vector<ProcessDiagnostic>& diagnostics);
Result<ProcessVia> buildVia(const Field& field, SyntaxKind kind, const ProcessUnits& units, std::vector<ProcessDiagnostic>& diagnostics);
void ignored(const Field& field, std::vector<ProcessDiagnostic>& diagnostics);
void deferEvaluation(const Field& field, ProcessEffectType effect, std::string message, std::vector<ProcessEvaluationIssue>& issues,
                     std::vector<ProcessDiagnostic>& diagnostics);

}  // namespace ircx::process_detail

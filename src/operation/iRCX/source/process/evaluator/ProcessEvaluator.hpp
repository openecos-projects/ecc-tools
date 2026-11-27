#pragma once

#include <span>

#include "ProcessGeometry.hpp"
#include "ProcessVia.hpp"
#include "ProcessThicknessEvaluator.hpp"

namespace ircx {
// Neighbors refer to inputs in this batch. Each table stage reads a complete
// snapshot; span boundaries propagate from neighbors before the next stage.
ProcessResult<WireGeometryProfiles> evaluateWireGeometryProfiles(std::span<const WireGeometryInput> wires, ProcessEffectType effect);
// Produces the complete etch snapshot for a layout density calculation. The
// caller must finalize thickness/sidewalls before resistance or capacitance use.
ProcessResult<WireGeometryProfiles> evaluateWireEtchProfiles(std::span<const WireGeometryInput> wires, ProcessEffectType effect);
ProcessResult<void, std::string> validateWireGeometryProfiles(std::span<const WireGeometryInput> wires, const WireGeometryProfiles& profiles, ProcessEffectType effect);
// A standalone query represents fixed neighboring sidewalls. Use the profile
// evaluator when multiple wires undergo the same sequence of process steps.
ProcessResult<EffectiveWireGeometry> evaluateWireGeometry(const ProcessConductor& conductor, const WireGeometryQuery& query, ProcessEffectType effect);
// Consumes finalized geometry; does not scale or etch it a second time.
ProcessResult<double> evaluateWireResistanceFromGeometry(const ProcessConductor& conductor, const WireGeometryQuery& drawn,
    const EffectiveWireGeometry& geometry, double length, double temperature, double nominal_temperature);
ProcessResult<double> evaluateWireResistance(const ProcessConductor& conductor, const WireGeometryQuery& query, double length, double temperature,
                                             double nominal_temperature);
ProcessResult<double> evaluateViaResistance(const ProcessVia& via, double width, double length, double temperature, double nominal_temperature);
}  // namespace ircx

// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory

#pragma once

#include "STAInterface.hpp"

namespace eccdb {
class DatabaseContext;
}

namespace eccdb::adapters {

// Installs or clears the borrowed EccDB projection for one iSTA session. The
// Context and every pointer in inputs must outlive initSTA through destroySTA
// or abortSTA. Passing nullptr clears both the projection and borrowed inputs.
void bindStaSource(ista::STAInterface& interface, DatabaseContext* context, ista::NativeTimingInputs inputs = {});

}  // namespace eccdb::adapters

// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory

#pragma once

namespace eccdb {
class DatabaseContext;
}

namespace iemir {
class EMIRInterface;
}

namespace eccdb::adapters {

// Installs or clears the borrowed EccDB projection for one iEMIR session.
void bindEmirSource(iemir::EMIRInterface& interface, DatabaseContext* context);

}  // namespace eccdb::adapters

// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory

#pragma once

#include "PWInterface.hpp"

namespace eccdb {
class DatabaseContext;
}

namespace eccdb::adapters {

// Installs or clears the borrowed EccDB projection for one iPW session. The
// Context and every pointer in inputs must outlive initPW through destroyPW or
// abortPW. Passing nullptr clears both the projection and borrowed inputs.
void bindPwSource(ipw::PWInterface& interface, DatabaseContext* context, ipw::NativePowerInputs inputs = {});

}  // namespace eccdb::adapters

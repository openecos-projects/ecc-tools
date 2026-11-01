// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Beijing Institute of Open Source Chip

#pragma once

namespace imj {
class MJInterface;
}

namespace eccdb {
class DatabaseContext;

namespace adapters {

// Binds a borrowed EccDB context to iMJ's backend-neutral operation hooks.
// Passing nullptr clears every hook and restores iMJ's legacy iDB path.
void bindMjSource(imj::MJInterface& interface, DatabaseContext* context);

}  // namespace adapters
}  // namespace eccdb

// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory

#pragma once

namespace eccdb {
class DatabaseContext;
}

namespace ilvs {
class LVSInterface;
class NetlistData;
}

namespace eccdb::adapters {

// Binds a borrowed EccDB physical design and independently imported logical
// reference netlist to an iLVS session. Passing nullptr restores iDB wrapping.
void bindLvsSource(ilvs::LVSInterface& interface, DatabaseContext* context, const ilvs::NetlistData* reference);

// Projects only EccDB logical connectivity into iLVS's reference model.
[[nodiscard]] ilvs::NetlistData wrapLvsNetlist(const DatabaseContext& context);

}  // namespace eccdb::adapters

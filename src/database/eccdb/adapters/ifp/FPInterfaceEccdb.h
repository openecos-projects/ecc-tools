// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

namespace eccdb {
class DatabaseContext;
}

namespace ifp {
class FPInterface;
}

namespace eccdb::adapters {

// The source borrows context for the complete iFP session. Bind or detach it
// only outside init/run/destroy (including abort) of that session.
void bindFpSource(ifp::FPInterface& interface, DatabaseContext* context);

inline void bindFpSource(ifp::FPInterface& interface, DatabaseContext& context)
{
  bindFpSource(interface, &context);
}

}  // namespace eccdb::adapters

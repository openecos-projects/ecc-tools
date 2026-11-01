// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

namespace eccdb {
class DatabaseContext;
class DesignStore;
class TechStore;
class LibraryStore;
}  // namespace eccdb

namespace irt {
class RTInterface;
}  // namespace irt

namespace eccdb::adapters {

// The source borrows the supplied EccDB objects for the complete iRT session.
// Bind or detach only after iRT has finished or aborted its active session.
// Detach before destroying the tool singleton or the borrowed stores. The
// binder also attaches/detaches the iDRC source used by the routing session.
void bindRtSource(irt::RTInterface& tool, DatabaseContext* context);
void bindRtSource(irt::RTInterface& tool, DesignStore* design, TechStore* technology, LibraryStore* library);

}  // namespace eccdb::adapters

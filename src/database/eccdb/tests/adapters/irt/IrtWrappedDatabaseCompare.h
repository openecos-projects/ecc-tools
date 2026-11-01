// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <string>

namespace irt {
class Database;
}

namespace eccdb::test {

// Canonicalizes compared collections in place; callers pass copies so the
// source work model remains available to routing unchanged.
std::string compareIrtWrappedDatabase(irt::Database& left, irt::Database& right);

}  // namespace eccdb::test

// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory
#pragma once

#include <string>
#include <vector>

#include "../../../../interaction/RT_DRC/ids.hpp"

namespace idrc {
class Database;
class DRCInterface;
}  // namespace idrc

namespace eccdb::test {

// Returns an empty string when the two iDRC working databases have equivalent
// semantic contents. This belongs to the adapter differential tests rather
// than the production iDRC API.
std::string compareIdrcWrappedDatabase(idrc::Database& left, idrc::Database& right);

// The legacy iDB helpers expose DRCShape while EccDB sources expose ids::Shape.
// Keep the DRCShape conversion in the test support implementation so users of
// this header do not need an iDRC DRCShape definition (iRT has same-named
// internal headers).
std::vector<ids::Shape> legacyIdrcEnvironmentShapes(idrc::DRCInterface& interface);
std::vector<ids::Shape> legacyIdrcResultShapes(idrc::DRCInterface& interface);

}  // namespace eccdb::test

// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>
#include <string>

#include "graph/PathTagTable.hpp"

using ista::PathTagTable;
using ista::TransType;

namespace {

void require(bool condition, const char* message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

PathTagTable::Key baseKey()
{
  PathTagTable::Key key;
  key.clock_name = "clk";
  key.launch_transition = TransType::kRise;
  return key;
}

void canonicalMatchesTheLegacyFormat()
{
  // The delimiter layout is the compatibility surface: the sparse exporter
  // parses it and report paths carry it.
  require(PathTagTable::canonical(baseKey()) == "clk", "base key canonical form changed");
  auto key = baseKey();
  key.launch_transition = TransType::kFall;
  key.clock_source_path = true;
  key.launch_clock_edge_index = 3;
  key.exception_state_list = {0, 2, -1};
  key.crpr_path = "u1/CK";
  require(PathTagTable::canonical(key) == "clk\x1b" "3\x1d\x1e\x1f" "0\x1f" "2\x1f-1\x1c" "5:u1/CK", "canonical form diverged from the legacy layout");
  // Edge index < 2 carries no marker (matches the legacy condition).
  key = baseKey();
  key.launch_clock_edge_index = 1;
  require(PathTagTable::canonical(key) == "clk", "edge index 1 must not emit a marker");
  // CRPR is length-prefixed; empty CRPR emits nothing.
  key = baseKey();
  key.crpr_path = "";
  require(PathTagTable::canonical(key) == "clk", "empty CRPR path must emit nothing");
}

void internDedupsAndPreservesOrder()
{
  PathTagTable table;
  auto a = baseKey();
  auto b = baseKey();
  b.clock_name = "other";
  const uint32_t a1 = table.intern(a);
  const uint32_t b1 = table.intern(b);
  const uint32_t a2 = table.intern(a);
  require(a1 == a2, "interning the same key twice returned different ids");
  require(a1 != b1, "distinct keys collided on the same id");
  require(a1 == 1 && b1 == 2, "ids are not dense 1-based ordinals");
  require(table.size() == 2, "size() does not reflect unique key count");
}

void viewRoundTripsTheCanonicalForm()
{
  PathTagTable table;
  auto key = baseKey();
  key.exception_state_list = {1, 2};
  const uint32_t id = table.intern(key);
  require(table.view(id) == "clk\x1f" "1\x1f" "2", "view() did not return the canonical string for the id");
}

void keysDifferOnEveryField()
{
  PathTagTable table;
  auto base = baseKey();
  require(table.intern(base) == table.intern(base), "identical keys must dedup");
  auto variant = base;
  variant.launch_transition = TransType::kFall;
  require(table.intern(variant) != table.intern(base), "launch transition was ignored");
  variant = base;
  variant.clock_source_path = true;
  require(table.intern(variant) != table.intern(base), "clock-source-path flag was ignored");
  variant = base;
  variant.exception_state_list = {0};
  require(table.intern(variant) != table.intern(base), "exception state list was ignored");
  variant = base;
  variant.crpr_path = "p";
  require(table.intern(variant) != table.intern(base), "CRPR path was ignored");
  variant = base;
  variant.launch_clock_edge_index = 2;
  require(table.intern(variant) != table.intern(base), "edge index was ignored");
}

}  // namespace

int main()
{
  canonicalMatchesTheLegacyFormat();
  internDedupsAndPreservesOrder();
  viewRoundTripsTheCanonicalForm();
  keysDifferOnEveryField();
  return 0;
}

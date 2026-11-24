// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>
#include <string>
#include <vector>

#include "NamedTable.h"

namespace {

void require(bool condition, const char* message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct TestTag;

struct Widget
{
  std::string tag;
  int value = 0;
};

using WidgetTable = ecc::symbol::NamedTable<Widget, TestTag>;

void operatorBracketInsertsOrFetchesLikeMap()
{
  WidgetTable table;
  table["alpha"].value = 1;
  table["beta"].value = 2;
  table["alpha"].value = 3;  // re-indexing an existing key must not create a second entry.
  require(table.size() == 2, "operator[] on an existing key grew the table");
  require(table["alpha"].value == 3, "operator[] on an existing key did not return the same slot");
  require(table["beta"].value == 2, "inserting a second key corrupted the first");
}

void findDoesNotInsertAndAtThrowsOnMiss()
{
  WidgetTable table;
  require(table.find("missing") == nullptr, "find() reported a name that was never inserted");
  require(table.size() == 0, "find() on a miss inserted an entry");
  require(!table.contains("missing") && table.count("missing") == 0, "contains/count disagreed with find on a miss");
  bool threw = false;
  try {
    table.at("missing");
  } catch (const std::out_of_range&) {
    threw = true;
  }
  require(threw, "at() did not throw on a missing name");
}

// Mirrors the test-fixture idiom the audit found ~30+ times in iSTA
// (e.g. SdcTestDesign.hpp's addInstance holding Instance& across a loop that
// calls addPin, which re-indexes the same instance key): take a reference,
// then insert *other*, unrelated keys, then use the original reference.
void referencesSurviveUnrelatedInsertionsAndGrowth()
{
  WidgetTable table;
  Widget& first = table["first"];
  first.value = 10;
  // Insert enough unrelated keys to force at least one std::deque block
  // growth; a std::vector-backed table would reallocate and invalidate
  // `first` here, which is exactly the hazard this container exists to avoid.
  for (int i = 0; i < 500; ++i) {
    table["other_" + std::to_string(i)].value = i;
  }
  require(first.value == 10, "an unrelated insertion invalidated a previously taken reference");
  require(table["first"].value == 10, "growth corrupted the original entry's value");
}

void clearResetsToEmpty()
{
  WidgetTable table;
  table["a"].value = 1;
  table["b"].value = 2;
  table.clear();
  require(table.size() == 0 && table.empty(), "clear() did not empty the table");
  require(!table.contains("a") && !table.contains("b"), "clear() left stale entries findable");
  // The table must remain usable after clear(), starting a fresh id space.
  table["a"].value = 99;
  require(table.size() == 1 && table["a"].value == 99, "table unusable after clear()");
}

void idRoundTripsThroughNameOfAndGet()
{
  WidgetTable table;
  table["zebra"].value = 1;
  table["apple"].value = 2;
  const auto zebra_id = table.id("zebra");
  const auto apple_id = table.id("apple");
  require(zebra_id.has_value() && apple_id.has_value(), "id() failed to resolve an inserted name");
  require(*zebra_id != *apple_id, "two distinct names resolved to the same id");
  require(table.nameOf(*zebra_id) == "zebra" && table.nameOf(*apple_id) == "apple", "nameOf() did not round-trip id()'s result");
  require(table.get(*zebra_id).value == 1 && table.get(*apple_id).value == 2, "get(id) did not return the entry intern()/operator[] created");
  require(!table.id("missing").has_value(), "id() reported a name that was never inserted");
}

// sortedIds() must reproduce std::map<std::string, T> iteration order
// (name-lexicographic) regardless of insertion order, matching the
// invariant locked by iSTA's name_order_invariant_test.cpp for this refactor.
void sortedIdsAreNameLexicographicRegardlessOfInsertOrder()
{
  WidgetTable table;
  for (const char* name : {"zebra", "mango", "apple", "kiwi"}) {
    table[name].value = 0;
  }
  const std::vector<uint32_t> ids = table.sortedIds();
  require(ids.size() == 4, "sortedIds() dropped or duplicated entries");
  std::vector<std::string> names;
  for (uint32_t id : ids) {
    names.push_back(std::string(table.nameOf(ecc::symbol::Id<TestTag>{id})));
  }
  const std::vector<std::string> expected = {"apple", "kiwi", "mango", "zebra"};
  require(names == expected, "sortedIds() did not reproduce name-lexicographic order");
}

}  // namespace

int main()
{
  operatorBracketInsertsOrFetchesLikeMap();
  findDoesNotInsertAndAtThrowsOnMiss();
  referencesSurviveUnrelatedInsertionsAndGrowth();
  clearResetsToEmpty();
  idRoundTripsThroughNameOfAndGet();
  sortedIdsAreNameLexicographicRegardlessOfInsertOrder();
  return 0;
}

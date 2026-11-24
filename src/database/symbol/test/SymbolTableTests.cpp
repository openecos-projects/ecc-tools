// SPDX-License-Identifier: MulanPSL-2.0
#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "Id.h"
#include "SymbolTable.h"

using ecc::symbol::Id;
using ecc::symbol::SymbolTable;

namespace {

void require(bool condition, const char* message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void internDedupsAndAssignsDistinctIds()
{
  SymbolTable table;
  const uint32_t a1 = table.intern("alpha");
  const uint32_t b = table.intern("beta");
  const uint32_t a2 = table.intern("alpha");
  require(a1 == a2, "interning the same name twice returned different ids");
  require(a1 != b, "two distinct names collided on the same id");
  require(a1 != 0 && b != 0, "a real interned name got the reserved invalid id 0");
  require(table.size() == 2, "size() did not reflect the two distinct interned names");
}

void findDoesNotInsert()
{
  SymbolTable table;
  require(!table.find("missing").has_value(), "find() reported a name that was never interned");
  require(table.size() == 0, "find() on a miss inserted an entry");
  const uint32_t id = table.intern("present");
  const auto found = table.find("present");
  require(found.has_value() && *found == id, "find() did not return the id intern() assigned");
}

void viewRoundtripsExactBytes()
{
  SymbolTable table;
  // Deliberately include an embedded NUL: string_view must not stop at it,
  // so the probe/compare path cannot rely on null-terminated comparison.
  const std::string with_embedded_nul = std::string("a\0b", 3);
  const uint32_t id = table.intern(with_embedded_nul);
  require(table.view(id) == std::string_view(with_embedded_nul), "view() did not round-trip bytes exactly, including an embedded NUL");
}

void growthPreservesEveryEntry()
{
  SymbolTable table;
  std::vector<std::string> names;
  std::vector<uint32_t> ids;
  // Initial capacity is 16; this forces at least two rehashes.
  for (int i = 0; i < 100; ++i) {
    names.push_back("name_" + std::to_string(i));
    ids.push_back(table.intern(names.back()));
  }
  for (std::size_t i = 0; i < names.size(); ++i) {
    require(table.view(ids[i]) == names[i], "rehash corrupted a previously interned entry's bytes");
    const auto found = table.find(names[i]);
    require(found.has_value() && *found == ids[i], "rehash broke find() for a previously interned entry");
  }
  // Re-interning after growth must still dedup, not append a duplicate.
  require(table.intern(names.front()) == ids.front(), "re-interning after growth created a duplicate id");
  require(table.size() == names.size(), "growth changed the logical entry count");
}

void sortedIdsAreNameLexicographic()
{
  SymbolTable table;
  // Insertion order is scrambled on purpose: sorted_ids() must reproduce
  // byte-lexicographic order regardless of intern() call order, matching
  // the std::map<std::string, ...> iteration order every current consumer
  // relies on (see name_order_invariant_test.cpp in iSTA).
  for (const char* name : {"zebra", "mango", "apple", "kiwi"}) {
    table.intern(name);
  }
  const std::vector<uint32_t> ids = table.sorted_ids();
  require(ids.size() == 4, "sorted_ids() dropped or duplicated entries");
  std::vector<std::string_view> names;
  for (uint32_t id : ids) {
    names.push_back(table.view(id));
  }
  require(std::is_sorted(names.begin(), names.end()), "sorted_ids() did not return names in lexicographic order");
}

void idIsDomainTyped()
{
  struct PinTag;
  struct NetTag;
  static_assert(!std::is_same_v<Id<PinTag>, Id<NetTag>>, "Id<Domain> must not collapse distinct domains into the same type");
  static_assert(sizeof(Id<PinTag>) == sizeof(uint32_t), "Id<Domain> must stay a plain uint32_t wrapper");

  Id<PinTag> default_id;
  require(!default_id.valid(), "a default-constructed Id must be invalid");
  Id<PinTag> real_id{7};
  require(real_id.valid(), "an Id with a nonzero value must be valid");
  require(real_id != default_id, "a real id compared equal to the default invalid id");
  require(Id<PinTag>{7} == real_id, "two Ids with the same value did not compare equal");
}

void rekeyMovesTheBindingNotTheId()
{
  SymbolTable table;
  const uint32_t a = table.intern("alpha");
  const uint32_t b = table.intern("beta");
  table.rekey("alpha", "gamma");
  require(!table.find("alpha").has_value(), "rekey left the old name findable");
  const auto gamma = table.find("gamma");
  require(gamma.has_value() && *gamma == a, "rekey did not preserve the id");
  require(table.view(a) == "gamma", "rekey did not repoint the id's bytes");
  require(table.find("beta").has_value() && *table.find("beta") == b, "rekey disturbed an unrelated entry");
  require(table.size() == 2, "rekey changed the logical entry count");
}

void rekeyRejectsInvalidArguments()
{
  SymbolTable table;
  table.intern("alpha");
  table.intern("beta");
  bool threw_absent = false;
  try {
    table.rekey("missing", "gamma");
  } catch (const std::invalid_argument&) {
    threw_absent = true;
  }
  require(threw_absent, "rekey from an absent name did not throw");
  bool threw_duplicate = false;
  try {
    table.rekey("alpha", "beta");
  } catch (const std::invalid_argument&) {
    threw_duplicate = true;
  }
  require(threw_duplicate, "rekey onto an existing name did not throw");
  require(table.find("alpha").has_value() && table.find("beta").has_value(), "a rejected rekey still mutated the table");
}

// Backward-shift removal must not break probe chains: intern enough colliding
// and non-colliding names to span a cluster, rekey one from the middle, and
// confirm every remaining name still resolves.
void rekeyKeepsProbeChainsIntact()
{
  SymbolTable table;
  std::vector<std::string> names;
  for (int i = 0; i < 60; ++i) {
    names.push_back("net_" + std::to_string(i));
    table.intern(names.back());
  }
  table.rekey("net_30", "renamed_30");
  for (const std::string& name : names) {
    if (name == "net_30") {
      require(!table.find(name).has_value(), "rekeyed name still resolves under its old name");
      continue;
    }
    require(table.find(name).has_value(), "rekey broke resolution of an unrelated entry in the same cluster");
  }
  require(table.find("renamed_30").has_value(), "rekeyed name does not resolve under its new name");
}

}  // namespace

int main()
{
  internDedupsAndAssignsDistinctIds();
  findDoesNotInsert();
  viewRoundtripsExactBytes();
  growthPreservesEveryEntry();
  sortedIdsAreNameLexicographic();
  idIsDomainTyped();
  rekeyMovesTheBindingNotTheId();
  rekeyRejectsInvalidArguments();
  rekeyKeepsProbeChainsIntact();
  return 0;
}

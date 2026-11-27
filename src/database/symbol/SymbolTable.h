#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ecc::symbol {

// Interns strings into a dense, unpadded byte arena and hands back a 1-based
// uint32_t id. intern()/find() probe without ever constructing or copying a
// std::string: the hash index stores {hash, id} pairs and matches by
// comparing the candidate string_view directly against the arena bytes the
// id already points to. Id 0 is reserved as "not interned"; the first
// interned name gets id 1.
//
// Ordering is deliberately not tied to id order: ids are assigned in
// intern() call order, but sorted_ids() returns ids in the names' own
// byte-lexicographic order, matching the order std::map<std::string, ...>
// iteration already gives every consumer this table is meant to replace.
class SymbolTable
{
 public:
  SymbolTable();

  uint32_t intern(std::string_view name);
  // Rebinds an existing id to a new name: the id (and therefore everything
  // that holds it, e.g. a NamedTable storage slot) keeps its identity, only
  // the name the id resolves from changes. Mirrors std::map::extract() +
  // node_handle::key() mutation + insert() — renaming a key in place without
  // copying or moving the mapped value. Throws std::invalid_argument if
  // old_name is not interned or new_name already is.
  void rekey(std::string_view old_name, std::string_view new_name);
  // Removes a name's binding. The id stays allocated (ids are never reused —
  // anything already holding it, e.g. a NamedTable storage slot, keeps its
  // identity) but becomes unreachable by name; view() on it still returns its
  // last interned bytes. Returns true if the name was interned.
  bool remove(std::string_view name);
  std::optional<uint32_t> find(std::string_view name) const;
  std::string_view view(uint32_t id) const;
  // The id was interned and not removed. Stale ids can linger in containers
  // after erase(), so validity is not proof of liveness.
  bool alive(uint32_t id) const { return id > 0 && id <= _entries.size() && _entries[id - 1].live; }
  // Number of live (name-resolvable) entries.
  std::size_t size() const { return _used; }
  std::vector<uint32_t> sorted_ids() const;

 private:
  struct Entry
  {
    uint32_t offset = 0;
    uint32_t size = 0;
    bool live = true;
  };
  // id == 0 marks an empty slot.
  struct Slot
  {
    uint32_t hash = 0;
    uint32_t id = 0;
  };

  std::size_t locate(std::string_view name, uint32_t hash) const;
  void removeSlot(std::size_t slot);
  void rehash(std::size_t new_capacity);

  std::vector<char> _arena;
  std::vector<Entry> _entries;
  std::vector<Slot> _index;
  std::size_t _used = 0;
};

}  // namespace ecc::symbol

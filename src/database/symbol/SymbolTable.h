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
  std::optional<uint32_t> find(std::string_view name) const;
  std::string_view view(uint32_t id) const;
  std::size_t size() const { return _entries.size(); }
  std::vector<uint32_t> sorted_ids() const;

 private:
  struct Entry
  {
    uint32_t offset = 0;
    uint32_t size = 0;
  };
  // id == 0 marks an empty slot.
  struct Slot
  {
    uint32_t hash = 0;
    uint32_t id = 0;
  };

  std::size_t locate(std::string_view name, uint32_t hash) const;
  void rehash(std::size_t new_capacity);

  std::vector<char> _arena;
  std::vector<Entry> _entries;
  std::vector<Slot> _index;
  std::size_t _used = 0;
};

}  // namespace ecc::symbol

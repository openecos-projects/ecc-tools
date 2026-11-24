#include "SymbolTable.h"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <utility>

namespace ecc::symbol {

namespace {
constexpr std::size_t kInitialCapacity = 16;

uint32_t hashOf(std::string_view name)
{
  return static_cast<uint32_t>(std::hash<std::string_view>{}(name));
}
}  // namespace

SymbolTable::SymbolTable() : _index(kInitialCapacity) {}

std::size_t SymbolTable::locate(std::string_view name, uint32_t hash) const
{
  const std::size_t mask = _index.size() - 1;
  std::size_t slot = hash & mask;
  while (_index[slot].id != 0) {
    if (_index[slot].hash == hash && view(_index[slot].id) == name) {
      return slot;
    }
    slot = (slot + 1) & mask;
  }
  return slot;
}

void SymbolTable::rehash(std::size_t new_capacity)
{
  std::vector<Slot> next(new_capacity);
  const std::size_t mask = new_capacity - 1;
  for (const Slot& slot : _index) {
    if (slot.id == 0) {
      continue;
    }
    std::size_t probe = slot.hash & mask;
    while (next[probe].id != 0) {
      probe = (probe + 1) & mask;
    }
    next[probe] = slot;
  }
  _index = std::move(next);
}

// Knuth 6.4R backward-shift deletion: linear probing with no tombstones,
// so every following entry in the cluster that hashed at or before the hole
// must move up. Keeps probe chains intact without a deleted-slot marker.
void SymbolTable::removeSlot(std::size_t slot)
{
  const std::size_t mask = _index.size() - 1;
  std::size_t hole = slot;
  std::size_t next = (hole + 1) & mask;
  while (_index[next].id != 0) {
    const std::size_t ideal = _index[next].hash & mask;
    const bool in_range = (hole < next) ? (ideal > hole && ideal <= next) : (ideal > hole || ideal <= next);
    if (!in_range) {
      _index[hole] = _index[next];
      hole = next;
    }
    next = (next + 1) & mask;
  }
  _index[hole] = Slot{0, 0};
  --_used;
}

void SymbolTable::rekey(std::string_view old_name, std::string_view new_name)
{
  if (find(new_name).has_value()) {
    throw std::invalid_argument("SymbolTable::rekey: new name already interned");
  }
  const uint32_t old_hash = hashOf(old_name);
  const std::size_t old_slot = locate(old_name, old_hash);
  if (_index[old_slot].id == 0) {
    throw std::invalid_argument("SymbolTable::rekey: old name not interned");
  }
  const uint32_t id = _index[old_slot].id;
  removeSlot(old_slot);
  const uint32_t offset = static_cast<uint32_t>(_arena.size());
  _arena.insert(_arena.end(), new_name.begin(), new_name.end());
  _entries[id - 1] = Entry{offset, static_cast<uint32_t>(new_name.size())};
  const uint32_t new_hash = hashOf(new_name);
  const std::size_t new_slot = locate(new_name, new_hash);
  _index[new_slot] = Slot{new_hash, id};
  ++_used;
}

uint32_t SymbolTable::intern(std::string_view name)
{
  const uint32_t hash = hashOf(name);
  std::size_t slot = locate(name, hash);
  if (_index[slot].id != 0) {
    return _index[slot].id;
  }
  if ((_used + 1) * 2 > _index.size()) {
    rehash(_index.size() * 2);
    slot = locate(name, hash);
  }
  const uint32_t id = static_cast<uint32_t>(_entries.size()) + 1;
  const uint32_t offset = static_cast<uint32_t>(_arena.size());
  _arena.insert(_arena.end(), name.begin(), name.end());
  _entries.push_back({offset, static_cast<uint32_t>(name.size())});
  _index[slot] = Slot{hash, id};
  ++_used;
  return id;
}

std::optional<uint32_t> SymbolTable::find(std::string_view name) const
{
  const uint32_t hash = hashOf(name);
  const std::size_t slot = locate(name, hash);
  if (_index[slot].id == 0) {
    return std::nullopt;
  }
  return _index[slot].id;
}

std::string_view SymbolTable::view(uint32_t id) const
{
  const Entry& entry = _entries.at(static_cast<std::size_t>(id) - 1);
  return std::string_view(_arena.data() + entry.offset, entry.size);
}

std::vector<uint32_t> SymbolTable::sorted_ids() const
{
  std::vector<uint32_t> ids(_entries.size());
  for (std::size_t i = 0; i < ids.size(); ++i) {
    ids[i] = static_cast<uint32_t>(i) + 1;
  }
  std::sort(ids.begin(), ids.end(), [this](uint32_t lhs, uint32_t rhs) { return view(lhs) < view(rhs); });
  return ids;
}

}  // namespace ecc::symbol

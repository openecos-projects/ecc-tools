#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "Id.h"
#include "SymbolTable.h"

namespace ecc::symbol {

// A name-keyed table with std::map-compatible operator[]/find/at/count/contains
// semantics, backed by a SymbolTable for name<->id translation and a
// std::deque<T> for storage.
//
// std::deque is required, not std::vector: appending at the back never
// invalidates references to existing elements (the C++ standard guarantees
// this for deque, not for vector, which can reallocate and move every element
// on growth). This matches std::map's node-stability guarantee, which existing
// call sites intending to replace a std::map<std::string, T> with this table
// may already depend on (e.g. holding a T& across a loop that inserts other,
// unrelated keys).
//
// Iteration order is intentionally not exposed here as insertion order:
// callers that need map-compatible (name-lexicographic) iteration should use
// sortedIds() and index via get(), matching the order std::map<std::string, T>
// iteration already gave. There is no items()/begin()/end() yet; add one only
// once a real caller's exact iteration shape is known, rather than guessing it.
template <typename T, typename Domain>
class NamedTable
{
 public:
  using IdType = Id<Domain>;

  T& operator[](std::string_view name)
  {
    const uint32_t id = _symbols.intern(name);
    if (id > _storage.size()) {
      _storage.emplace_back();
    }
    return _storage[id - 1];
  }

  // std::map::try_emplace equivalent: inserts a default-constructed (or
  // arg-constructed) T only if the name is absent. Returns the slot's id and
  // whether an insertion happened.
  template <typename... Args>
  std::pair<IdType, bool> try_emplace(std::string_view name, Args&&... args)
  {
    const auto existing = _symbols.find(name);
    if (existing) {
      return {IdType{*existing}, false};
    }
    const uint32_t id = _symbols.intern(name);
    if (id > _storage.size()) {
      _storage.emplace_back();
    }
    _storage[id - 1] = T(std::forward<Args>(args)...);
    return {IdType{id}, true};
  }

  T* find(std::string_view name)
  {
    const auto id = _symbols.find(name);
    return id ? &_storage[*id - 1] : nullptr;
  }
  const T* find(std::string_view name) const
  {
    const auto id = _symbols.find(name);
    return id ? &_storage[*id - 1] : nullptr;
  }

  // find()'s miss sentinel, so the std::map idiom
  // `auto it = table.find(x); if (it == table.end()) ...` keeps compiling
  // unchanged on a migrated call site (only `it->second` needs to become
  // `*it`/`it->`, since find() now returns T* rather than an iterator).
  static constexpr T* end() { return nullptr; }

  T& at(std::string_view name)
  {
    T* found = find(name);
    if (found == nullptr) {
      throw std::out_of_range("NamedTable::at: name not found");
    }
    return *found;
  }
  const T& at(std::string_view name) const
  {
    const T* found = find(name);
    if (found == nullptr) {
      throw std::out_of_range("NamedTable::at: name not found");
    }
    return *found;
  }

  bool contains(std::string_view name) const { return _symbols.find(name).has_value(); }
  std::size_t count(std::string_view name) const { return contains(name) ? 1 : 0; }

  std::size_t size() const { return _storage.size(); }
  bool empty() const { return _storage.empty(); }

  void clear()
  {
    _storage.clear();
    _symbols = SymbolTable();
  }

  std::optional<IdType> id(std::string_view name) const
  {
    const auto raw = _symbols.find(name);
    return raw ? std::optional<IdType>(IdType{*raw}) : std::nullopt;
  }

  // Rebinds the entry keyed by old_name to new_name, preserving the stored
  // value's identity, id, and storage slot (std::map extract()+key mutation+
  // insert() semantics). The value's own name member, if it has one, is not
  // touched. Throws std::invalid_argument if old_name is absent or new_name
  // is already present.
  void rekey(std::string_view old_name, std::string_view new_name) { _symbols.rekey(old_name, new_name); }

  std::string_view nameOf(IdType id) const { return _symbols.view(id.value); }

  T& get(IdType id) { return _storage[id.value - 1]; }
  const T& get(IdType id) const { return _storage[id.value - 1]; }

  // Ids in name-lexicographic order, matching the order std::map<std::string, T>
  // iteration already gave every existing consumer. Pair with get(id) to
  // replace a `for (auto& [name, value] : table)` loop that only needs the
  // value, not the name.
  std::vector<IdType> sortedIds() const
  {
    std::vector<IdType> ids;
    for (uint32_t id : _symbols.sorted_ids()) {
      ids.push_back(IdType{id});
    }
    return ids;
  }

 private:
  SymbolTable _symbols;
  std::deque<T> _storage;
};

}  // namespace ecc::symbol

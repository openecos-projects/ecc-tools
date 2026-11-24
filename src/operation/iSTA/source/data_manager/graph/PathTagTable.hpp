// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/TransType.hpp"

namespace ista {

// Interns path-state tags: the composite key
// (clock, launch transition, exception state progress, clock-source-path flag,
//  CRPR anchor pin, launch clock edge index) that identifies one launch
// variant of a timing path state. Producers previously re-serialized this
// into a delimiter-joined std::string on every arc traversal; the table builds
// that canonical string once per unique key and hands out a dense uint32_t id
// for map keys.
//
// Ids are 1-based; 0 is reserved for "no tag". The canonical string form is
// byte-identical to what ExceptionMatcher::makePathState() produced before the
// interning refactor — SparseTimingExportAdapter parses that format
// (sameLaunchTag) and report/export surfaces carry it, so the format is a
// compatibility surface, not an implementation detail.
class PathTagTable
{
 public:
  struct Key
  {
    std::string clock_name;
    TransType launch_transition = TransType::kNone;
    std::vector<int32_t> exception_state_list;
    bool clock_source_path = false;
    std::string crpr_path;
    std::optional<std::size_t> launch_clock_edge_index;

    bool operator<(const Key& other) const;
  };

  uint32_t intern(const Key& key);
  std::string_view view(uint32_t id) const;
  std::size_t size() const { return _tags.size(); }

  // The canonical string form of a key, without interning it.
  static std::string canonical(const Key& key);

 private:
  std::map<Key, uint32_t> _ids;
  std::vector<std::string> _tags;
};

}  // namespace ista

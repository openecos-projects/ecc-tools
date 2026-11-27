// SPDX-License-Identifier: MulanPSL-2.0
#include "PathTagTable.hpp"

#include <tuple>

namespace ista {

bool PathTagTable::Key::operator<(const Key& other) const
{
  return std::tie(clock_name, launch_transition, exception_state_list, clock_source_path, crpr_path, launch_clock_edge_index)
         < std::tie(other.clock_name, other.launch_transition, other.exception_state_list, other.clock_source_path, other.crpr_path,
                    other.launch_clock_edge_index);
}

// Byte-identical to the string ExceptionMatcher::makePathStateTag built before
// the interning refactor; the format is parsed by the sparse exporter and
// carried on report paths.
std::string PathTagTable::canonical(const Key& key)
{
  std::string tag(key.clock_name);
  if (key.launch_clock_edge_index && *key.launch_clock_edge_index >= 2) {
    tag += "\x1b" + std::to_string(*key.launch_clock_edge_index);
  }
  if (key.clock_source_path) {
    tag += "\x1d";
  }
  if (key.launch_transition == TransType::kFall) {
    tag += "\x1e";
  }
  for (const int32_t state : key.exception_state_list) {
    tag += "\x1f" + std::to_string(state);
  }
  if (!key.crpr_path.empty()) {
    tag += "\x1c" + std::to_string(key.crpr_path.size()) + ":";
    tag += key.crpr_path;
  }
  return tag;
}

uint32_t PathTagTable::intern(const Key& key)
{
  const auto found = _ids.find(key);
  if (found != _ids.end()) {
    return found->second;
  }
  const uint32_t id = static_cast<uint32_t>(_tags.size()) + 1;
  _tags.push_back(canonical(key));
  _ids.emplace(key, id);
  return id;
}

std::string_view PathTagTable::view(uint32_t id) const
{
  return _tags.at(id - 1);
}

}  // namespace ista

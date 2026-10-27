// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "path/TimingPath.hpp"

namespace ista {
// Owns published paths, ordered first by endpoint name and then by insertion.
class PathGroup
{
 public:
  const std::string& name() const { return _name; }
  void setName(std::string_view name) { _name = name; }
  std::map<std::string, std::vector<TimingPath>>& endpointPaths() { return _endpoint_paths; }
  const std::map<std::string, std::vector<TimingPath>>& endpointPaths() const { return _endpoint_paths; }

 private:
  std::string _name;
  std::map<std::string, std::vector<TimingPath>> _endpoint_paths;
};
}  // namespace ista

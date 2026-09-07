#pragma once

#include "EXTLayerRect.hpp"
#include "LayerCoord.hpp"
#include "Segment.hpp"
#include "Violation.hpp"

namespace irt {

class DRBoxResult
{
 public:
  bool get_valid() const { return _valid; }
  std::map<int32_t, std::vector<Segment<LayerCoord>>>& get_net_own_result_map() { return _net_own_result_map; }
  std::map<int32_t, std::vector<EXTLayerRect>>& get_net_own_patch_map() { return _net_own_patch_map; }
  std::vector<Violation>& get_route_violation_list() { return _route_violation_list; }
  const std::map<int32_t, std::vector<Segment<LayerCoord>>>& get_net_own_result_map() const { return _net_own_result_map; }
  const std::map<int32_t, std::vector<EXTLayerRect>>& get_net_own_patch_map() const { return _net_own_patch_map; }
  const std::vector<Violation>& get_route_violation_list() const { return _route_violation_list; }
  void set_valid(bool valid) { _valid = valid; }

 private:
  bool _valid = false;
  std::map<int32_t, std::vector<Segment<LayerCoord>>> _net_own_result_map;
  std::map<int32_t, std::vector<EXTLayerRect>> _net_own_patch_map;
  std::vector<Violation> _route_violation_list;
};

}  // namespace irt

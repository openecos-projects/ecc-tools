#pragma once

#include "GeometryStore.h"

#include <string_view>
#include <vector>

namespace idb {
class IdbDesign;
}

namespace ecc::geometry {

class GeometryNameQuery
{
 public:
  std::vector<ShapeId> query_net_name(idb::IdbDesign& design, const GeometryStore& store, std::string_view net_name) const;
  std::vector<ShapeId> query_instance_name(idb::IdbDesign& design, const GeometryStore& store,
                                           std::string_view instance_name) const;
};

}  // namespace ecc::geometry

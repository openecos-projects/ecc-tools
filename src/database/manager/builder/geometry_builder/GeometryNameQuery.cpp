#include "GeometryNameQuery.h"

#include "IdbDesign.h"

#include <algorithm>
#include <string>

namespace ecc::geometry {
namespace {

void append_unique(std::vector<ShapeId>& result, const std::vector<ShapeId>& values)
{
  for (const ShapeId value : values) {
    if (std::find(result.begin(), result.end(), value) == result.end()) {
      result.push_back(value);
    }
  }
}

OwnerId regular_net_owner_id(idb::IdbDesign& design, idb::IdbNet* net)
{
  if (net == nullptr) {
    return 0;
  }

  if (net->get_id() != 0) {
    return net->get_id();
  }

  uint32_t net_index = 0;
  for (auto* candidate : design.get_net_list()->get_net_list()) {
    if (candidate == net) {
      return net_index;
    }
    ++net_index;
  }

  return 0;
}

OwnerId special_net_owner_id(idb::IdbDesign& design, idb::IdbSpecialNet* net)
{
  uint32_t net_index = 0;
  for (auto* candidate : design.get_special_net_list()->get_net_list()) {
    if (candidate == net) {
      return net_index;
    }
    ++net_index;
  }

  return 0;
}

}  // namespace

std::vector<ShapeId> GeometryNameQuery::query_net_name(idb::IdbDesign& design, const GeometryStore& store,
                                                       std::string_view net_name) const
{
  std::vector<ShapeId> result;
  const std::string name{net_name};

  if (auto* net_list = design.get_net_list(); net_list != nullptr) {
    if (auto* net = net_list->find_net(name); net != nullptr) {
      append_unique(result, store.query_owner(OwnerType::kNetWireSegment, regular_net_owner_id(design, net)));
    }
  }

  if (auto* special_net_list = design.get_special_net_list(); special_net_list != nullptr) {
    if (auto* net = special_net_list->find_net(name); net != nullptr) {
      append_unique(result, store.query_owner(OwnerType::kSpecialWireSegment, special_net_owner_id(design, net)));
    }
  }

  return result;
}

std::vector<ShapeId> GeometryNameQuery::query_instance_name(idb::IdbDesign& design, const GeometryStore& store,
                                                           std::string_view instance_name) const
{
  if (design.get_instance_list() == nullptr) {
    return {};
  }

  idb::IdbInstance* instance = design.get_instance_list()->find_instance(std::string{instance_name});
  if (instance == nullptr) {
    return {};
  }

  std::vector<ShapeId> result = store.query_owner(OwnerType::kInstanceBBox, instance->get_id());
  append_unique(result, store.query_owner(OwnerType::kInstanceHalo, instance->get_id()));
  return result;
}

}  // namespace ecc::geometry

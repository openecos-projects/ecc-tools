#include "py_replay.h"

#include "GeometryBuilder.h"
#include "GeometryStore.h"
#include "IdbDesign.h"
#include "IdbLayout.h"
#include "idm.h"
#include "json.hpp"

namespace python_interface {
std::string replayGeometry()
{
  using namespace ecc::geometry;
  auto* design = dmInst->get_idb_design();
  auto* layout = dmInst->get_idb_layout();
  if (!design || !layout) throw std::runtime_error("Replay requires a loaded database");
  GeometryStore store;
  GeometryBuilder().rebuild_from_design(*design, *layout, store);
  std::map<std::pair<OwnerType, OwnerId>, std::string> names;
  for (const auto& name : store.name_records()) {
    names[{name.owner_type, name.owner_id}] = std::string(
        reinterpret_cast<const char*>(store.name_payloads().data() + name.name_offset), name.name_size);
  }
  std::map<std::string, bool> macros;
  for (auto* inst : design->get_instance_list()->get_instance_list()) {
    macros[inst->get_name()] = inst->get_cell_master()->is_block();
  }
  auto objects = nlohmann::json::object();
  for (const auto& shape : store.records()) {
    const auto& owner = store.owners()[shape.owner_index];
    // Cell pin/OBS polygons move with their cells; v1 exports physical bounds.
    if (owner.type == OwnerType::kInstancePinPortShape || owner.type == OwnerType::kObs) continue;
    auto name = names[{owner.type, owner.owner_id}];
    auto kind = std::string(owner_type_label(owner.type));
    auto key = kind + ":" + name + ":" + std::to_string(owner.owner_id) + ":"
        + std::to_string(owner.path0) + ":" + std::to_string(owner.path1) + ":"
        + std::to_string(owner.path2) + ":" + std::to_string(owner.path3);
    if (owner.type == OwnerType::kDie || owner.type == OwnerType::kCore) key = kind;
    if (owner.type == OwnerType::kInstanceBBox) { key = "inst:" + name; kind = macros[name] ? "macro" : "instance"; }
    if (owner.type == OwnerType::kInstanceHalo) { key = "halo:" + name; kind = "halo"; }
    if (owner.type == OwnerType::kIoPinPortShape || owner.type == OwnerType::kPinPortShape) kind = "pin";
    if (owner.type == OwnerType::kNetWireSegment) kind = "wire";
    if (owner.type == OwnerType::kSpecialWireSegment) kind = "pdn";
    if (owner.type == OwnerType::kVia) kind = "via";
    const auto& b = shape.bbox;
    objects[key] = {{"kind", kind}, {"name", name}, {"bbox", {b.lx, b.ly, b.hx, b.hy}},
                    {"layer", std::to_string(shape.layer_id)}};
  }
  for (auto* inst : design->get_instance_list()->get_instance_list()) {
    const auto key = "inst:" + inst->get_name();
    if (!objects.contains(key)) continue;
    objects[key]["master"] = inst->get_cell_master()->get_name();
    objects[key]["orient"] = static_cast<int>(inst->get_orient());
    objects[key]["fixed"] = inst->is_fixed();
  }
  return objects.dump();
}
}

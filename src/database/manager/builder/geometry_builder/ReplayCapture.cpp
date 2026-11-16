#include "ReplayCapture.h"
#include "ReplayRoutingDelta.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>

#include "GeometryBuilder.h"
#include "GeometryStore.h"
#include "IdbDesign.h"
#include "IdbLayout.h"

namespace ecc::replay {
using Json = nlohmann::json;

bool enabled()
{
  const auto* path = std::getenv("ECC_REPLAY_CAPTURE_DIR");
  return path && *path;
}

void failure(const char* message) noexcept
{
  try {
    std::cerr << "Replay capture failed: " << message << '\n';
    if (enabled())
      std::ofstream(std::filesystem::path(std::getenv("ECC_REPLAY_CAPTURE_DIR")) / "error.json") << "{\"error\":\"native capture failed\"}";
  } catch (...) {
  }
}

Json instance(idb::IdbInstance& inst)
{
  auto* b = inst.get_bounding_box();
  Json pins = Json::object();
  for (auto* pin : inst.get_pin_list()->get_pin_list()) {
    auto* coordinate = pin->get_average_coordinate();
    pins[pin->get_pin_name()] = {{"net", pin->get_net_name()}, {"position", {coordinate->get_x(), coordinate->get_y()}}};
  }
  return {{"kind", inst.get_cell_master()->is_block() ? "macro" : "instance"},
          {"name", inst.get_name()},
          {"master", inst.get_cell_master()->get_name()},
          {"bbox", {b->get_low_x(), b->get_low_y(), b->get_high_x(), b->get_high_y()}},
          {"orient", static_cast<int>(inst.get_orient())},
          {"fixed", inst.is_fixed()},
          {"placed", inst.is_placed() || inst.is_fixed()},
          {"pins", pins}};
}

Json geometry(idb::IdbDesign& design, idb::IdbLayout& layout)
{
  using namespace ecc::geometry;
  GeometryStore store;
  GeometryBuilder().rebuild_from_design(design, layout, store);
  std::map<std::pair<OwnerType, OwnerId>, std::string> names;
  for (const auto& name : store.name_records()) {
    names[{name.owner_type, name.owner_id}]
        = std::string(reinterpret_cast<const char*>(store.name_payloads().data() + name.name_offset), name.name_size);
  }
  std::map<unsigned, std::string> layers;
  for (auto* layer : layout.get_layers()->get_layers())
    layers[layer->get_id()] = layer->get_name();
  Json objects = Json::object();
  std::map<std::string, size_t> duplicates;
  for (const auto& shape : store.records()) {
    const auto& owner = store.owners()[shape.owner_index];
    if (owner.type == OwnerType::kInstancePinPortShape || owner.type == OwnerType::kObs)
      continue;
    const auto name = names[{owner.type, owner.owner_id}];
    auto kind = std::string(owner_type_label(owner.type));
    if (owner.type == OwnerType::kInstanceBBox)
      continue;
    if (owner.type == OwnerType::kInstanceHalo)
      kind = "halo";
    if (owner.type == OwnerType::kIoPinPortShape || owner.type == OwnerType::kPinPortShape)
      kind = "pin";
    if (owner.type == OwnerType::kNetWireSegment)
      kind = "wire";
    if (owner.type == OwnerType::kSpecialWireSegment)
      kind = "pdn";
    if (owner.type == OwnerType::kVia)
      kind = "via";
    const auto& b = shape.bbox;
    Json object = {{"kind", kind},
                   {"name", name},
                   {"bbox", {b.lx, b.ly, b.hx, b.hy}},
                   {"layer", layers.count(shape.layer_id) ? layers.at(shape.layer_id) : std::to_string(shape.layer_id)}};
    if (owner.type == OwnerType::kVia) {
      // GeometryBuilder encodes the parent owner type in the high byte.
      const auto parent = static_cast<OwnerType>(owner.owner_id >> 56U);
      object["net_kind"] = parent == OwnerType::kNetWireSegment ? "signal" : parent == OwnerType::kSpecialWireSegment ? "power" : "pin";
    }
    if (shape.kind == ShapeKind::kLine && shape.payload_size == sizeof(LinePayload)) {
      LinePayload line;
      std::memcpy(&line, store.payloads().data() + shape.payload_offset, sizeof(line));
      object["segment"] = {{"begin", {line.begin.x, line.begin.y}}, {"end", {line.end.x, line.end.y}}, {"width", line.width}};
    }
    // Geometry identity preserves duplicate multiplicity without transient database IDs.
    const auto identity = object.dump();
    auto key = kind + ":" + identity + ":" + std::to_string(duplicates[identity]++);
    if (kind == "die" || kind == "core")
      key = kind;
    if (kind == "halo")
      key = "halo:" + name;
    objects[key] = std::move(object);
  }
  for (auto* inst : design.get_instance_list()->get_instance_list())
    objects["inst:" + inst->get_name()] = instance(*inst);
  return objects;
}

void sample(const std::string& phase, const std::string& subflow, Json objects, const std::string& scope, const std::string& domain,
            Json metrics) noexcept
{
  if (!enabled())
    return;
  static std::mutex mutex;
  static std::string previous;
  static RoutingDelta routing_delta;
  static uint64_t sequence = 0;
  static std::chrono::steady_clock::time_point started;
  static int64_t offset = 0;
  static std::string producer;
  std::lock_guard lock(mutex);
  const std::string directory = std::getenv("ECC_REPLAY_CAPTURE_DIR");
  try {
    if (directory != previous) {
      previous = directory;
      routing_delta.reset();
      sequence = 0;
      started = std::chrono::steady_clock::now();
      const char* anchor = std::getenv("ECC_REPLAY_OFFSET_NS");
      offset = anchor ? std::stoll(anchor) : 0;
      producer = "ecc-tools-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    }
    const auto elapsed = offset + std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count();
    Json frame = {{"sequence", sequence},         {"producer", producer},   {"elapsed_ns", elapsed}, {"phase", phase},
                  {"subflow", subflow},           {"state_domain", domain}, {"scope", scope},        {"objects", std::move(objects)},
                  {"metrics", std::move(metrics)}};
    routing_delta.compact(frame);
    auto path = std::filesystem::path(directory) / (producer + "-" + std::to_string(sequence++) + ".json");
    auto temp = path;
    temp += ".tmp";
    std::ofstream stream(temp);
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream << frame.dump();
    stream.close();
    std::filesystem::rename(temp, path);
  } catch (const std::exception& error) {
    std::cerr << "Replay capture failed: " << error.what() << '\n';
    std::ofstream(std::filesystem::path(directory) / "error.json") << "{\"error\":\"native capture failed\"}";
  }
}

void database(idb::IdbDesign& design, idb::IdbLayout& layout, const std::string& phase, const std::string& subflow) noexcept
{
  if (!enabled())
    return;
  try {
    ecc::replay::sample(phase, subflow, geometry(design, layout), "full", "native_committed");
  } catch (const std::exception& error) {
    std::cerr << "Replay database failed: " << error.what() << '\n';
    std::ofstream(std::filesystem::path(std::getenv("ECC_REPLAY_CAPTURE_DIR")) / "error.json") << "{\"error\":\"database capture failed\"}";
  }
}
}  // namespace ecc::replay

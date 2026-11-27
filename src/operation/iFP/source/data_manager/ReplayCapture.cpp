#include "ReplayCapture.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "DataManager.hpp"
#include "json.hpp"

namespace ifp {
namespace {
std::string directory;
std::chrono::steady_clock::time_point started;
int64_t offset = 0;
uint64_t sequence = 0;
using Json = nlohmann::json;
template <typename Rect> Json bounds(const Rect& rect) {
  return {rect.get_ll_x(), rect.get_ll_y(), rect.get_ur_x(), rect.get_ur_y()};
}
}  // namespace

void beginReplayCapture() {
  endReplayCapture();
  const auto* path = std::getenv("ECC_REPLAY_CAPTURE_DIR");
  if (path == nullptr || *path == '\0') return;
  try {
    directory = path;
    const auto* anchor = std::getenv("ECC_REPLAY_OFFSET_NS");
    offset = anchor ? std::stoll(anchor) : 0;
    started = std::chrono::steady_clock::now();
  } catch (const std::exception& e) {
    std::cerr << "Replay disabled: " << e.what() << '\n';
    endReplayCapture();
  }
}

void captureReplay(const std::string& phase, const std::string& subflow, const std::string& domain) {
  if (directory.empty()) return;
  try {
    auto elapsed = offset + std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count();
    auto& db = FPDM.getDatabase();
    Json objects = Json::object();
    auto put = [&](const std::string& id, const std::string& kind, Json bbox, const std::string& layer = "") {
      if (bbox[2].get<int64_t>() <= bbox[0].get<int64_t>() || bbox[3].get<int64_t>() <= bbox[1].get<int64_t>()) return;
      objects[id] = {{"kind", kind}, {"bbox", bbox}, {"layer", layer}, {"name", id}};
    };
    put("die", "die", bounds(db.get_die()));
    put("core", "core", bounds(db.get_core()));
    for (const auto& row : db.get_row_list()) {
      const auto rect = bounds(row);
      put("row:" + row.get_name() + ":" + rect.dump(), "row", rect);
    }
    for (const auto& inst : db.get_instance_list()) {
      if (!inst.get_placed() || inst.is_delete_instance()) continue;
      const auto id = "inst:" + inst.get_name();
      auto rect = bounds(inst.get_bounding_rect());
      // Newly placed physical cells have coordinates and master dimensions;
      // their cached bounding rectangle is populated only during writeback.
      if (inst.is_new_instance()) {
        auto width = inst.get_width(), height = inst.get_height();
        const auto orient = inst.get_orient();
        if (orient == PlacementOrientation::kE || orient == PlacementOrientation::kW
            || orient == PlacementOrientation::kFE || orient == PlacementOrientation::kFW) {
          std::swap(width, height);
        }
        rect = {inst.get_x(), inst.get_y(), inst.get_x() + width, inst.get_y() + height};
      }
      put(id, inst.get_macro() ? "macro" : "instance", rect);
      if (!objects.contains(id)) continue;
      objects[id]["master"] = inst.get_master_name();
      objects[id]["orient"] = static_cast<int>(inst.get_orient());
      objects[id]["fixed"] = inst.get_fixed();
      if (inst.get_macro()) {
        put("halo:" + inst.get_name(), "halo", bounds(inst.get_placement_halo_rect()));
      }
    }
    // Newly generated ports are relative to their pin/port anchor.
    for (auto& pin : db.get_io_pin_list()) {
      if (!pin.get_placed() && !pin.get_fixed()) continue;
      size_t port_id = 0;
      for (auto& port : pin.get_new_port_list()) {
        const auto x = port.get_placed() ? port.get_x() : pin.get_x();
        const auto y = port.get_placed() ? port.get_y() : pin.get_y();
        put("pin:" + pin.get_name() + ":" + std::to_string(port_id++), "pin",
            {port.get_ll_x()+x, port.get_ll_y()+y, port.get_ur_x()+x, port.get_ur_y()+y}, port.get_layer_name());
      }
    }
    std::map<std::string, int> counts;
    for (const auto& segment : db.get_pg_segment_list()) {
      // PG via envelopes are explicitly bounds, not individual cut polygons.
      Json rect = bounds(segment);
      if (segment.get_type() == PGSegmentType::kVia) {
        const int64_t x = segment.get_start_x(), y = segment.get_start_y();
        rect = {x - segment.get_via_width()/2, y - segment.get_via_height()/2,
                x + segment.get_via_width()/2, y + segment.get_via_height()/2};
      }
      auto key = segment.get_net_name() + ":" + segment.get_layer_name() + ":" + rect.dump();
      put("pg:" + key + ":" + std::to_string(counts[key]++), "pdn", rect, segment.get_layer_name());
    }
    const char* step = std::getenv("ECC_REPLAY_STEP");
    const std::string step_name = step ? step : "";
    const bool post_floorplan = step_name == "post_floorplan" || step_name == "postFloorplan";
    const std::string stage = post_floorplan && subflow == "init floorplan" ? "create tracks" : subflow;
    Json frame = {{"sequence", sequence++}, {"elapsed_ns", elapsed}, {"phase", phase},
                  {"subflow", stage}, {"state_domain", domain}, {"objects", objects},
                  {"metrics", {{"dbu_per_micron", db.get_micron_dbu()}, {"objects", objects.size()}}}};
    auto path = std::filesystem::path(directory) / ("fp-" + std::to_string(sequence) + ".json");
    auto temp = path; temp += ".tmp";
    std::ofstream out(temp); out.exceptions(std::ios::failbit | std::ios::badbit);
    out << frame.dump(); out.close(); std::filesystem::rename(temp, path);
  } catch (const std::exception& e) {
    std::cerr << "Replay capture failed: " << e.what() << '\n';
    std::ofstream(std::filesystem::path(directory) / "error.json") << "{\"error\":\"iFP capture failed\"}";
    endReplayCapture();
  }
}

void endReplayCapture() { directory.clear(); sequence = 0; offset = 0; }
}  // namespace ifp

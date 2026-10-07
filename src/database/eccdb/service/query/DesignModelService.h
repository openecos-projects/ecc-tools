// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/GeometryTypes.h"

namespace eccdb {

class Database;

// The model service deliberately uses a `(kind, id)` pair rather than a bare
// integer.  Design, library, technology, and geometry registries have
// independent ID domains, and an embedded dbITerm is not an EnTT entity at
// all.  A typed reference stays unambiguous across the Python boundary.
inline constexpr uint64_t kInvalidModelObjectId = std::numeric_limits<uint64_t>::max();

namespace ModelEntityKind {
inline constexpr std::string_view kLayer = "layer";
inline constexpr std::string_view kInstance = "instance";
inline constexpr std::string_view kMaster = "master";
inline constexpr std::string_view kMasterTerm = "mterm";
inline constexpr std::string_view kMasterPin = "mpin";
inline constexpr std::string_view kInstanceTerm = "iterm";
inline constexpr std::string_view kBlockTerm = "bterm";
inline constexpr std::string_view kBlockPin = "bpin";
inline constexpr std::string_view kNet = "net";
inline constexpr std::string_view kWire = "wire";
inline constexpr std::string_view kShape = "shape";
}  // namespace ModelEntityKind

struct ModelObjectRef
{
  std::string kind;
  uint64_t id = kInvalidModelObjectId;

  [[nodiscard]] bool valid() const noexcept { return !kind.empty() && id != kInvalidModelObjectId; }
  friend bool operator==(const ModelObjectRef&, const ModelObjectRef&) = default;
};

// A detached scalar descriptor for one EccDB object.  `list()` deliberately
// leaves `relations` empty: expanding every relationship while paginating a
// flat list would defeat lazy GUI navigation.  `describe()` populates its
// direct relationships on demand.
struct ModelEntity
{
  ModelObjectRef object;
  std::string name;
  std::optional<ModelObjectRef> parent;
  std::optional<ModelObjectRef> master;
  std::optional<ModelObjectRef> net;
  std::optional<Rect> bounds;
  std::vector<ModelObjectRef> relations;

  // These scalar fields use the storage enum representation.  They avoid
  // leaking pointers or live EnTT components through the binding boundary.
  uint64_t layer = kInvalidModelObjectId;
  uint8_t status = 0;
  uint8_t orientation = 0;
  uint8_t geometry_kind = 0;
  bool has_origin = false;
  Point origin{};
  uint32_t flags = 0;
};

struct ModelEntityPage
{
  std::vector<ModelEntity> records;
  uint64_t next_offset = 0;
  bool has_more = false;
};

// One visible technology layer.  `sequence_index` is the physical stack
// order from TechLayerSequence.  A routing record has width/pitch/direction;
// a cut record retains the same identity but leaves those values at zero.
struct ModelLayerRecord
{
  uint64_t id = kInvalidModelObjectId;
  std::string name;
  uint32_t sequence_index = 0;
  uint32_t routing_level = UINT32_MAX;
  uint32_t mask_count = 1;
  uint8_t kind = 0;
  uint8_t direction = 0;
  uint64_t flags = 0;
  int32_t width = 0;
  int32_t pitch_x = 0;
  int32_t pitch_y = 0;
  int32_t offset_x = 0;
  int32_t offset_y = 0;
};

// DEF permits one TRACKS statement to name multiple layers.  The service
// expands that source statement into one record per target layer so the view
// can efficiently render tracks for a selected layer without duplicating the
// authoritative DesignTrackGrid component.
struct ModelTrackGridRecord
{
  uint64_t source_id = kInvalidModelObjectId;
  uint64_t layer = kInvalidModelObjectId;
  uint8_t axis = 0;
  int32_t start = 0;
  uint32_t track_count = 0;
  int32_t step = 0;
  uint32_t flags = 0;
  uint32_t mask = 0;
};

// Read-only, GUI-independent lazy access to design hierarchy and technology
// metadata.  It returns value snapshots only.  It never exposes EnTT object
// addresses, callbacks, or a full database copy to Python.
class DesignModelService
{
 public:
  static constexpr std::size_t kMaxPageSize = 4096;

  explicit DesignModelService(Database& database) noexcept : _database(&database) {}

  // The offset is a short-lived cursor in current dense-storage order.  It is
  // deliberately not a persistent cursor across edits.  `limit` is capped at
  // kMaxPageSize and no total count is computed, avoiding an additional full
  // design traversal for each page.
  [[nodiscard]] ModelEntityPage list(std::string_view kind, uint64_t offset = 0, std::size_t limit = 256,
                                     std::optional<ModelObjectRef> parent = std::nullopt) const;
  [[nodiscard]] std::vector<ModelObjectRef> listIds(std::string_view kind, uint64_t offset = 0,
                                                     std::size_t limit = 256,
                                                     std::optional<ModelObjectRef> parent = std::nullopt) const;
  [[nodiscard]] std::optional<ModelEntity> describe(std::string_view kind, uint64_t id) const;
  [[nodiscard]] std::optional<ModelObjectRef> find(std::string_view kind, std::string_view name,
                                                    std::optional<ModelObjectRef> parent = std::nullopt) const;

  [[nodiscard]] std::vector<ModelLayerRecord> layers() const;
  [[nodiscard]] std::vector<ModelTrackGridRecord> trackGrids(std::optional<uint64_t> layer = std::nullopt) const;

  // Instance placement is currently represented by its master envelope, not
  // an instance R-tree.  This method is intentionally opt-in: it scans placed
  // instances (O(N)) and must not be called from every paint frame on a large
  // design.  A later instance spatial index can preserve this API.
  [[nodiscard]] ModelEntityPage listPlacedInstances(Rect viewport, uint64_t offset = 0,
                                                    std::size_t limit = 256) const;

 private:
  Database* _database;
};

}  // namespace eccdb

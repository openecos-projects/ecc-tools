#pragma once

#include "GeometryPayload.h"
#include "ShapeRecord.h"

#include <cstddef>
#include <cstring>
#include <span>
#include <unordered_map>
#include <vector>

namespace ecc::geometry {

class ShapeTable
{
 public:
  template <typename Payload>
  RecordIndex insert(ShapeRecord record, const Payload& payload)
  {
    return insert_bytes(record, &payload, sizeof(Payload));
  }

  template <typename Payload>
  bool update(ShapeId id, ShapeRecord record, const Payload& payload)
  {
    return update_bytes(id, record, &payload, sizeof(Payload));
  }

  RecordIndex insert_bytes(ShapeRecord record, const void* payload, uint32_t payload_size);
  bool update_bytes(ShapeId id, ShapeRecord record, const void* payload, uint32_t payload_size);
  bool mark_deleted(ShapeId id);
  void clear();
  bool replace_snapshot(std::vector<ShapeRecord> records, std::vector<std::byte> payloads);

  const ShapeRecord* find(ShapeId id) const;
  ShapeRecord* find_mutable(ShapeId id);
  std::span<const ShapeRecord> records() const;
  std::span<const std::byte> payloads() const;
  size_t size() const;

 private:
  void append_payload(ShapeRecord& record, const void* payload, uint32_t payload_size);

  std::vector<ShapeRecord> _records;
  std::vector<std::byte> _payloads;
  std::unordered_map<ShapeId, RecordIndex> _id_to_index;
};

}  // namespace ecc::geometry

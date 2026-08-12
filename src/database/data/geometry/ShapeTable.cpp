#include "ShapeTable.h"

#include <utility>

namespace ecc::geometry {

RecordIndex ShapeTable::insert_bytes(ShapeRecord record, const void* payload, uint32_t payload_size)
{
  record.bbox = normalize(record.bbox);
  append_payload(record, payload, payload_size);

  const RecordIndex index = static_cast<RecordIndex>(_records.size());
  _records.push_back(record);
  _id_to_index[record.id] = index;
  return index;
}

bool ShapeTable::update_bytes(ShapeId id, ShapeRecord record, const void* payload, uint32_t payload_size)
{
  ShapeRecord* existing = find_mutable(id);
  if (existing == nullptr) {
    return false;
  }

  record.id = id;
  record.bbox = normalize(record.bbox);
  append_payload(record, payload, payload_size);
  *existing = record;
  return true;
}

bool ShapeTable::mark_deleted(ShapeId id)
{
  ShapeRecord* record = find_mutable(id);
  if (record == nullptr) {
    return false;
  }

  record->state = ShapeState::kDeleted;
  ++record->version;
  return true;
}

void ShapeTable::clear()
{
  _records.clear();
  _payloads.clear();
  _id_to_index.clear();
}

bool ShapeTable::replace_snapshot(std::vector<ShapeRecord> records, std::vector<std::byte> payloads)
{
  std::unordered_map<ShapeId, RecordIndex> id_to_index;

  for (RecordIndex index = 0; index < records.size(); ++index) {
    ShapeRecord& record = records[index];
    if (record.id == 0 || id_to_index.contains(record.id)) {
      return false;
    }

    const uint64_t payload_end = record.payload_offset + record.payload_size;
    if (payload_end > payloads.size()) {
      return false;
    }

    record.bbox = normalize(record.bbox);
    id_to_index[record.id] = index;
  }

  _records = std::move(records);
  _payloads = std::move(payloads);
  _id_to_index = std::move(id_to_index);
  return true;
}

const ShapeRecord* ShapeTable::find(ShapeId id) const
{
  const auto iter = _id_to_index.find(id);
  if (iter == _id_to_index.end()) {
    return nullptr;
  }

  return &_records[iter->second];
}

ShapeRecord* ShapeTable::find_mutable(ShapeId id)
{
  const auto iter = _id_to_index.find(id);
  if (iter == _id_to_index.end()) {
    return nullptr;
  }

  return &_records[iter->second];
}

std::span<const ShapeRecord> ShapeTable::records() const
{
  return _records;
}

std::span<const std::byte> ShapeTable::payloads() const
{
  return _payloads;
}

size_t ShapeTable::size() const
{
  return _records.size();
}

void ShapeTable::append_payload(ShapeRecord& record, const void* payload, uint32_t payload_size)
{
  record.payload_offset = static_cast<uint64_t>(_payloads.size());
  record.payload_size = payload_size;

  if (payload == nullptr || payload_size == 0) {
    return;
  }

  const auto* payload_bytes = static_cast<const std::byte*>(payload);
  _payloads.insert(_payloads.end(), payload_bytes, payload_bytes + payload_size);
}

}  // namespace ecc::geometry

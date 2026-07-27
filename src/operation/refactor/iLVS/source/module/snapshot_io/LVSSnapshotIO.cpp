// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "LVSSnapshotIO.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <type_traits>

namespace ilvs {

// public

void LVSSnapshotIO::initInst()
{
  if (_sio_instance == nullptr) {
    _sio_instance = new LVSSnapshotIO();
  }
}

LVSSnapshotIO& LVSSnapshotIO::getInst()
{
  if (_sio_instance == nullptr) {
    LVSLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_sio_instance;
}

void LVSSnapshotIO::destroyInst()
{
  if (_sio_instance != nullptr) {
    delete _sio_instance;
    _sio_instance = nullptr;
  }
}

// private

LVSSnapshotIO* LVSSnapshotIO::_sio_instance = nullptr;

namespace {

constexpr std::array<char, 8> kSnapshotMagic = {'I', 'L', 'V', 'S', 'B', 'I', 'N', '\0'};
constexpr uint32_t kSnapshotVersion = 4;
constexpr uint64_t kMaxContainerSize = 100000000;
constexpr uint64_t kMaxStringSize = 16 * 1024 * 1024;
constexpr uint64_t kChecksumOffsetBasis = 14695981039346656037ULL;
constexpr uint64_t kChecksumPrime = 1099511628211ULL;

class BinaryWriter
{
 public:
  explicit BinaryWriter(const std::filesystem::path& file_path)
  {
    _stream.open(file_path, std::ios::binary | std::ios::trunc);
    if (!_stream) {
      fail("Failed to open snapshot file for writing: " + file_path.string());
    }
  }

  template <typename T>
  bool write(const T& value)
  {
    static_assert(std::is_trivially_copyable_v<T>, "Snapshot binary write requires a trivially copyable value.");
    return writeRaw(reinterpret_cast<const char*>(&value), sizeof(T));
  }

  bool writeString(const std::string& value)
  {
    if (value.size() > kMaxStringSize) {
      fail("Snapshot string exceeds the supported size.");
      return false;
    }
    return write(static_cast<uint64_t>(value.size())) && writeRaw(value.data(), static_cast<std::streamsize>(value.size()));
  }

  bool writeBytes(const char* data, std::streamsize size) { return writeRaw(data, size); }

  void beginPayload() { _checksum_enabled = true; }

  bool finishPayload()
  {
    _checksum_enabled = false;
    return write(_checksum);
  }

  bool close()
  {
    if (!_ok) {
      return false;
    }
    _stream.flush();
    if (!_stream) {
      fail("Failed to flush snapshot file.");
      return false;
    }
    _stream.close();
    return true;
  }

  bool ok() const { return _ok; }
  const std::string& error() const { return _error; }
  void setError(std::string error) { fail(std::move(error)); }

 private:
  bool writeRaw(const char* data, std::streamsize size)
  {
    if (!_ok) {
      return false;
    }
    _stream.write(data, size);
    if (!_stream) {
      fail("Failed to write snapshot file.");
      return false;
    }
    if (_checksum_enabled) {
      updateChecksum(data, size);
    }
    return true;
  }

  void updateChecksum(const char* data, std::streamsize size)
  {
    for (std::streamsize idx = 0; idx < size; idx++) {
      _checksum ^= static_cast<uint8_t>(data[idx]);
      _checksum *= kChecksumPrime;
    }
  }

  void fail(std::string error)
  {
    _ok = false;
    _error = std::move(error);
  }

  std::ofstream _stream;
  uint64_t _checksum = kChecksumOffsetBasis;
  bool _checksum_enabled = false;
  bool _ok = true;
  std::string _error;
};

class BinaryReader
{
 public:
  explicit BinaryReader(const std::filesystem::path& file_path)
  {
    _stream.open(file_path, std::ios::binary);
    if (!_stream) {
      fail("Failed to open snapshot file for reading: " + file_path.string());
    }
  }

  template <typename T>
  bool read(T& value)
  {
    static_assert(std::is_trivially_copyable_v<T>, "Snapshot binary read requires a trivially copyable value.");
    return readRaw(reinterpret_cast<char*>(&value), sizeof(T));
  }

  bool readString(std::string& value)
  {
    uint64_t size = 0;
    if (!read(size)) {
      return false;
    }
    if (size > kMaxStringSize || size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
      fail("Snapshot string size is invalid.");
      return false;
    }
    value.resize(static_cast<size_t>(size));
    return size == 0 || readRaw(value.data(), static_cast<std::streamsize>(size));
  }

  bool readBytes(char* data, std::streamsize size) { return readRaw(data, size); }

  void beginPayload() { _checksum_enabled = true; }

  bool finishPayload()
  {
    _checksum_enabled = false;
    uint64_t expected_checksum = 0;
    if (!read(expected_checksum)) {
      return false;
    }
    if (expected_checksum != _checksum) {
      fail("Snapshot checksum mismatch.");
      return false;
    }
    if (_stream.peek() != std::char_traits<char>::eof()) {
      fail("Snapshot contains trailing data.");
      return false;
    }
    return true;
  }

  bool ok() const { return _ok; }
  const std::string& error() const { return _error; }
  void setError(std::string error) { fail(std::move(error)); }

 private:
  bool readRaw(char* data, std::streamsize size)
  {
    if (!_ok) {
      return false;
    }
    _stream.read(data, size);
    if (!_stream) {
      fail("Snapshot file is truncated or unreadable.");
      return false;
    }
    if (_checksum_enabled) {
      updateChecksum(data, size);
    }
    return true;
  }

  void updateChecksum(const char* data, std::streamsize size)
  {
    for (std::streamsize idx = 0; idx < size; idx++) {
      _checksum ^= static_cast<uint8_t>(data[idx]);
      _checksum *= kChecksumPrime;
    }
  }

  void fail(std::string error)
  {
    _ok = false;
    _error = std::move(error);
  }

  std::ifstream _stream;
  uint64_t _checksum = kChecksumOffsetBasis;
  bool _checksum_enabled = false;
  bool _ok = true;
  std::string _error;
};

bool writeCount(BinaryWriter& writer, size_t size)
{
  if (size > kMaxContainerSize) {
    writer.setError("Snapshot container exceeds the supported size.");
    return false;
  }
  return writer.write(static_cast<uint64_t>(size));
}

bool readCount(BinaryReader& reader, uint64_t& size)
{
  if (!reader.read(size)) {
    return false;
  }
  if (size > kMaxContainerSize || size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    reader.setError("Snapshot container size is invalid.");
    return false;
  }
  return true;
}

bool writeStringList(BinaryWriter& writer, const std::vector<std::string>& value_list)
{
  if (!writeCount(writer, value_list.size())) {
    return false;
  }
  for (const std::string& value : value_list) {
    if (!writer.writeString(value)) {
      return false;
    }
  }
  return true;
}

bool readStringList(BinaryReader& reader, std::vector<std::string>& value_list)
{
  uint64_t size = 0;
  if (!readCount(reader, size)) {
    return false;
  }
  value_list.clear();
  value_list.reserve(static_cast<size_t>(size));
  for (uint64_t idx = 0; idx < size; idx++) {
    std::string value;
    if (!reader.readString(value)) {
      return false;
    }
    value_list.push_back(std::move(value));
  }
  return true;
}

bool writeNet(BinaryWriter& writer, const Net& net)
{
  return writer.writeString(net.name) && writeStringList(writer, net.terminal_list) && writer.write(net.wire_segment_num) && writer.write(net.via_num)
         && writer.write(net.terminal_component_num) && writer.write(net.floating_terminal_num);
}

bool readNet(BinaryReader& reader, Net& net)
{
  return reader.readString(net.name) && readStringList(reader, net.terminal_list) && reader.read(net.wire_segment_num) && reader.read(net.via_num)
         && reader.read(net.terminal_component_num) && reader.read(net.floating_terminal_num);
}

bool writeNetMap(BinaryWriter& writer, const std::unordered_map<std::string, Net>& net_map)
{
  if (!writeCount(writer, net_map.size())) {
    return false;
  }
  std::vector<std::string> net_name_list;
  net_name_list.reserve(net_map.size());
  for (const auto& [net_name, net] : net_map) {
    (void) net;
    net_name_list.push_back(net_name);
  }
  std::sort(net_name_list.begin(), net_name_list.end());
  for (const std::string& net_name : net_name_list) {
    if (!writer.writeString(net_name) || !writeNet(writer, net_map.at(net_name))) {
      return false;
    }
  }
  return true;
}

bool readNetMap(BinaryReader& reader, std::unordered_map<std::string, Net>& net_map)
{
  uint64_t size = 0;
  if (!readCount(reader, size)) {
    return false;
  }
  net_map.clear();
  net_map.reserve(static_cast<size_t>(size));
  for (uint64_t idx = 0; idx < size; idx++) {
    std::string net_name;
    Net net;
    if (!reader.readString(net_name) || !readNet(reader, net) || net.name != net_name || !net_map.emplace(net_name, std::move(net)).second) {
      return false;
    }
  }
  return true;
}

bool writeInstanceMap(BinaryWriter& writer, const std::unordered_map<std::string, Instance>& instance_map)
{
  if (!writeCount(writer, instance_map.size())) {
    return false;
  }
  std::vector<std::string> instance_name_list;
  instance_name_list.reserve(instance_map.size());
  for (const auto& [instance_name, instance] : instance_map) {
    (void) instance;
    instance_name_list.push_back(instance_name);
  }
  std::sort(instance_name_list.begin(), instance_name_list.end());
  for (const std::string& instance_name : instance_name_list) {
    const Instance& instance = instance_map.at(instance_name);
    if (!writer.writeString(instance_name) || !writer.writeString(instance.name) || !writer.writeString(instance.master_name)
        || !writeStringList(writer, instance.pin_list)) {
      return false;
    }
  }
  return true;
}

bool readInstanceMap(BinaryReader& reader, std::unordered_map<std::string, Instance>& instance_map)
{
  uint64_t size = 0;
  if (!readCount(reader, size)) {
    return false;
  }
  instance_map.clear();
  instance_map.reserve(static_cast<size_t>(size));
  for (uint64_t idx = 0; idx < size; idx++) {
    std::string instance_name;
    Instance instance;
    if (!reader.readString(instance_name) || !reader.readString(instance.name) || !reader.readString(instance.master_name)
        || !readStringList(reader, instance.pin_list) || instance.name != instance_name
        || !instance_map.emplace(instance_name, std::move(instance)).second) {
      return false;
    }
  }
  return true;
}

bool writeLogicalGraph(BinaryWriter& writer, const LogicalGraph& logical_graph)
{
  return writeInstanceMap(writer, logical_graph.instance_map) && writeStringList(writer, logical_graph.io_pin_list)
         && writer.write(logical_graph.net_edge_num);
}

bool readLogicalGraph(BinaryReader& reader, LogicalGraph& logical_graph)
{
  return readInstanceMap(reader, logical_graph.instance_map) && readStringList(reader, logical_graph.io_pin_list)
         && reader.read(logical_graph.net_edge_num);
}

bool writeStringSet(BinaryWriter& writer, const std::unordered_set<std::string>& value_set)
{
  std::vector<std::string> value_list(value_set.begin(), value_set.end());
  std::sort(value_list.begin(), value_list.end());
  return writeStringList(writer, value_list);
}

bool readStringSet(BinaryReader& reader, std::unordered_set<std::string>& value_set)
{
  std::vector<std::string> value_list;
  if (!readStringList(reader, value_list)) {
    return false;
  }
  value_set.clear();
  value_set.reserve(value_list.size());
  for (std::string& value : value_list) {
    if (!value_set.emplace(std::move(value)).second) {
      return false;
    }
  }
  return true;
}

bool writeStringMap(BinaryWriter& writer, const std::unordered_map<std::string, std::string>& value_map)
{
  if (!writeCount(writer, value_map.size())) {
    return false;
  }
  std::vector<std::string> key_list;
  key_list.reserve(value_map.size());
  for (const auto& [key, value] : value_map) {
    (void) value;
    key_list.push_back(key);
  }
  std::sort(key_list.begin(), key_list.end());
  for (const std::string& key : key_list) {
    if (!writer.writeString(key) || !writer.writeString(value_map.at(key))) {
      return false;
    }
  }
  return true;
}

bool readStringMap(BinaryReader& reader, std::unordered_map<std::string, std::string>& value_map)
{
  uint64_t size = 0;
  if (!readCount(reader, size)) {
    return false;
  }
  value_map.clear();
  value_map.reserve(static_cast<size_t>(size));
  for (uint64_t idx = 0; idx < size; idx++) {
    std::string key;
    std::string value;
    if (!reader.readString(key) || !reader.readString(value) || !value_map.emplace(std::move(key), std::move(value)).second) {
      return false;
    }
  }
  return true;
}

bool writeStringVectorMap(BinaryWriter& writer, const std::unordered_map<uint64_t, std::vector<std::string>>& value_map)
{
  if (!writeCount(writer, value_map.size())) {
    return false;
  }
  std::vector<uint64_t> key_list;
  key_list.reserve(value_map.size());
  for (const auto& [key, value] : value_map) {
    (void) value;
    key_list.push_back(key);
  }
  std::sort(key_list.begin(), key_list.end());
  for (uint64_t key : key_list) {
    if (!writer.write(key) || !writeStringList(writer, value_map.at(key))) {
      return false;
    }
  }
  return true;
}

bool readStringVectorMap(BinaryReader& reader, std::unordered_map<uint64_t, std::vector<std::string>>& value_map)
{
  uint64_t size = 0;
  if (!readCount(reader, size)) {
    return false;
  }
  value_map.clear();
  value_map.reserve(static_cast<size_t>(size));
  for (uint64_t idx = 0; idx < size; idx++) {
    uint64_t key = 0;
    std::vector<std::string> value_list;
    if (!reader.read(key) || !readStringList(reader, value_list) || !value_map.emplace(key, std::move(value_list)).second) {
      return false;
    }
  }
  return true;
}

bool writeShapeLocation(BinaryWriter& writer, const ShapeLocation& shape)
{
  return writer.write(shape.layer_id) && writer.write(shape.ll_x) && writer.write(shape.ll_y) && writer.write(shape.ur_x) && writer.write(shape.ur_y);
}

bool readShapeLocation(BinaryReader& reader, ShapeLocation& shape)
{
  return reader.read(shape.layer_id) && reader.read(shape.ll_x) && reader.read(shape.ll_y) && reader.read(shape.ur_x) && reader.read(shape.ur_y);
}

bool writeSupplyRouteShapeList(BinaryWriter& writer, const std::vector<SupplyRouteShape>& shape_list)
{
  if (!writeCount(writer, shape_list.size())) {
    return false;
  }
  for (const SupplyRouteShape& route_shape : shape_list) {
    if (!writer.writeString(route_shape.net_name) || !writer.write(route_shape.component_id) || !writer.write(route_shape.layer_order)
        || !writeShapeLocation(writer, route_shape.shape)) {
      return false;
    }
  }
  return true;
}

bool readSupplyRouteShapeList(BinaryReader& reader, std::vector<SupplyRouteShape>& shape_list)
{
  uint64_t size = 0;
  if (!readCount(reader, size)) {
    return false;
  }
  shape_list.clear();
  shape_list.reserve(static_cast<size_t>(size));
  for (uint64_t idx = 0; idx < size; idx++) {
    SupplyRouteShape route_shape;
    if (!reader.readString(route_shape.net_name) || !reader.read(route_shape.component_id) || !reader.read(route_shape.layer_order)
        || !readShapeLocation(reader, route_shape.shape)) {
      return false;
    }
    shape_list.push_back(std::move(route_shape));
  }
  return true;
}

bool writeShapeMap(BinaryWriter& writer, const std::unordered_map<uint64_t, std::vector<ShapeLocation>>& shape_map)
{
  if (!writeCount(writer, shape_map.size())) {
    return false;
  }
  std::vector<uint64_t> key_list;
  key_list.reserve(shape_map.size());
  for (const auto& [key, shape_list] : shape_map) {
    (void) shape_list;
    key_list.push_back(key);
  }
  std::sort(key_list.begin(), key_list.end());
  for (uint64_t key : key_list) {
    const auto& shape_list = shape_map.at(key);
    if (!writer.write(key) || !writeCount(writer, shape_list.size())) {
      return false;
    }
    for (const ShapeLocation& shape : shape_list) {
      if (!writeShapeLocation(writer, shape)) {
        return false;
      }
    }
  }
  return true;
}

bool readShapeMap(BinaryReader& reader, std::unordered_map<uint64_t, std::vector<ShapeLocation>>& shape_map)
{
  uint64_t size = 0;
  if (!readCount(reader, size)) {
    return false;
  }
  shape_map.clear();
  shape_map.reserve(static_cast<size_t>(size));
  for (uint64_t idx = 0; idx < size; idx++) {
    uint64_t key = 0;
    uint64_t shape_size = 0;
    if (!reader.read(key) || !readCount(reader, shape_size)) {
      return false;
    }
    std::vector<ShapeLocation> shape_list;
    shape_list.reserve(static_cast<size_t>(shape_size));
    for (uint64_t shape_idx = 0; shape_idx < shape_size; shape_idx++) {
      ShapeLocation shape;
      if (!readShapeLocation(reader, shape)) {
        return false;
      }
      shape_list.push_back(shape);
    }
    if (!shape_map.emplace(key, std::move(shape_list)).second) {
      return false;
    }
  }
  return true;
}

bool writeShapeList(BinaryWriter& writer, const std::vector<ShapeLocation>& shape_list)
{
  if (!writeCount(writer, shape_list.size())) {
    return false;
  }
  for (const ShapeLocation& shape : shape_list) {
    if (!writeShapeLocation(writer, shape)) {
      return false;
    }
  }
  return true;
}

bool readShapeList(BinaryReader& reader, std::vector<ShapeLocation>& shape_list)
{
  uint64_t size = 0;
  if (!readCount(reader, size)) {
    return false;
  }
  shape_list.clear();
  shape_list.reserve(static_cast<size_t>(size));
  for (uint64_t idx = 0; idx < size; idx++) {
    ShapeLocation shape;
    if (!readShapeLocation(reader, shape)) {
      return false;
    }
    shape_list.push_back(shape);
  }
  return true;
}

bool writeIndexList(BinaryWriter& writer, const std::vector<uint64_t>& index_list)
{
  if (!writeCount(writer, index_list.size())) {
    return false;
  }
  for (uint64_t index : index_list) {
    if (!writer.write(index)) {
      return false;
    }
  }
  return true;
}

bool readIndexList(BinaryReader& reader, std::vector<uint64_t>& index_list)
{
  uint64_t size = 0;
  if (!readCount(reader, size)) {
    return false;
  }
  index_list.clear();
  index_list.reserve(static_cast<size_t>(size));
  for (uint64_t idx = 0; idx < size; idx++) {
    uint64_t index = 0;
    if (!reader.read(index)) {
      return false;
    }
    index_list.push_back(index);
  }
  return true;
}

bool writeIndexPairList(BinaryWriter& writer, const std::vector<std::pair<uint64_t, uint64_t>>& index_pair_list)
{
  if (!writeCount(writer, index_pair_list.size())) {
    return false;
  }
  for (const auto& [first_index, second_index] : index_pair_list) {
    if (!writer.write(first_index) || !writer.write(second_index)) {
      return false;
    }
  }
  return true;
}

bool readIndexPairList(BinaryReader& reader, std::vector<std::pair<uint64_t, uint64_t>>& index_pair_list)
{
  uint64_t size = 0;
  if (!readCount(reader, size)) {
    return false;
  }
  index_pair_list.clear();
  index_pair_list.reserve(static_cast<size_t>(size));
  for (uint64_t idx = 0; idx < size; idx++) {
    uint64_t first_index = 0;
    uint64_t second_index = 0;
    if (!reader.read(first_index) || !reader.read(second_index)) {
      return false;
    }
    index_pair_list.emplace_back(first_index, second_index);
  }
  return true;
}

bool writeTerminalShapeMap(BinaryWriter& writer, const std::unordered_map<std::string, std::vector<uint64_t>>& terminal_shape_map)
{
  if (!writeCount(writer, terminal_shape_map.size())) {
    return false;
  }
  std::vector<std::string> terminal_name_list;
  terminal_name_list.reserve(terminal_shape_map.size());
  for (const auto& [terminal_name, shape_index_list] : terminal_shape_map) {
    (void) shape_index_list;
    terminal_name_list.push_back(terminal_name);
  }
  std::sort(terminal_name_list.begin(), terminal_name_list.end());
  for (const std::string& terminal_name : terminal_name_list) {
    if (!writer.writeString(terminal_name) || !writeIndexList(writer, terminal_shape_map.at(terminal_name))) {
      return false;
    }
  }
  return true;
}

bool readTerminalShapeMap(BinaryReader& reader, std::unordered_map<std::string, std::vector<uint64_t>>& terminal_shape_map)
{
  uint64_t size = 0;
  if (!readCount(reader, size)) {
    return false;
  }
  terminal_shape_map.clear();
  terminal_shape_map.reserve(static_cast<size_t>(size));
  for (uint64_t idx = 0; idx < size; idx++) {
    std::string terminal_name;
    std::vector<uint64_t> shape_index_list;
    if (!reader.readString(terminal_name) || !readIndexList(reader, shape_index_list)
        || !terminal_shape_map.emplace(std::move(terminal_name), std::move(shape_index_list)).second) {
      return false;
    }
  }
  return true;
}

bool writeNetRoutingGraphMap(BinaryWriter& writer, const std::unordered_map<std::string, NetRoutingGraph>& net_routing_graph_map)
{
  if (!writeCount(writer, net_routing_graph_map.size())) {
    return false;
  }
  std::vector<std::string> net_name_list;
  net_name_list.reserve(net_routing_graph_map.size());
  for (const auto& [net_name, routing_graph] : net_routing_graph_map) {
    (void) routing_graph;
    net_name_list.push_back(net_name);
  }
  std::sort(net_name_list.begin(), net_name_list.end());
  for (const std::string& net_name : net_name_list) {
    const NetRoutingGraph& routing_graph = net_routing_graph_map.at(net_name);
    if (!writer.writeString(net_name) || !writer.writeString(routing_graph.driver_terminal_name)
        || !writeShapeList(writer, routing_graph.shape_list) || !writeIndexPairList(writer, routing_graph.via_shape_pair_list)
        || !writeTerminalShapeMap(writer, routing_graph.terminal_shape_map)) {
      return false;
    }
  }
  return true;
}

bool readNetRoutingGraphMap(BinaryReader& reader, std::unordered_map<std::string, NetRoutingGraph>& net_routing_graph_map)
{
  uint64_t size = 0;
  if (!readCount(reader, size)) {
    return false;
  }
  net_routing_graph_map.clear();
  net_routing_graph_map.reserve(static_cast<size_t>(size));
  for (uint64_t idx = 0; idx < size; idx++) {
    std::string net_name;
    NetRoutingGraph routing_graph;
    if (!reader.readString(net_name) || !reader.readString(routing_graph.driver_terminal_name)
        || !readShapeList(reader, routing_graph.shape_list) || !readIndexPairList(reader, routing_graph.via_shape_pair_list)
        || !readTerminalShapeMap(reader, routing_graph.terminal_shape_map)
        || !net_routing_graph_map.emplace(std::move(net_name), std::move(routing_graph)).second) {
      return false;
    }
  }
  return true;
}

bool writeTerminalComponentMap(BinaryWriter& writer, const std::unordered_map<std::string, uint64_t>& terminal_component_map)
{
  if (!writeCount(writer, terminal_component_map.size())) {
    return false;
  }
  std::vector<std::string> terminal_name_list;
  terminal_name_list.reserve(terminal_component_map.size());
  for (const auto& [terminal_name, component_id] : terminal_component_map) {
    (void) component_id;
    terminal_name_list.push_back(terminal_name);
  }
  std::sort(terminal_name_list.begin(), terminal_name_list.end());
  for (const std::string& terminal_name : terminal_name_list) {
    if (!writer.writeString(terminal_name) || !writer.write(terminal_component_map.at(terminal_name))) {
      return false;
    }
  }
  return true;
}

bool readTerminalComponentMap(BinaryReader& reader, std::unordered_map<std::string, uint64_t>& terminal_component_map)
{
  uint64_t size = 0;
  if (!readCount(reader, size)) {
    return false;
  }
  terminal_component_map.clear();
  terminal_component_map.reserve(static_cast<size_t>(size));
  for (uint64_t idx = 0; idx < size; idx++) {
    std::string terminal_name;
    uint64_t component_id = 0;
    if (!reader.readString(terminal_name) || !reader.read(component_id)
        || !terminal_component_map.emplace(std::move(terminal_name), component_id).second) {
      return false;
    }
  }
  return true;
}

bool writePhysicalGraph(BinaryWriter& writer, const PhysicalGraph& physical_graph)
{
  return writer.write(physical_graph.node_num) && writer.write(physical_graph.edge_num) && writer.write(physical_graph.candidate_pair_num)
         && writer.write(physical_graph.max_active_shape_num) && writer.write(physical_graph.component_num) && writer.write(physical_graph.short_component_num)
         && writer.write(physical_graph.power_port_num) && writer.write(physical_graph.floating_power_port_num) && writer.write(physical_graph.ground_port_num)
         && writer.write(physical_graph.floating_ground_port_num) && writer.write(physical_graph.power_pin_num)
         && writer.write(physical_graph.floating_power_pin_num) && writer.write(physical_graph.ground_pin_num)
         && writer.write(physical_graph.floating_ground_pin_num) && writeStringList(writer, physical_graph.floating_power_port_list)
         && writeStringList(writer, physical_graph.floating_ground_port_list) && writeStringList(writer, physical_graph.floating_power_pin_list)
         && writeStringList(writer, physical_graph.floating_ground_pin_list) && writeStringVectorMap(writer, physical_graph.component_terminal_map)
         && writeStringVectorMap(writer, physical_graph.component_net_map) && writeShapeMap(writer, physical_graph.component_shape_map)
         && writeTerminalComponentMap(writer, physical_graph.terminal_component_map) && writeStringSet(writer, physical_graph.power_net_set)
         && writeStringSet(writer, physical_graph.ground_net_set) && writeStringMap(writer, physical_graph.power_instance_pin_net_map)
         && writeStringMap(writer, physical_graph.ground_instance_pin_net_map) && writeSupplyRouteShapeList(writer, physical_graph.supply_route_shape_list)
         && writeInstanceMap(writer, physical_graph.instance_map)
         && writeStringList(writer, physical_graph.io_pin_list) && writeNetRoutingGraphMap(writer, physical_graph.net_routing_graph_map);
}

bool readPhysicalGraph(BinaryReader& reader, PhysicalGraph& physical_graph)
{
  return reader.read(physical_graph.node_num) && reader.read(physical_graph.edge_num) && reader.read(physical_graph.candidate_pair_num)
         && reader.read(physical_graph.max_active_shape_num) && reader.read(physical_graph.component_num) && reader.read(physical_graph.short_component_num)
         && reader.read(physical_graph.power_port_num) && reader.read(physical_graph.floating_power_port_num) && reader.read(physical_graph.ground_port_num)
         && reader.read(physical_graph.floating_ground_port_num) && reader.read(physical_graph.power_pin_num)
         && reader.read(physical_graph.floating_power_pin_num) && reader.read(physical_graph.ground_pin_num)
         && reader.read(physical_graph.floating_ground_pin_num) && readStringList(reader, physical_graph.floating_power_port_list)
         && readStringList(reader, physical_graph.floating_ground_port_list) && readStringList(reader, physical_graph.floating_power_pin_list)
         && readStringList(reader, physical_graph.floating_ground_pin_list) && readStringVectorMap(reader, physical_graph.component_terminal_map)
         && readStringVectorMap(reader, physical_graph.component_net_map) && readShapeMap(reader, physical_graph.component_shape_map)
         && readTerminalComponentMap(reader, physical_graph.terminal_component_map) && readStringSet(reader, physical_graph.power_net_set)
         && readStringSet(reader, physical_graph.ground_net_set) && readStringMap(reader, physical_graph.power_instance_pin_net_map)
         && readStringMap(reader, physical_graph.ground_instance_pin_net_map) && readSupplyRouteShapeList(reader, physical_graph.supply_route_shape_list)
         && readInstanceMap(reader, physical_graph.instance_map)
         && readStringList(reader, physical_graph.io_pin_list) && readNetRoutingGraphMap(reader, physical_graph.net_routing_graph_map);
}

bool writeNetlist(BinaryWriter& writer, const Netlist& netlist, LVSSnapshotType snapshot_type)
{
  if (!writer.writeString(netlist.design_name) || !writeNetMap(writer, netlist.net_map)) {
    return false;
  }
  switch (snapshot_type) {
    case LVSSnapshotType::kLogical:
      return writeLogicalGraph(writer, netlist.logical_graph);
    case LVSSnapshotType::kPhysical:
      return writePhysicalGraph(writer, netlist.physical_graph);
  }
  return false;
}

bool readNetlist(BinaryReader& reader, Netlist& netlist, LVSSnapshotType snapshot_type)
{
  if (!reader.readString(netlist.design_name) || !readNetMap(reader, netlist.net_map)) {
    return false;
  }
  switch (snapshot_type) {
    case LVSSnapshotType::kLogical:
      return readLogicalGraph(reader, netlist.logical_graph);
    case LVSSnapshotType::kPhysical:
      return readPhysicalGraph(reader, netlist.physical_graph);
  }
  return false;
}

bool isValidSnapshotType(uint32_t raw_type)
{
  return raw_type == static_cast<uint32_t>(LVSSnapshotType::kLogical) || raw_type == static_cast<uint32_t>(LVSSnapshotType::kPhysical);
}

}  // namespace

#if 1  // snapshot

bool LVSSnapshotIO::write(const Netlist& netlist, LVSSnapshotType snapshot_type, const std::string& file_path, std::string& error_message)
{
  error_message.clear();
  if (!isValidSnapshotType(static_cast<uint32_t>(snapshot_type))) {
    error_message = "Unsupported iLVS snapshot type.";
    return false;
  }
  if (file_path.empty()) {
    error_message = "Snapshot file path is empty.";
    return false;
  }

  std::filesystem::path snapshot_path(file_path);
  std::filesystem::path temporary_path = snapshot_path;
  temporary_path += ".tmp";
  std::error_code system_error;
  if (!snapshot_path.parent_path().empty()) {
    std::filesystem::create_directories(snapshot_path.parent_path(), system_error);
    if (system_error) {
      error_message = "Failed to create snapshot directory: " + system_error.message();
      return false;
    }
  }
  std::filesystem::remove(temporary_path, system_error);
  system_error.clear();

  {
    BinaryWriter writer(temporary_path);
    if (!writer.writeBytes(kSnapshotMagic.data(), static_cast<std::streamsize>(kSnapshotMagic.size())) || !writer.write(kSnapshotVersion)
        || !writer.write(static_cast<uint32_t>(snapshot_type))) {
      error_message = writer.error();
    } else {
      writer.beginPayload();
      if (!writeNetlist(writer, netlist, snapshot_type) || !writer.finishPayload() || !writer.close()) {
        error_message = writer.error().empty() ? "Failed to write iLVS snapshot payload." : writer.error();
      }
    }
  }
  if (!error_message.empty()) {
    std::filesystem::remove(temporary_path, system_error);
    return false;
  }

  std::filesystem::rename(temporary_path, snapshot_path, system_error);
  if (system_error) {
    error_message = "Failed to replace snapshot file: " + system_error.message();
    std::filesystem::remove(temporary_path, system_error);
    return false;
  }
  return true;
}

bool LVSSnapshotIO::read(const std::string& file_path, LVSSnapshotType expected_snapshot_type, Netlist& netlist, std::string& error_message)
{
  error_message.clear();
  if (!isValidSnapshotType(static_cast<uint32_t>(expected_snapshot_type))) {
    error_message = "Unsupported expected iLVS snapshot type.";
    return false;
  }
  if (file_path.empty()) {
    error_message = "Snapshot file path is empty.";
    return false;
  }

  BinaryReader reader(file_path);
  std::array<char, kSnapshotMagic.size()> magic{};
  uint32_t version = 0;
  uint32_t raw_snapshot_type = 0;
  if (!reader.readBytes(magic.data(), static_cast<std::streamsize>(magic.size())) || !reader.read(version) || !reader.read(raw_snapshot_type)) {
    error_message = reader.error();
    return false;
  }
  if (magic != kSnapshotMagic) {
    error_message = "Invalid iLVS snapshot magic: " + file_path;
    return false;
  }
  if (version != kSnapshotVersion) {
    error_message = "Unsupported iLVS snapshot version: " + std::to_string(version);
    return false;
  }
  if (!isValidSnapshotType(raw_snapshot_type) || raw_snapshot_type != static_cast<uint32_t>(expected_snapshot_type)) {
    error_message = "Unexpected iLVS snapshot type: " + file_path;
    return false;
  }

  Netlist loaded_netlist;
  reader.beginPayload();
  if (!readNetlist(reader, loaded_netlist, expected_snapshot_type) || !reader.finishPayload()) {
    error_message = reader.error().empty() ? "Invalid iLVS snapshot payload." : reader.error();
    return false;
  }
  netlist = std::move(loaded_netlist);
  return true;
}

#endif

}  // namespace ilvs

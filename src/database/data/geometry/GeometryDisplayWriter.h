#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "json.hpp"

namespace ecc::geometry {

// ECOSDSP1: portable LE header, bounded metadata, then fixed cells or indexed
// MessagePack records. Writers consume native values; no report parser is used.
class GeometryDisplayWriter
{
 public:
  enum class Encoding : uint32_t
  {
    kCells = 1,
    kRecords = 2
  };

  GeometryDisplayWriter(const std::filesystem::path& path, nlohmann::json metadata, Encoding encoding)
      : _path(path), _temporary(path.string() + ".tmp"), _encoding(encoding)
  {
    for (const auto* field : {"kind", "key", "label"}) {
      if (!metadata.contains(field) || !metadata[field].is_string() || metadata[field].get<std::string>().empty())
        throw std::runtime_error("Missing geometry display identity");
    }
    const std::string text = metadata.dump();
    if (text.size() > 1024 * 1024)
      throw std::runtime_error("Geometry display metadata exceeds 1 MiB");
    _metadata_size = static_cast<uint32_t>(text.size());
    std::filesystem::create_directories(path.parent_path());
    _output.exceptions(std::ios::badbit | std::ios::failbit);
    try {
      _output.open(_temporary, std::ios::binary | std::ios::trunc);
      header(0);
      _output.write(text.data(), text.size());
      _data_offset = static_cast<uint64_t>(_output.tellp());
    } catch (...) {
      _output.exceptions(std::ios::goodbit);
      _output.close();
      std::error_code error;
      std::filesystem::remove(_temporary, error);
      throw;
    }
  }

  GeometryDisplayWriter(const GeometryDisplayWriter&) = delete;
  GeometryDisplayWriter& operator=(const GeometryDisplayWriter&) = delete;

  ~GeometryDisplayWriter()
  {
    if (!_finished) {
      _output.exceptions(std::ios::goodbit);
      _output.close();
      std::error_code error;
      std::filesystem::remove(_temporary, error);
    }
  }

  void cell(uint32_t row, uint32_t col, const std::array<int32_t, 4>& bounds, double value)
  {
    if (_encoding != Encoding::kCells || !std::isfinite(value) || bounds[0] > bounds[2] || bounds[1] > bounds[3])
      throw std::runtime_error("Invalid geometry display cell");
    integer(row, 4);
    integer(col, 4);
    for (int32_t coordinate : bounds)
      integer(static_cast<uint32_t>(coordinate), 4);
    uint64_t bits;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    integer(bits, 8);
    ++_count;
  }

  void record(const nlohmann::json& value)
  {
    if (_encoding != Encoding::kRecords)
      throw std::runtime_error("Geometry display encoding mismatch");
    auto bytes = nlohmann::json::to_msgpack(value);
    if (bytes.size() > 64 * 1024 * 1024)
      throw std::runtime_error("Geometry display record exceeds 64 MiB");
    _offsets.push_back(static_cast<uint64_t>(_output.tellp()) - _data_offset);
    _output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    ++_count;
  }

  void finish()
  {
    if (_finished)
      throw std::runtime_error("Geometry display already published");
    uint64_t index = 0;
    if (_encoding == Encoding::kRecords) {
      index = static_cast<uint64_t>(_output.tellp());
      _offsets.push_back(index - _data_offset);
      for (uint64_t offset : _offsets)
        integer(offset, 8);
    }
    _output.seekp(0);
    header(index);
    _output.close();
    std::filesystem::rename(_temporary, _path);
    _finished = true;
  }

 private:
  void integer(uint64_t value, unsigned bytes)
  {
    for (unsigned i = 0; i < bytes; ++i)
      _output.put(static_cast<char>((value >> (8 * i)) & 0xff));
  }
  void header(uint64_t index)
  {
    _output.write("ECOSDSP1", 8);
    integer(1, 4);
    integer(static_cast<uint32_t>(_encoding), 4);
    integer(_metadata_size, 4);
    integer(_encoding == Encoding::kCells ? 32 : 0, 4);
    integer(_count, 8);
    integer(index, 8);
  }

  std::filesystem::path _path;
  std::filesystem::path _temporary;
  Encoding _encoding;
  std::ofstream _output;
  uint32_t _metadata_size = 0;
  uint64_t _data_offset = 0;
  uint64_t _count = 0;
  std::vector<uint64_t> _offsets;
  bool _finished = false;
};

}  // namespace ecc::geometry

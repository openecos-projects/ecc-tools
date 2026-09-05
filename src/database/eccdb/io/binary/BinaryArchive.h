#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <boost/pfr/core.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <istream>
#include <limits>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "common/EnttId.h"

namespace eccdb::binary_detail {

template <typename>
inline constexpr bool kUnsupportedArchiveType = false;

template <typename T>
struct IsVector : std::false_type
{
};

template <typename T, typename Allocator>
struct IsVector<std::vector<T, Allocator>> : std::true_type
{
};

template <typename T>
struct IsOptional : std::false_type
{
};

template <typename T>
struct IsOptional<std::optional<T>> : std::true_type
{
};

template <typename T>
struct IsArray : std::false_type
{
};

template <typename T, std::size_t Size>
struct IsArray<std::array<T, Size>> : std::true_type
{
};

template <typename T>
struct IsEnttId : std::false_type
{
};

template <typename Entity, typename Component>
struct IsEnttId<EnttId<Entity, Component>> : std::true_type
{
};

template <typename Archive, typename Value>
concept HasCustomBinaryArchive = requires(Archive& archive, Value& value) { binaryArchive(archive, value); };

template <typename Value>
concept ByteCompatibleArchiveValue
    = std::is_trivially_copyable_v<Value> && std::has_unique_object_representations_v<Value>;

// Fixed little-endian archive used by EnTT snapshot. Boost.PFR only handles
// aggregate field traversal; containers and stable IDs keep explicit wire
// encodings here.
class BinaryOutputArchive
{
 public:
  explicit BinaryOutputArchive(std::ostream& output) : _output(output) {}

  template <typename... Values>
  void operator()(const Values&... values)
  {
    (write(values), ...);
  }

  template <typename Value>
  void writeSequence(std::span<const Value> values)
  {
    write(static_cast<uint64_t>(values.size()));
    for (const auto& value : values) {
      write(value);
    }
  }

  // The caller must additionally prove that Value's object representation is
  // the same as its field-wise archive representation on a little-endian
  // host. The constraints reject padding and non-trivial object lifetimes;
  // big-endian builds preserve the wire format through the generic fallback.
  template <ByteCompatibleArchiveValue Value>
  void writeByteCompatibleSequence(std::span<const Value> values)
  {
    write(static_cast<uint64_t>(values.size()));
    if constexpr (std::endian::native == std::endian::little) {
      if (values.size() > std::numeric_limits<std::size_t>::max() / sizeof(Value)) {
        throw std::length_error("binary sequence byte size exceeds host limits");
      }
      writeBytes(values.data(), values.size() * sizeof(Value));
    } else {
      for (const auto& value : values) {
        write(value);
      }
    }
  }

  void flush()
  {
    if (_buffered_bytes == 0u) return;
    _output.write(_buffer.data(), static_cast<std::streamsize>(_buffered_bytes));
    if (!_output) throw std::runtime_error("failed to write binary archive");
    _buffered_bytes = 0u;
  }

  [[nodiscard]] uint64_t byteCount() const noexcept { return _byte_count; }

 private:
  void writeBytes(const void* data, std::size_t size)
  {
    const auto* source = static_cast<const char*>(data);
    _byte_count += size;
    while (size != 0u) {
      if (_buffered_bytes == 0u && size >= _buffer.size()) {
        const auto count = std::min(size, static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()));
        _output.write(source, static_cast<std::streamsize>(count));
        if (!_output) throw std::runtime_error("failed to write binary archive");
        source += count;
        size -= count;
        continue;
      }
      const auto available = _buffer.size() - _buffered_bytes;
      const auto count = std::min(size, available);
      std::memcpy(_buffer.data() + _buffered_bytes, source, count);
      source += count;
      size -= count;
      _buffered_bytes += count;
      if (_buffered_bytes == _buffer.size()) flush();
    }
  }

  template <typename Value>
  void writeInteger(Value value)
  {
    using Unsigned = std::make_unsigned_t<Value>;
    const auto bits = static_cast<Unsigned>(value);
    if constexpr (std::endian::native == std::endian::little) {
      const auto available = _buffer.size() - _buffered_bytes;
      if (available >= sizeof(bits)) {
        std::memcpy(_buffer.data() + _buffered_bytes, &bits, sizeof(bits));
        _buffered_bytes += sizeof(bits);
        _byte_count += sizeof(bits);
        if (_buffered_bytes == _buffer.size()) flush();
      } else {
        writeBytes(&bits, sizeof(bits));
      }
    } else {
      std::array<uint8_t, sizeof(Value)> bytes{};
      for (std::size_t index = 0; index < sizeof(Value); ++index) {
        bytes[index] = static_cast<uint8_t>(bits >> (index * 8u));
      }
      writeBytes(bytes.data(), bytes.size());
    }
  }

  template <typename Value>
  void write(const Value& value)
  {
    using Type = std::remove_cv_t<Value>;
    if constexpr (std::is_same_v<Type, bool>) {
      writeInteger<uint8_t>(value ? 1u : 0u);
    } else if constexpr (std::is_integral_v<Type>) {
      writeInteger(value);
    } else if constexpr (std::is_floating_point_v<Type>) {
      static_assert(sizeof(Type) == sizeof(uint32_t) || sizeof(Type) == sizeof(uint64_t));
      if constexpr (sizeof(Type) == sizeof(uint32_t)) {
        writeInteger(std::bit_cast<uint32_t>(value));
      } else {
        writeInteger(std::bit_cast<uint64_t>(value));
      }
    } else if constexpr (std::is_enum_v<Type>) {
      writeInteger(static_cast<std::underlying_type_t<Type>>(value));
    } else if constexpr (std::is_same_v<Type, std::string>) {
      write(static_cast<uint64_t>(value.size()));
      writeBytes(value.data(), value.size());
    } else if constexpr (IsVector<Type>::value) {
      writeSequence(std::span<const typename Type::value_type>{value});
    } else if constexpr (IsOptional<Type>::value) {
      write(value.has_value());
      if (value.has_value()) {
        write(*value);
      }
    } else if constexpr (IsArray<Type>::value) {
      for (const auto& element : value) {
        write(element);
      }
    } else if constexpr (IsEnttId<Type>::value) {
      write(value.entity());
    } else if constexpr (HasCustomBinaryArchive<BinaryOutputArchive, const Type>) {
      binaryArchive(*this, value);
    } else if constexpr (std::is_aggregate_v<Type>) {
      boost::pfr::for_each_field(value, [this](const auto& field) { write(field); });
    } else {
      static_assert(kUnsupportedArchiveType<Type>, "binary archive type requires an explicit serializer");
    }
  }

  std::ostream& _output;
  std::array<char, 64u * 1024u> _buffer{};
  std::size_t _buffered_bytes = 0;
  uint64_t _byte_count = 0;
};

class BinaryInputArchive
{
 public:
  explicit BinaryInputArchive(std::istream& input) : _input(input) {}

  template <typename... Values>
  void operator()(Values&... values)
  {
    (read(values), ...);
  }

  template <ByteCompatibleArchiveValue Value, typename Allocator>
  void readByteCompatibleSequence(std::vector<Value, Allocator>& values)
  {
    uint64_t size = 0;
    read(size);
    if (size > values.max_size()) {
      throw std::length_error("binary vector exceeds host limits");
    }
    values.resize(static_cast<std::size_t>(size));
    if constexpr (std::endian::native == std::endian::little) {
      if (values.size() > std::numeric_limits<std::size_t>::max() / sizeof(Value)) {
        throw std::length_error("binary sequence byte size exceeds host limits");
      }
      readBytes(values.data(), values.size() * sizeof(Value));
    } else {
      for (auto& value : values) {
        read(value);
      }
    }
  }

  [[nodiscard]] uint64_t byteCount() const noexcept { return _byte_count; }

 private:
  void readBytes(void* data, std::size_t size)
  {
    auto* destination = static_cast<char*>(data);
    _byte_count += size;
    while (size != 0u) {
      if (_buffer_position == _buffered_bytes) {
        if (size >= _buffer.size()) {
          const auto requested = std::min(size, static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()));
          const auto count = _input.rdbuf()->sgetn(destination, static_cast<std::streamsize>(requested));
          if (count <= 0) throw std::runtime_error("truncated binary archive");
          destination += count;
          size -= static_cast<std::size_t>(count);
          continue;
        }
        const auto count = _input.rdbuf()->sgetn(_buffer.data(), static_cast<std::streamsize>(_buffer.size()));
        if (count <= 0) throw std::runtime_error("truncated binary archive");
        _buffer_position = 0u;
        _buffered_bytes = static_cast<std::size_t>(count);
      }
      const auto available = _buffered_bytes - _buffer_position;
      const auto count = std::min(size, available);
      std::memcpy(destination, _buffer.data() + _buffer_position, count);
      destination += count;
      size -= count;
      _buffer_position += count;
    }
  }

  template <typename Value>
  void readInteger(Value& value)
  {
    using Unsigned = std::make_unsigned_t<Value>;
    Unsigned bits = 0;
    if constexpr (std::endian::native == std::endian::little) {
      const auto available = _buffered_bytes - _buffer_position;
      if (available >= sizeof(bits)) {
        std::memcpy(&bits, _buffer.data() + _buffer_position, sizeof(bits));
        _buffer_position += sizeof(bits);
        _byte_count += sizeof(bits);
      } else {
        readBytes(&bits, sizeof(bits));
      }
    } else {
      std::array<uint8_t, sizeof(Value)> bytes{};
      readBytes(bytes.data(), bytes.size());
      for (std::size_t index = 0; index < sizeof(Value); ++index) {
        bits |= static_cast<Unsigned>(bytes[index]) << (index * 8u);
      }
    }
    value = static_cast<Value>(bits);
  }

  template <typename Value>
  void read(Value& value)
  {
    using Type = std::remove_cv_t<Value>;
    if constexpr (std::is_same_v<Type, bool>) {
      uint8_t encoded = 0;
      readInteger(encoded);
      if (encoded > 1u) {
        throw std::runtime_error("invalid boolean in binary archive");
      }
      value = encoded != 0u;
    } else if constexpr (std::is_integral_v<Type>) {
      readInteger(value);
    } else if constexpr (std::is_floating_point_v<Type>) {
      static_assert(sizeof(Type) == sizeof(uint32_t) || sizeof(Type) == sizeof(uint64_t));
      if constexpr (sizeof(Type) == sizeof(uint32_t)) {
        uint32_t bits = 0;
        readInteger(bits);
        value = std::bit_cast<Type>(bits);
      } else {
        uint64_t bits = 0;
        readInteger(bits);
        value = std::bit_cast<Type>(bits);
      }
    } else if constexpr (std::is_enum_v<Type>) {
      std::underlying_type_t<Type> encoded{};
      readInteger(encoded);
      value = static_cast<Type>(encoded);
    } else if constexpr (std::is_same_v<Type, std::string>) {
      uint64_t size = 0;
      read(size);
      if (size > value.max_size()) {
        throw std::length_error("binary string exceeds host limits");
      }
      value.resize(static_cast<std::size_t>(size));
      readBytes(value.data(), value.size());
    } else if constexpr (IsVector<Type>::value) {
      uint64_t size = 0;
      read(size);
      if (size > value.max_size()) {
        throw std::length_error("binary vector exceeds host limits");
      }
      value.resize(static_cast<std::size_t>(size));
      for (auto& element : value) {
        read(element);
      }
    } else if constexpr (IsOptional<Type>::value) {
      bool present = false;
      read(present);
      if (present) {
        value.emplace();
        read(*value);
      } else {
        value.reset();
      }
    } else if constexpr (IsArray<Type>::value) {
      for (auto& element : value) {
        read(element);
      }
    } else if constexpr (IsEnttId<Type>::value) {
      decltype(value.entity()) entity{};
      read(entity);
      value = Type{entity};
    } else if constexpr (HasCustomBinaryArchive<BinaryInputArchive, Type>) {
      binaryArchive(*this, value);
    } else if constexpr (std::is_aggregate_v<Type>) {
      boost::pfr::for_each_field(value, [this](auto& field) { read(field); });
    } else {
      static_assert(kUnsupportedArchiveType<Type>, "binary archive type requires an explicit serializer");
    }
  }

  std::istream& _input;
  std::array<char, 64u * 1024u> _buffer{};
  std::size_t _buffer_position = 0;
  std::size_t _buffered_bytes = 0;
  uint64_t _byte_count = 0;
};

}  // namespace eccdb::binary_detail

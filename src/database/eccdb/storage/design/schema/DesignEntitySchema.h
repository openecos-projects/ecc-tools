#pragma once

#include <cstdint>

namespace eccdb {

enum class DesignEntityMode : uint8_t
{
  kCompact32,
  kExtended64
};

// Compact mode keeps EnTT's default 32-bit layout. It minimizes entity and
// sparse-set overhead, but limits one physical design to roughly one million
// entity slots.
struct DesignEntitySchema32
{
  using underlying_type = uint32_t;

  static constexpr DesignEntityMode mode = DesignEntityMode::kCompact32;
  static constexpr uint32_t storage_bits = 32;
  static constexpr uint32_t entity_bits = 20;
  static constexpr uint32_t version_bits = 12;
  static constexpr uint64_t entity_mask = 0xFFFFF;
  static constexpr uint64_t version_mask = 0xFFF;
};

// Extended mode uses EnTT's default 64-bit layout. It supports a 32-bit
// entity index and makes generation wraparound practically unreachable.
struct DesignEntitySchema64
{
  using underlying_type = uint64_t;

  static constexpr DesignEntityMode mode = DesignEntityMode::kExtended64;
  static constexpr uint32_t storage_bits = 64;
  static constexpr uint32_t entity_bits = 32;
  static constexpr uint32_t version_bits = 32;
  static constexpr uint64_t entity_mask = 0xFFFFFFFF;
  static constexpr uint64_t version_mask = 0xFFFFFFFF;
};

#ifndef ECCDB_DESIGN_ENTITY_BITS
#define ECCDB_DESIGN_ENTITY_BITS 64
#endif

#if ECCDB_DESIGN_ENTITY_BITS == 32
using ActiveDesignEntitySchema = DesignEntitySchema32;
#elif ECCDB_DESIGN_ENTITY_BITS == 64
using ActiveDesignEntitySchema = DesignEntitySchema64;
#else
#error "ECCDB_DESIGN_ENTITY_BITS must be 32 or 64"
#endif

using DesignEntityUnderlying = ActiveDesignEntitySchema::underlying_type;

// The active mode is selected once for the entire binary. EnTT registries
// cannot switch entity types at runtime because the entity type is part of the
// registry's C++ type and ABI.
enum class DesignEntity : DesignEntityUnderlying
{
};

}  // namespace eccdb

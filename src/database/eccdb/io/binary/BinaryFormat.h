#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>

namespace eccdb::binary_detail {

enum class BinaryDatabaseKind : uint32_t
{
  kTech = 1,
  kLibrary = 2,
  kDesign = 3
};

struct BinaryFileHeader
{
  BinaryDatabaseKind kind = BinaryDatabaseKind::kTech;
  uint32_t schema_version = 0;
  uint64_t payload_size = 0;
};

using BinaryPayloadWriter = std::function<void(std::ostream&)>;
using BinaryPayloadReader = std::function<void(std::istream&, const BinaryFileHeader&)>;

void writeBinaryFile(const std::filesystem::path& path, BinaryFileHeader header, const BinaryPayloadWriter& writer);
void readBinaryFile(const std::filesystem::path& path, BinaryDatabaseKind expected_kind, uint32_t expected_schema_version,
                    const BinaryPayloadReader& reader);

}  // namespace eccdb::binary_detail

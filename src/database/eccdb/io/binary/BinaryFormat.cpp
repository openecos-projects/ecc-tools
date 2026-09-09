#include "binary/BinaryFormat.h"

#include <array>
#include <fstream>
#include <limits>
#include <ostream>
#include <stdexcept>

#include "binary/BinaryArchive.h"

namespace eccdb::binary_detail {
namespace {

constexpr std::array<uint8_t, 8> kMagic{'I', 'E', 'D', 'A', 'E', 'D', 'B', 0};
constexpr uint32_t kFormatVersion = 2;
constexpr uint32_t kHeaderSize = 32;

void writeHeader(std::ostream& output, const BinaryFileHeader& header)
{
  BinaryOutputArchive archive(output);
  archive(kMagic, kFormatVersion, kHeaderSize, header.kind, header.schema_version, header.payload_size);
  archive.flush();
  if (archive.byteCount() != kHeaderSize) {
    throw std::logic_error("binary header size does not match its format declaration");
  }
}

BinaryFileHeader readHeader(std::istream& input)
{
  std::array<uint8_t, 8> magic{};
  uint32_t format_version = 0;
  uint32_t header_size = 0;
  BinaryFileHeader header;
  BinaryInputArchive archive(input);
  archive(magic, format_version, header_size, header.kind, header.schema_version, header.payload_size);
  if (magic != kMagic) {
    throw std::runtime_error("not an iEDA binary database file");
  }
  if (format_version != kFormatVersion || header_size != kHeaderSize) {
    throw std::runtime_error("unsupported binary database format version");
  }
  return header;
}

}  // namespace

void writeBinaryFile(const std::filesystem::path& path, BinaryFileHeader header, const BinaryPayloadWriter& writer)
{
  try {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
      throw std::runtime_error("cannot open binary database for writing: " + path.string());
    }
    writeHeader(output, header);
    const auto payload_begin = output.tellp();
    writer(output);
    output.flush();
    const auto payload_end = output.tellp();
    if (!output || payload_begin < 0 || payload_end < payload_begin) {
      throw std::runtime_error("failed to write binary database payload");
    }
    header.payload_size = static_cast<uint64_t>(payload_end - payload_begin);
    output.seekp(0);
    writeHeader(output, header);
    output.flush();
    if (!output) throw std::runtime_error("cannot update binary database header: " + path.string());
  } catch (...) {
    std::error_code error;
    std::filesystem::remove(path, error);
    throw;
  }
}

void readBinaryFile(const std::filesystem::path& path, BinaryDatabaseKind expected_kind, uint32_t expected_schema_version,
                    const BinaryPayloadReader& reader)
{
  std::ifstream header_input(path, std::ios::binary);
  if (!header_input) {
    throw std::runtime_error("cannot open binary database for reading: " + path.string());
  }
  const auto header = readHeader(header_input);
  if (header.kind != expected_kind) {
    throw std::runtime_error("binary database kind does not match the requested loader");
  }
  if (header.schema_version != expected_schema_version) {
    throw std::runtime_error("unsupported binary database schema version");
  }
  header_input.close();

  if (header.payload_size > std::numeric_limits<std::uintmax_t>::max() - kHeaderSize
      || std::filesystem::file_size(path) != static_cast<std::uintmax_t>(kHeaderSize) + header.payload_size) {
    throw std::runtime_error("binary database file size does not match its header");
  }

  std::ifstream payload(path, std::ios::binary);
  payload.seekg(kHeaderSize);
  reader(payload, header);
  if (!payload || static_cast<uint64_t>(payload.tellg()) != kHeaderSize + header.payload_size) {
    throw std::runtime_error("binary database payload size mismatch");
  }
}

}  // namespace eccdb::binary_detail

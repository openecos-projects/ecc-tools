// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************

#include "layout_read.h"
#include "layout_write.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "IdbLayer.h"
#include "header.h"
#include "utility/logger/Logger.hpp"
namespace {

constexpr std::array<char, 8> kArchiveMagic = {'I', 'D', 'B', 'D', 'A', 'T', 'A', '\0'};

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireRect(idb::IdbRect* rect, int32_t lx, int32_t ly, int32_t ux, int32_t uy, const std::string& message)
{
  require(rect != nullptr, message + ": rectangle is null");
  require(rect->get_low_x() == lx && rect->get_low_y() == ly && rect->get_high_x() == ux && rect->get_high_y() == uy, message);
}

template <typename T>
void writePod(std::ofstream& output, const T& value)
{
  static_assert(std::is_trivially_copyable_v<T>);
  output.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

void writeString(std::ofstream& output, const std::string& value)
{
  const uint64_t size = value.size();
  writePod(output, size);
  output.write(value.data(), static_cast<std::streamsize>(size));
}

idb::IdbLayerCut* addCutLayer(idb::IdbLayout& layout, const std::string& name, int32_t width, double resistance)
{
  auto* cut = new idb::IdbLayerCut();
  cut->set_name(name);
  cut->set_id(0);
  cut->set_order(0);
  cut->set_width(width);
  cut->set_resistance(resistance);
  layout.get_layers()->get_layers().push_back(cut);
  layout.get_layers()->add_cut_layer(cut);
  return cut;
}

// This is the complete v2 encoding for the intentionally minimal CUT layer used below.
// It is a fixture for the reader's compatibility branch, not a second implementation of
// the current archive writer.
void writeVersion2MinimalCutLayerSection(const std::filesystem::path& path, const std::string& name, int32_t width)
{
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  require(static_cast<bool>(output), "failed to open v2 layers fixture");

  output.write(kArchiveMagic.data(), static_cast<std::streamsize>(kArchiveMagic.size()));
  writePod(output, idb::data_binary::kArchiveVersionV2);
  writePod(output, static_cast<uint32_t>(idb::data_binary::ArchiveSection::kLayoutLayers));

  const uint64_t layer_count = 1;
  writePod(output, layer_count);
  writePod(output, static_cast<std::underlying_type_t<idb::IdbLayerType>>(idb::IdbLayerType::kLayerCut));
  writeString(output, name);
  writePod(output, int8_t{0});
  writePod(output, uint8_t{0});
  writePod(output, width);

  const uint64_t spacing_count = 0;
  writePod(output, spacing_count);

  // These are the default objects emitted by a newly constructed v2 CUT layer.
  const uint8_t present = 1;
  const uint8_t false_value = 0;
  writePod(output, present);
  writePod(output, false_value);  // is_long_array
  writePod(output, int32_t{-1});  // cut_spacing
  writePod(output, uint64_t{0});  // array cut count
  for (int enclosure = 0; enclosure < 2; ++enclosure) {
    writePod(output, present);
    writePod(output, int32_t{-1});
    writePod(output, int32_t{-1});
    writePod(output, int32_t{0});
    writePod(output, int32_t{0});
    writePod(output, int32_t{0});
    writePod(output, int32_t{0});
  }

  require(static_cast<bool>(output), "failed to write v2 layers fixture");
}

void testCoreDoesNotAliasDieBoundingBox()
{
  idb::IdbLayout layout;
  layout.initDie(0, 0, 49319, 49319);

  auto* die_bbox = layout.get_die()->get_bounding_box();
  auto* core = layout.get_core();
  const bool has_independent_bbox = core->get_bounding_box() != die_bbox;

  // Avoid a double delete while reporting the regression in the old implementation.
  if (!has_independent_bbox) {
    core->set_bounding_box(new idb::IdbRect());
  }

  require(has_independent_bbox, "core and die must own independent bounding boxes");
  core->set_bounding_box(2000, 2800, 47200, 46200);
  requireRect(layout.get_die()->get_bounding_box(), 0, 0, 49319, 49319, "changing core must not change die");
}

void testArchiveUsesDiePointsAsCanonicalGeometry()
{
  const auto archive_dir = std::filesystem::temp_directory_path() / "idb_die_geometry_test";
  std::filesystem::remove_all(archive_dir);

  idb::IdbLayout source;
  source.initDie(0, 0, 49319, 49319);
  source.get_die()->IdbObject::set_bounding_box(2000, 2800, 47200, 46200);

  idb::LayoutWrite writer(&source);
  require(writer.writeLayout(archive_dir.string(), false), "failed to write stale layout archive");

  idb::IdbLayout restored;
  idb::LayoutRead reader;
  const bool loaded = reader.readLayout(&restored, archive_dir.string(), false);

  std::filesystem::remove_all(archive_dir);

  require(loaded, "failed to read stale layout archive");
  requireRect(restored.get_die()->get_bounding_box(), 0, 0, 49319, 49319, "DIEAREA points must override stale serialized geometry");
}

void testArchiveWithoutDiePointsUsesSerializedGeometry()
{
  const auto archive_dir = std::filesystem::temp_directory_path() / "idb_die_geometry_without_points_test";
  std::filesystem::remove_all(archive_dir);

  idb::IdbLayout source;
  source.get_die()->IdbObject::set_bounding_box(0, 0, 49319, 49319);

  idb::LayoutWrite writer(&source);
  require(writer.writeLayout(archive_dir.string(), false), "failed to write layout archive without DIEAREA points");

  idb::IdbLayout restored;
  idb::LayoutRead reader;
  const bool loaded = reader.readLayout(&restored, archive_dir.string(), false);

  std::filesystem::remove_all(archive_dir);

  require(loaded, "failed to read layout archive without DIEAREA points");
  requireRect(restored.get_die()->get_bounding_box(), 0, 0, 49319, 49319,
              "serialized die geometry must be preserved when DIEAREA points are absent");
}

void testArchivePreservesCutResistance()
{
  const auto archive_dir = std::filesystem::temp_directory_path() / "idb_cut_resistance_v3_test";
  std::filesystem::remove_all(archive_dir);

  constexpr double kResistance = 7.25;
  idb::IdbLayout source;
  addCutLayer(source, "VIA1", 80, kResistance);

  idb::LayoutWrite writer(&source);
  require(writer.writeLayout(archive_dir.string(), false), "failed to write CUT resistance archive");

  idb::IdbLayout restored;
  idb::LayoutRead reader;
  const bool loaded = reader.readLayout(&restored, archive_dir.string(), false);
  std::filesystem::remove_all(archive_dir);

  require(loaded, "failed to read CUT resistance archive");
  auto* cut = dynamic_cast<idb::IdbLayerCut*>(restored.get_layers()->find_layer("VIA1"));
  require(cut != nullptr, "CUT layer is missing after archive round-trip");
  require(std::abs(cut->get_resistance() - kResistance) < 1e-12, "CUT resistance must survive v3 archive round-trip");
}

void testVersion2ArchiveDefaultsCutResistance()
{
  const auto archive_dir = std::filesystem::temp_directory_path() / "idb_cut_resistance_v2_test";
  std::filesystem::remove_all(archive_dir);

  idb::IdbLayout source;
  addCutLayer(source, "VIA1", 80, 7.25);

  idb::LayoutWrite writer(&source);
  require(writer.writeLayout(archive_dir.string(), false), "failed to write archive before v2 fixture replacement");
  writeVersion2MinimalCutLayerSection(archive_dir / "layout" / "layers.idb", "VIA1", 80);

  idb::IdbLayout restored;
  idb::LayoutRead reader;
  const bool loaded = reader.readLayout(&restored, archive_dir.string(), false);
  std::filesystem::remove_all(archive_dir);

  require(loaded, "failed to read v2 CUT layers fixture");
  auto* cut = dynamic_cast<idb::IdbLayerCut*>(restored.get_layers()->find_layer("VIA1"));
  require(cut != nullptr, "CUT layer is missing after v2 archive read");
  require(cut->get_resistance() == 0.0, "v2 CUT archive must default omitted resistance to zero");
}

}  // namespace

int main()
{
  try {
    testCoreDoesNotAliasDieBoundingBox();
    testArchiveUsesDiePointsAsCanonicalGeometry();
    testArchiveWithoutDiePointsUsesSerializedGeometry();
    testArchivePreservesCutResistance();
    testVersion2ArchiveDefaultsCutResistance();
  } catch (const std::exception& error) {
    ECCLOG.warn(ecc::Loc::current(), error.what());
    return 1;
  }
  return 0;
}

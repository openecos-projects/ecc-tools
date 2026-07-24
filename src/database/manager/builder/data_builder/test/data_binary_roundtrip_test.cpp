// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************

#include <filesystem>
#include <iostream>

#include "IdbDesign.h"
#include "IdbLayout.h"
#include "header.h"

namespace {

bool test_use_min_spacing_round_trip()
{
  const auto archive = std::filesystem::temp_directory_path() / "idb_data_binary_roundtrip_test";
  std::filesystem::remove_all(archive);

  idb::IdbLayout source;
  source.set_use_min_spacing_obs(false);
  source.set_use_min_spacing_pin(true);
  if (!idb::data_binary::write_layout(archive.string(), &source, false)) {
    std::cerr << "failed to write layout archive\n";
    return false;
  }

  auto restored = idb::data_binary::read_layout(archive.string(), false);
  std::filesystem::remove_all(archive);
  if (restored == nullptr) {
    std::cerr << "failed to read layout archive\n";
    return false;
  }
  if (!restored->has_use_min_spacing_obs() || restored->get_use_min_spacing_obs()) {
    std::cerr << "USEMINSPACING OBS OFF was not preserved\n";
    return false;
  }
  if (!restored->has_use_min_spacing_pin() || !restored->get_use_min_spacing_pin()) {
    std::cerr << "USEMINSPACING PIN ON was not preserved\n";
    return false;
  }
  return true;
}

bool test_legacy_layout_metadata_defaults()
{
  const auto archive = std::filesystem::temp_directory_path() / "idb_data_binary_legacy_metadata_test";
  std::filesystem::remove_all(archive);

  idb::IdbLayout source;
  source.set_use_min_spacing_obs(false);
  source.set_use_min_spacing_pin(true);
  if (!idb::data_binary::write_layout(archive.string(), &source, false)) {
    std::cerr << "failed to write legacy layout fixture\n";
    return false;
  }

  const auto metadata = archive / "layout" / "metadata.idb";
  std::filesystem::resize_file(metadata, std::filesystem::file_size(metadata) - 4);
  auto restored = idb::data_binary::read_layout(archive.string(), false);
  std::filesystem::remove_all(archive);
  if (restored == nullptr) {
    std::cerr << "failed to read legacy layout fixture\n";
    return false;
  }
  if (restored->has_use_min_spacing_obs() || !restored->get_use_min_spacing_obs()) {
    std::cerr << "legacy OBS default changed\n";
    return false;
  }
  if (restored->has_use_min_spacing_pin() || restored->get_use_min_spacing_pin()) {
    std::cerr << "legacy PIN default changed\n";
    return false;
  }
  return true;
}

bool test_die_transform_offset_round_trip()
{
  const auto archive = std::filesystem::temp_directory_path() / "idb_data_binary_die_offset_test";
  std::filesystem::remove_all(archive);

  idb::IdbLayout layout;
  idb::IdbDesign source(&layout);
  source.set_die_transform_offset(-2000, -3000);
  if (!idb::data_binary::write_layout(archive.string(), &layout, false)
      || !idb::data_binary::write_design(archive.string(), &source, false)) {
    std::cerr << "failed to write die-offset archive\n";
    return false;
  }

  auto restored_layout = idb::data_binary::read_layout(archive.string(), false);
  auto restored = idb::data_binary::read_design(archive.string(), restored_layout.get(), false);
  std::filesystem::remove_all(archive);
  if (restored == nullptr) {
    std::cerr << "failed to read die-offset archive\n";
    return false;
  }
  if (restored->get_die_transform_offset_x() != -2000 || restored->get_die_transform_offset_y() != -3000) {
    std::cerr << "die transform offset was not preserved\n";
    return false;
  }
  return true;
}

bool test_legacy_design_metadata_defaults()
{
  const auto archive = std::filesystem::temp_directory_path() / "idb_data_binary_legacy_design_metadata_test";
  std::filesystem::remove_all(archive);

  idb::IdbLayout layout;
  idb::IdbDesign source(&layout);
  source.set_die_transform_offset(-2000, -3000);
  if (!idb::data_binary::write_layout(archive.string(), &layout, false)
      || !idb::data_binary::write_design(archive.string(), &source, false)) {
    std::cerr << "failed to write legacy design fixture\n";
    return false;
  }

  const auto metadata = archive / "design" / "metadata.idb";
  std::filesystem::resize_file(metadata, std::filesystem::file_size(metadata) - 8);
  auto restored_layout = idb::data_binary::read_layout(archive.string(), false);
  auto restored = idb::data_binary::read_design(archive.string(), restored_layout.get(), false);
  std::filesystem::remove_all(archive);
  if (restored == nullptr) {
    std::cerr << "failed to read legacy design fixture\n";
    return false;
  }
  if (restored->get_die_transform_offset_x() != 0 || restored->get_die_transform_offset_y() != 0) {
    std::cerr << "legacy die transform offset default changed\n";
    return false;
  }
  return true;
}

}  // namespace

int main()
{
  const bool passed = test_use_min_spacing_round_trip() && test_legacy_layout_metadata_defaults() && test_die_transform_offset_round_trip()
                      && test_legacy_design_metadata_defaults();
  return passed ? 0 : 1;
}

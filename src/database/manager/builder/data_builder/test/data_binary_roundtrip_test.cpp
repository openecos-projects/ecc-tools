// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************

#include <filesystem>
#include <iostream>

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

}  // namespace

int main()
{
  return test_use_min_spacing_round_trip() && test_legacy_layout_metadata_defaults() ? 0 : 1;
}

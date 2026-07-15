#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ecc::geometry {

struct GeometrySiteMetadata
{
  std::string name;
  std::string site_class;
  std::string symmetry;
  std::string orient;
  int32_t width = 0;
  int32_t height = 0;
  bool is_overlap = false;
};

struct GeometryMasterMetadata
{
  std::string name;
  std::string master_type;
  std::string site;
  std::string symmetry;
  int64_t origin_x = 0;
  int64_t origin_y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t term_count = 0;
  uint32_t obs_count = 0;
};

struct GeometryViaMetadata
{
  std::string name;
  std::string master_name;
  std::string via_type;
  std::string rule_name;
  std::string bottom_layer;
  std::string cut_layer;
  std::string top_layer;
  int32_t cut_width = 0;
  int32_t cut_height = 0;
  int32_t cut_spacing_x = 0;
  int32_t cut_spacing_y = 0;
  int32_t enclosure_bottom_x = 0;
  int32_t enclosure_bottom_y = 0;
  int32_t enclosure_top_x = 0;
  int32_t enclosure_top_y = 0;
  int32_t rows = 0;
  int32_t cols = 0;
  bool is_default = false;
};

struct GeometryGridMetadata
{
  std::string grid_type;
  std::string direction;
  uint32_t index = 0;
  int64_t start = 0;
  int64_t step = 0;
  uint32_t count = 0;
  int32_t width = 0;
  std::vector<std::string> layer_names;
};

struct GeometryConnectivityMetadata
{
  std::string net_name;
  std::string net_kind;
  std::string endpoint_type;
  std::string instance_name;
  std::string pin_name;
  std::string master_name;
};

struct GeometryBusMetadata
{
  std::string name;
  std::string bus_type;
  uint32_t left = 0;
  uint32_t right = 0;
  uint32_t net_count = 0;
  uint32_t pin_count = 0;
  std::vector<std::string> net_names;
  std::vector<std::string> pin_names;
};

struct GeometryGroupMetadata
{
  std::string name;
  std::string region_name;
  uint32_t instance_count = 0;
  std::vector<std::string> instance_names;
};

}  // namespace ecc::geometry

#pragma once

#include <cstdint>
#include <string>

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
};

struct GeometryGroupMetadata
{
  std::string name;
  std::string region_name;
  uint32_t instance_count = 0;
};

}  // namespace ecc::geometry

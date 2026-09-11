#pragma once

#include "EMIRHeader.hpp"

namespace iemir {

struct RedHawkWireSegmentRecord
{
  std::string id;
  std::string layer_name;
  std::string net_name;
  double first_x_um = 0.0;
  double first_y_um = 0.0;
  double second_x_um = 0.0;
  double second_y_um = 0.0;
  double width_um = 0.0;
  double resistance_ohm = 0.0;
};

struct RedHawkViaRecord
{
  std::string id;
  std::string layer_name;
  std::string via_name;
  std::string net_name;
  double x_um = 0.0;
  double y_um = 0.0;
  int32_t cut_num = 0;
  double cut_width_um = 0.0;
  double cut_height_um = 0.0;
  double resistance_ohm = 0.0;
  std::vector<std::string> connected_wire_segment_ids;
};

struct RedHawkResNetwork
{
  std::vector<RedHawkWireSegmentRecord> wire_segments;
  std::vector<RedHawkViaRecord> vias;
};

class RedHawkResNetworkReader
{
 public:
  static RedHawkResNetwork read(const std::string& file_path);
  static std::optional<std::pair<double, double>> connectionCoordinate(
      const RedHawkViaRecord& via, const std::unordered_map<std::string, const RedHawkWireSegmentRecord*>& wire_segment_map,
      const std::string& layer_name);
};

}  // namespace iemir

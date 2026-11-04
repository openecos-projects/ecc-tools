#pragma once

#include "EMIRHeader.hpp"

namespace iemir {

struct ResistanceWireSegmentRecord
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

struct ResistanceViaRecord
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

struct ResistanceNetwork
{
  std::vector<ResistanceWireSegmentRecord> wire_segments;
  std::vector<ResistanceViaRecord> vias;
};

class ResistanceNetworkReader
{
 public:
  static ResistanceNetwork read(const std::string& file_path);
  static std::optional<std::pair<double, double>> connectionCoordinate(
      const ResistanceViaRecord& via, const std::unordered_map<std::string, const ResistanceWireSegmentRecord*>& wire_segment_map,
      const std::string& layer_name);
};

}  // namespace iemir

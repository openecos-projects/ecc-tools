// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "TopoSvgPlotter.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <system_error>

namespace irt {

namespace {

constexpr int32_t kMarginCellNum = 2;
constexpr double kCellSize = 16.0;
constexpr double kPadding = 48.0;
constexpr int32_t kLegendWidth = 250;

struct SvgCanvas
{
  int32_t min_x = 0;
  int32_t min_y = 0;
  int32_t max_x = 0;
  int32_t max_y = 0;
  int32_t width = 0;
  int32_t height = 0;

  double getX(double x) const { return kPadding + (x - min_x) * kCellSize; }
  double getY(double y) const { return kPadding + (max_y - y) * kCellSize; }
};

SvgCanvas getSvgCanvas(const TopoSvgPlotRequest& request)
{
  SvgCanvas canvas;
  canvas.min_x = request.planar_search_region.get_ll_x();
  canvas.min_y = request.planar_search_region.get_ll_y();
  canvas.max_x = request.planar_search_region.get_ur_x();
  canvas.max_y = request.planar_search_region.get_ur_y();
  auto extendCanvas = [&](const PlanarCoord& coord) {
    canvas.min_x = std::min(canvas.min_x, coord.get_x());
    canvas.min_y = std::min(canvas.min_y, coord.get_y());
    canvas.max_x = std::max(canvas.max_x, coord.get_x());
    canvas.max_y = std::max(canvas.max_y, coord.get_y());
  };
  for (const PlanarRect& planar_obs : request.planar_obs_list) {
    extendCanvas(planar_obs.get_ll());
    extendCanvas(planar_obs.get_ur());
  }
  for (const PlanarCoord& terminal_coord : request.terminal_coord_list) {
    extendCanvas(terminal_coord);
  }
  for (const std::vector<Segment<PlanarCoord>>* planar_topo_list : {&request.flute_topo_list, &request.legal_topo_list}) {
    for (const Segment<PlanarCoord>& planar_topo : *planar_topo_list) {
      extendCanvas(planar_topo.get_first());
      extendCanvas(planar_topo.get_second());
    }
  }
  canvas.min_x -= kMarginCellNum;
  canvas.min_y -= kMarginCellNum;
  canvas.max_x += kMarginCellNum;
  canvas.max_y += kMarginCellNum;
  canvas.width = static_cast<int32_t>(2 * kPadding + (canvas.max_x - canvas.min_x) * kCellSize) + kLegendWidth;
  canvas.height = static_cast<int32_t>(2 * kPadding + (canvas.max_y - canvas.min_y) * kCellSize);
  return canvas;
}

std::string escapeXml(const std::string& value)
{
  std::string escaped_value;
  escaped_value.reserve(value.size());
  for (char ch : value) {
    switch (ch) {
      case '&':
        escaped_value += "&amp;";
        break;
      case '<':
        escaped_value += "&lt;";
        break;
      case '>':
        escaped_value += "&gt;";
        break;
      case '\"':
        escaped_value += "&quot;";
        break;
      case '\'':
        escaped_value += "&apos;";
        break;
      default:
        escaped_value += ch;
        break;
    }
  }
  return escaped_value;
}

std::string getCoordLabel(const std::string& prefix, const PlanarCoord& coord)
{
  std::ostringstream stream;
  stream << prefix << "(" << coord.get_x() << "," << coord.get_y() << ")";
  return stream.str();
}

std::vector<PlanarCoord> getSteinerCoordList(const std::vector<PlanarCoord>& terminal_coord_list, const std::vector<Segment<PlanarCoord>>& planar_topo_list)
{
  std::set<PlanarCoord, CmpPlanarCoordByXASC> terminal_coord_set(terminal_coord_list.begin(), terminal_coord_list.end());
  std::set<PlanarCoord, CmpPlanarCoordByXASC> steiner_coord_set;
  for (const Segment<PlanarCoord>& planar_topo : planar_topo_list) {
    for (const PlanarCoord& coord : {planar_topo.get_first(), planar_topo.get_second()}) {
      if (terminal_coord_set.find(coord) == terminal_coord_set.end()) {
        steiner_coord_set.insert(coord);
      }
    }
  }
  return std::vector<PlanarCoord>(steiner_coord_set.begin(), steiner_coord_set.end());
}

void writeGrid(std::ofstream& output_stream, const SvgCanvas& canvas)
{
  int32_t x_span = canvas.max_x - canvas.min_x;
  int32_t y_span = canvas.max_y - canvas.min_y;
  if (std::max(x_span, y_span) > 64) {
    return;
  }
  output_stream << "<g data-layer=\"grid\" stroke=\"#E0E0E0\" stroke-width=\"1\">\n";
  for (int32_t x = canvas.min_x; x <= canvas.max_x; x++) {
    output_stream << "<line x1=\"" << canvas.getX(x) << "\" y1=\"" << canvas.getY(canvas.min_y) << "\" x2=\"" << canvas.getX(x) << "\" y2=\""
                  << canvas.getY(canvas.max_y) << "\"/>\n";
  }
  for (int32_t y = canvas.min_y; y <= canvas.max_y; y++) {
    output_stream << "<line x1=\"" << canvas.getX(canvas.min_x) << "\" y1=\"" << canvas.getY(y) << "\" x2=\"" << canvas.getX(canvas.max_x) << "\" y2=\""
                  << canvas.getY(y) << "\"/>\n";
  }
  output_stream << "</g>\n";
}

void writeGridRect(std::ofstream& output_stream, const SvgCanvas& canvas, const PlanarRect& planar_rect, const std::string& style)
{
  double left = planar_rect.get_ll_x() - 0.5;
  double right = planar_rect.get_ur_x() + 0.5;
  double bottom = planar_rect.get_ll_y() - 0.5;
  double top = planar_rect.get_ur_y() + 0.5;
  output_stream << "<rect x=\"" << canvas.getX(left) << "\" y=\"" << canvas.getY(top) << "\" width=\"" << canvas.getX(right) - canvas.getX(left)
                << "\" height=\"" << canvas.getY(bottom) - canvas.getY(top) << "\" " << style << "/>\n";
}

void writeTopology(std::ofstream& output_stream, const SvgCanvas& canvas, const std::string& layer_name,
                   const std::vector<Segment<PlanarCoord>>& planar_topo_list, const std::string& style)
{
  output_stream << "<g data-layer=\"" << layer_name << "\" " << style << ">\n";
  for (const Segment<PlanarCoord>& planar_topo : planar_topo_list) {
    const PlanarCoord& first_coord = planar_topo.get_first();
    const PlanarCoord& second_coord = planar_topo.get_second();
    output_stream << "<line x1=\"" << canvas.getX(first_coord.get_x()) << "\" y1=\"" << canvas.getY(first_coord.get_y()) << "\" x2=\""
                  << canvas.getX(second_coord.get_x()) << "\" y2=\"" << canvas.getY(second_coord.get_y()) << "\"/>\n";
  }
  output_stream << "</g>\n";
}

void writeTerminals(std::ofstream& output_stream, const SvgCanvas& canvas, const std::vector<PlanarCoord>& terminal_coord_list)
{
  output_stream << "<g data-layer=\"terminals\">\n";
  for (const PlanarCoord& terminal_coord : terminal_coord_list) {
    double x = canvas.getX(terminal_coord.get_x());
    double y = canvas.getY(terminal_coord.get_y());
    output_stream << "<circle cx=\"" << x << "\" cy=\"" << y << "\" r=\"4.5\" fill=\"#2E7D32\" stroke=\"#FFFFFF\" stroke-width=\"1.5\"/>\n";
    output_stream << "<text x=\"" << x + 6 << "\" y=\"" << y - 6 << "\" fill=\"#1B5E20\" class=\"coord-label\">" << getCoordLabel("T", terminal_coord)
                  << "</text>\n";
  }
  output_stream << "</g>\n";
}

void writeDiamond(std::ofstream& output_stream, double x, double y, double radius, const std::string& style)
{
  output_stream << "<path d=\"M " << x << " " << y - radius << " L " << x + radius << " " << y << " L " << x << " " << y + radius << " L " << x - radius << " "
                << y << " Z\" " << style << "/>\n";
}

void writeSteinerCoords(std::ofstream& output_stream, const SvgCanvas& canvas, const std::string& layer_name,
                        const std::vector<PlanarCoord>& steiner_coord_list, const std::string& prefix, double radius, const std::string& style,
                        const std::string& label_color)
{
  output_stream << "<g data-layer=\"" << layer_name << "\">\n";
  for (const PlanarCoord& steiner_coord : steiner_coord_list) {
    double x = canvas.getX(steiner_coord.get_x());
    double y = canvas.getY(steiner_coord.get_y());
    writeDiamond(output_stream, x, y, radius, style);
    output_stream << "<text x=\"" << x + 7 << "\" y=\"" << y + 14 << "\" fill=\"" << label_color << "\" class=\"coord-label\">"
                  << getCoordLabel(prefix, steiner_coord) << "</text>\n";
  }
  output_stream << "</g>\n";
}

void writeLegend(std::ofstream& output_stream, const SvgCanvas& canvas)
{
  double legend_x = canvas.width - 242;
  output_stream << "<g data-layer=\"legend\">\n";
  output_stream << "<rect x=\"" << legend_x << "\" y=\"10\" width=\"232\" height=\"126\" fill=\"#FFFFFF\" "
                << "fill-opacity=\"0.92\" stroke=\"#9E9E9E\"/>\n";
  output_stream << "<text x=\"" << legend_x + 10 << "\" y=\"30\" class=\"legend-title\">Topology legend</text>\n";
  output_stream << "<rect x=\"" << legend_x + 10 << "\" y=\"40\" width=\"16\" height=\"12\" fill=\"#EF5350\" fill-opacity=\"0.45\"/>\n";
  output_stream << "<text x=\"" << legend_x + 34 << "\" y=\"51\" class=\"legend-text\">Obstacle</text>\n";
  output_stream << "<line x1=\"" << legend_x + 10 << "\" y1=\"68\" x2=\"" << legend_x + 26
                << "\" y2=\"68\" stroke=\"#78909C\" stroke-width=\"2\" stroke-dasharray=\"6 4\"/>\n";
  output_stream << "<text x=\"" << legend_x + 34 << "\" y=\"72\" class=\"legend-text\">Raw FLUTE edge</text>\n";
  output_stream << "<line x1=\"" << legend_x + 10 << "\" y1=\"88\" x2=\"" << legend_x + 26 << "\" y2=\"88\" stroke=\"#1565C0\" stroke-width=\"3\"/>\n";
  output_stream << "<text x=\"" << legend_x + 34 << "\" y=\"92\" class=\"legend-text\">Legalized edge</text>\n";
  output_stream << "<circle cx=\"" << legend_x + 18 << "\" cy=\"110\" r=\"4\" fill=\"#2E7D32\"/>\n";
  output_stream << "<text x=\"" << legend_x + 34 << "\" y=\"114\" class=\"legend-text\">Terminal</text>\n";
  writeDiamond(output_stream, legend_x + 18, 128, 5, "fill=\"#FFFFFF\" stroke=\"#FB8C00\" stroke-width=\"2\"");
  writeDiamond(output_stream, legend_x + 18, 128, 3, "fill=\"#8E24AA\" stroke=\"#FFFFFF\" stroke-width=\"1\"");
  output_stream << "<text x=\"" << legend_x + 34 << "\" y=\"132\" class=\"legend-text\">Raw / legal Steiner</text>\n";
  output_stream << "</g>\n";
}

}  // namespace

bool writeTopoSvg(const std::string& file_path, const TopoSvgPlotRequest& request, std::string& error_message)
{
  error_message.clear();
  if (request.planar_search_region.isIncorrect()) {
    error_message = "The planar search region is invalid.";
    return false;
  }
  for (const PlanarRect& planar_obs : request.planar_obs_list) {
    if (planar_obs.isIncorrect()) {
      error_message = "The planar obstacle is invalid.";
      return false;
    }
  }

  std::filesystem::path output_path(file_path);
  if (output_path.empty()) {
    error_message = "The SVG file path is empty.";
    return false;
  }
  std::error_code error_code;
  if (!output_path.parent_path().empty()) {
    std::filesystem::create_directories(output_path.parent_path(), error_code);
    if (error_code) {
      error_message = "Cannot create output directory: " + error_code.message();
      return false;
    }
  }

  std::ofstream output_stream(output_path);
  if (!output_stream.is_open()) {
    error_message = "Cannot open the SVG file for writing.";
    return false;
  }
  output_stream << std::fixed << std::setprecision(1);

  SvgCanvas canvas = getSvgCanvas(request);
  std::vector<PlanarCoord> flute_steiner_coord_list = getSteinerCoordList(request.terminal_coord_list, request.flute_topo_list);
  std::vector<PlanarCoord> legal_steiner_coord_list = getSteinerCoordList(request.terminal_coord_list, request.legal_topo_list);

  output_stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  output_stream << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << canvas.width << "\" height=\"" << canvas.height << "\" viewBox=\"0 0 "
                << canvas.width << " " << canvas.height << "\">\n";
  output_stream << "<style>.coord-label{font-family:monospace;font-size:11px}.legend-title{font-family:monospace;font-size:13px;font-weight:bold}"
                << ".legend-text{font-family:monospace;font-size:11px}</style>\n";
  output_stream << "<rect data-layer=\"background\" width=\"100%\" height=\"100%\" fill=\"#FFFFFF\"/>\n";
  output_stream << "<text x=\"12\" y=\"28\" font-family=\"monospace\" font-size=\"16\" font-weight=\"bold\">" << escapeXml(request.title) << "</text>\n";
  writeGrid(output_stream, canvas);

  output_stream << "<g data-layer=\"search-region\">\n";
  writeGridRect(output_stream, canvas, request.planar_search_region, "fill=\"none\" stroke=\"#212121\" stroke-width=\"1.5\" stroke-dasharray=\"5 3\"");
  output_stream << "</g>\n";
  output_stream << "<g data-layer=\"obstacle\" fill=\"#EF5350\" fill-opacity=\"0.45\" stroke=\"#C62828\" stroke-width=\"1.5\">\n";
  for (const PlanarRect& planar_obs : request.planar_obs_list) {
    writeGridRect(output_stream, canvas, planar_obs, "");
  }
  output_stream << "</g>\n";

  writeTopology(output_stream, canvas, "flute-topology", request.flute_topo_list,
                "fill=\"none\" stroke=\"#78909C\" stroke-width=\"2\" stroke-dasharray=\"6 4\"");
  writeTopology(output_stream, canvas, "legal-topology", request.legal_topo_list, "fill=\"none\" stroke=\"#1565C0\" stroke-width=\"3\"");
  writeTerminals(output_stream, canvas, request.terminal_coord_list);
  writeSteinerCoords(output_stream, canvas, "raw-steiner", flute_steiner_coord_list, "Sraw", 6, "fill=\"#FFFFFF\" stroke=\"#FB8C00\" stroke-width=\"2\"",
                     "#E65100");
  writeSteinerCoords(output_stream, canvas, "legal-steiner", legal_steiner_coord_list, "Slegal", 4, "fill=\"#8E24AA\" stroke=\"#FFFFFF\" stroke-width=\"1\"",
                     "#6A1B9A");
  writeLegend(output_stream, canvas);
  output_stream << "</svg>\n";

  if (!output_stream.good()) {
    error_message = "An I/O error occurred while writing the SVG file.";
    return false;
  }
  return true;
}

}  // namespace irt

/*
 * @FilePath: map_layout_writer.cpp
 * @Author: Yihang Qiu (qiuyihang23@mails.ucas.ac.cn)
 * @Date: 2026-07-08
 * @Description:
 */

#include "map_layout_writer.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "GeometryDisplayWriter.h"

namespace ieval {

namespace {
bool writeCells(const std::string& map_dir, const std::vector<MapLayoutCell>& cells)
{
  if (map_dir.empty() || cells.empty()) {
    return false;
  }

  std::ofstream layout_file(map_dir + "/layout.csv");
  if (!layout_file.is_open()) {
    return false;
  }

  layout_file << "pixel_row,pixel_col,grid_x,grid_y,lx,ly,ux,uy\n";
  using Writer = ecc::geometry::GeometryDisplayWriter;
  Writer binary(std::filesystem::path(map_dir) / "layout.glayout", {{"kind", "layout"}, {"key", "layout"}, {"label", "layout"}},
                Writer::Encoding::kCells);
  for (const MapLayoutCell& cell : cells) {
    binary.cell(cell.pixel_row, cell.pixel_col, {cell.lx, cell.ly, cell.ux, cell.uy}, 0.0);
    layout_file << cell.pixel_row << "," << cell.pixel_col << "," << cell.grid_x << "," << cell.grid_y << "," << cell.lx << "," << cell.ly
                << "," << cell.ux << "," << cell.uy << "\n";
  }
  binary.finish();

  return true;
}
}  // namespace

void writeMapDisplay(const std::string& csv_path, const std::vector<std::vector<double>>& matrix, bool reverse_rows)
{
  using Writer = ecc::geometry::GeometryDisplayWriter;
  if (matrix.empty() || matrix.front().empty())
    return;
  const std::filesystem::path source(csv_path);
  const auto category = source.parent_path().filename().string();
  auto destination = source;
  destination.replace_extension(".gdisplay");
  std::ifstream layout(source.parent_path() / "layout.glayout", std::ios::binary);
  layout.exceptions(std::ios::failbit | std::ios::badbit);
  auto integer = [&](unsigned count) {
    uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i)
      value |= uint64_t(static_cast<unsigned char>(layout.get())) << (i * 8);
    return value;
  };
  char magic[8];
  layout.read(magic, 8);
  if (std::string(magic, 8) != "ECOSDSP1" || integer(4) != 1 || integer(4) != 1)
    throw std::runtime_error("Invalid map layout binary");
  const auto metadata = integer(4);
  if (integer(4) != 32)
    throw std::runtime_error("Invalid layout cell size");
  const auto count = integer(8);
  integer(8);
  layout.seekg(40 + metadata);
  Writer output(destination,
                {{"kind", "map"},
                 {"key", category + ":" + source.stem().string()},
                 {"label", source.stem().string()},
                 {"category", category},
                 {"rows", matrix.size()},
                 {"columns", matrix.front().size()},
                 {"coordinate_unit", "dbu"}},
                Writer::Encoding::kCells);
  for (uint64_t i = 0; i < count; ++i) {
    const auto row = integer(4), col = integer(4);
    std::array<int32_t, 4> bounds;
    for (auto& value : bounds)
      value = static_cast<int32_t>(integer(4));
    integer(8);
    if (row >= matrix.size() || col >= matrix.front().size())
      throw std::runtime_error("Map layout exceeds matrix");
    const auto& values = matrix[reverse_rows ? matrix.size() - 1 - row : row];
    if (values.size() != matrix.front().size())
      throw std::runtime_error("Ragged map matrix");
    output.cell(row, col, bounds, values[col]);
  }
  output.finish();
}

void writeEGRMapDisplay(const std::string& csv_path, const std::string& gcell_info_path, const std::vector<std::vector<double>>& matrix)
{
  if (matrix.empty() || matrix.front().empty())
    return;
  const auto map_dir = std::filesystem::path(csv_path).parent_path().string();
  if (writeEGRLayoutCsv(map_dir, gcell_info_path, static_cast<int32_t>(matrix.front().size()), static_cast<int32_t>(matrix.size()))) {
    writeMapDisplay(csv_path, matrix, false);
  }
}

bool writeMapLayoutCsv(const std::string& map_dir, int32_t grid_cols, int32_t grid_rows, int32_t grid_size, int32_t lx, int32_t ly,
                       int32_t ux, int32_t uy)
{
  if (map_dir.empty() || grid_cols <= 0 || grid_rows <= 0 || grid_size <= 0 || lx >= ux || ly >= uy) {
    return false;
  }

  std::vector<MapLayoutCell> cells;
  cells.reserve(static_cast<size_t>(grid_cols) * static_cast<size_t>(grid_rows));
  for (int32_t pixel_row = 0; pixel_row < grid_rows; ++pixel_row) {
    int32_t grid_row = grid_rows - 1 - pixel_row;
    for (int32_t grid_col = 0; grid_col < grid_cols; ++grid_col) {
      int32_t grid_lx = lx + grid_col * grid_size;
      int32_t grid_ly = ly + grid_row * grid_size;
      int32_t grid_ux = std::min(lx + (grid_col + 1) * grid_size, ux);
      int32_t grid_uy = std::min(ly + (grid_row + 1) * grid_size, uy);
      cells.push_back({pixel_row, grid_col, grid_col, grid_row, grid_lx, grid_ly, grid_ux, grid_uy});
    }
  }

  return writeCells(map_dir, cells);
}

bool writeMapLayoutCsv(const std::string& map_dir, const std::vector<MapLayoutCell>& cells)
{
  return writeCells(map_dir, cells);
}

bool writeEGRLayoutCsv(const std::string& map_dir, const std::string& gcell_info_path, int32_t matrix_cols, int32_t matrix_rows)
{
  if (matrix_cols <= 0 || matrix_rows <= 0) {
    return false;
  }
  std::ifstream gcell_file(gcell_info_path);
  if (!gcell_file.is_open()) {
    return false;
  }

  std::vector<MapLayoutCell> cells;
  std::string line;
  while (std::getline(gcell_file, line)) {
    std::istringstream input(line);
    std::vector<int32_t> row;
    std::string value;
    while (std::getline(input, value, ',')) {
      if (!value.empty()) {
        row.push_back(std::stoi(value));
      }
    }
    if (row.size() < 6) {
      continue;
    }
    const int32_t grid_x = row[0];
    const int32_t grid_y = row[1];
    if (grid_x < 0 || grid_x >= matrix_cols || grid_y < 0 || grid_y >= matrix_rows) {
      continue;
    }
    cells.push_back({matrix_rows - 1 - grid_y, grid_x, grid_x, grid_y, row[2], row[3], row[4], row[5]});
  }
  return writeCells(map_dir, cells);
}

}  // namespace ieval

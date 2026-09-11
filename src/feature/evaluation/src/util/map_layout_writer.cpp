/*
 * @FilePath: map_layout_writer.cpp
 * @Author: Yihang Qiu (qiuyihang23@mails.ucas.ac.cn)
 * @Date: 2026-07-08
 * @Description:
 */

#include "map_layout_writer.h"

#include <algorithm>
#include <fstream>

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
  for (const MapLayoutCell& cell : cells) {
    layout_file << cell.pixel_row << "," << cell.pixel_col << "," << cell.grid_x << "," << cell.grid_y << "," << cell.lx << ","
                << cell.ly << "," << cell.ux << "," << cell.uy << "\n";
  }

  return true;
}
}  // namespace

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

}  // namespace ieval

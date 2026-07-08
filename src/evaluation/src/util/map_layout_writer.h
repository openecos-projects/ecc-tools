/*
 * @FilePath: map_layout_writer.h
 * @Author: Yihang Qiu (qiuyihang23@mails.ucas.ac.cn)
 * @Date: 2026-07-08
 * @Description:
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ieval {

struct MapLayoutCell
{
  int32_t pixel_row;
  int32_t pixel_col;
  int32_t grid_x;
  int32_t grid_y;
  int32_t lx;
  int32_t ly;
  int32_t ux;
  int32_t uy;
};

bool writeMapLayoutCsv(const std::string& map_dir, int32_t grid_cols, int32_t grid_rows, int32_t grid_size, int32_t lx, int32_t ly,
                       int32_t ux, int32_t uy);
bool writeMapLayoutCsv(const std::string& map_dir, const std::vector<MapLayoutCell>& cells);

}  // namespace ieval

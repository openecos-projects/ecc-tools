#include "map_layout_writer.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "utility/logger/Logger.hpp"
namespace {
std::vector<std::string> readLines(const std::filesystem::path& path)
{
  std::ifstream file(path);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(file, line)) {
    lines.push_back(line);
  }
  return lines;
}

void expectEqual(const std::string& actual, const std::string& expected, const std::string& message)
{
  if (actual != expected) {
    ECCLOG.warn(ecc::Loc::current(), message, "\nexpected: ", expected, "\nactual:   ", actual);
    std::exit(1);
  }
}
}  // namespace

int main()
{
  const std::filesystem::path out_dir = std::filesystem::temp_directory_path() / "ecc_eval_layout_writer_test";
  std::filesystem::remove_all(out_dir);
  std::filesystem::create_directories(out_dir);

  const bool ok = ieval::writeMapLayoutCsv(out_dir.string(), 3, 2, 10, 100, 200, 125, 218);
  if (!ok) {
    ECCLOG.warn(ecc::Loc::current(), "writeMapLayoutCsv returned false\n");
    return 1;
  }

  const auto lines = readLines(out_dir / "layout.csv");
  if (lines.size() != 7) {
    ECCLOG.warn(ecc::Loc::current(), "expected header plus 6 layout rows, got ", lines.size());
    return 1;
  }

  expectEqual(lines[0], "pixel_row,pixel_col,grid_x,grid_y,lx,ly,ux,uy", "header mismatch");
  expectEqual(lines[1], "0,0,0,1,100,210,110,218", "top-left pixel should map to highest grid y");
  expectEqual(lines[3], "0,2,2,1,120,210,125,218", "last column should clamp ux to region ux");
  expectEqual(lines[4], "1,0,0,0,100,200,110,210", "second pixel row should map to bottom grid y");
  expectEqual(lines[6], "1,2,2,0,120,200,125,210", "bottom-right pixel bbox mismatch");

  // A top-left display cell must use the high-Y native row and preserve f64.
  const double peak = 0.12345678901234567;
  ieval::writeMapDisplay((out_dir / "density.csv").string(), {{1.0, 2.0, 3.0}, {peak, 5.0, 6.0}}, true);
  std::ifstream binary(out_dir / "density.gdisplay", std::ios::binary);
  auto integer = [&](unsigned size) {
    uint64_t value = 0;
    for (unsigned i = 0; i < size; ++i)
      value |= uint64_t(uint8_t(binary.get())) << (8 * i);
    return value;
  };
  binary.seekg(16);
  const auto metadata_size = integer(4);
  binary.seekg(24);
  if (integer(8) != 6)
    return 1;
  binary.seekg(40 + metadata_size);
  std::array<int32_t, 6> coordinates;
  for (auto& value : coordinates)
    value = static_cast<int32_t>(integer(4));
  const auto bits = integer(8);
  double value;
  std::memcpy(&value, &bits, sizeof(value));
  if (coordinates != std::array<int32_t, 6>{0, 0, 100, 210, 110, 218} || value != peak)
    return 1;

  const std::vector<ieval::MapLayoutCell> gcell_layout = {
      {.pixel_row = 0, .pixel_col = 0, .grid_x = 0, .grid_y = 7, .lx = 10, .ly = 30, .ux = 20, .uy = 40},
      {.pixel_row = 0, .pixel_col = 1, .grid_x = 1, .grid_y = 7, .lx = 20, .ly = 30, .ux = 35, .uy = 40},
  };
  const std::filesystem::path gcell_dir = out_dir / "egr";
  std::filesystem::create_directories(gcell_dir);
  if (!ieval::writeMapLayoutCsv(gcell_dir.string(), gcell_layout)) {
    ECCLOG.warn(ecc::Loc::current(), "writeMapLayoutCsv for explicit cells returned false\n");
    return 1;
  }
  const auto gcell_lines = readLines(gcell_dir / "layout.csv");
  if (gcell_lines.size() != 3) {
    ECCLOG.warn(ecc::Loc::current(), "expected header plus 2 gcell rows, got ", gcell_lines.size());
    return 1;
  }
  expectEqual(gcell_lines[1], "0,0,0,7,10,30,20,40", "explicit gcell row 0 mismatch");
  expectEqual(gcell_lines[2], "0,1,1,7,20,30,35,40", "explicit gcell row 1 mismatch");

  const auto gcell_info = out_dir / "gcell.info";
  {
    std::ofstream file(gcell_info);
    file << "0,0,100,200,110,210\n0,1,100,210,110,220\n"
            "1,0,110,200,120,210\n1,1,110,210,120,220\n"
            "2,0,120,200,130,210\n2,1,120,210,130,220\n"
            "3,0,130,200,140,210\n";
  }
  if (!ieval::writeEGRLayoutCsv(gcell_dir.string(), gcell_info.string(), 3, 2)) {
    ECCLOG.warn(ecc::Loc::current(), "writeEGRLayoutCsv returned false\n");
    return 1;
  }
  const auto egr_lines = readLines(gcell_dir / "layout.csv");
  if (egr_lines.size() != 7) {
    ECCLOG.warn(ecc::Loc::current(), "expected header plus 6 EGR cells, got ", egr_lines.size());
    return 1;
  }
  expectEqual(egr_lines[1], "1,0,0,0,100,200,110,210", "low-Y gcell should map to last pixel row");
  expectEqual(egr_lines[2], "0,0,0,1,100,210,110,220", "high-Y gcell should map to first pixel row");
  expectEqual(egr_lines[6], "0,2,2,1,120,210,130,220", "rightmost high-Y gcell mismatch");

  // An overflow CSV may exist without a current gcell grid. A leftover
  // layout from the previous pass must not be used to publish a display.
  const auto overflow_csv = gcell_dir / "new_overflow.csv";
  ieval::writeEGRMapDisplay(overflow_csv.string(), (gcell_dir / "missing.info").string(), {{2.0}});
  if (std::filesystem::exists(gcell_dir / "new_overflow.gdisplay")) {
    return 1;
  }
  std::filesystem::remove_all(out_dir);
  return 0;
}

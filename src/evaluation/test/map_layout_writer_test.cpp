#include "map_layout_writer.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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
    std::cerr << message << "\nexpected: " << expected << "\nactual:   " << actual << "\n";
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
    std::cerr << "writeMapLayoutCsv returned false\n";
    return 1;
  }

  const auto lines = readLines(out_dir / "layout.csv");
  if (lines.size() != 7) {
    std::cerr << "expected header plus 6 layout rows, got " << lines.size() << "\n";
    return 1;
  }

  expectEqual(lines[0], "pixel_row,pixel_col,grid_x,grid_y,lx,ly,ux,uy", "header mismatch");
  expectEqual(lines[1], "0,0,0,1,100,210,110,218", "top-left pixel should map to highest grid y");
  expectEqual(lines[3], "0,2,2,1,120,210,125,218", "last column should clamp ux to region ux");
  expectEqual(lines[4], "1,0,0,0,100,200,110,210", "second pixel row should map to bottom grid y");
  expectEqual(lines[6], "1,2,2,0,120,200,125,210", "bottom-right pixel bbox mismatch");

  const std::vector<ieval::MapLayoutCell> gcell_layout = {
      {.pixel_row = 0, .pixel_col = 0, .grid_x = 0, .grid_y = 7, .lx = 10, .ly = 30, .ux = 20, .uy = 40},
      {.pixel_row = 0, .pixel_col = 1, .grid_x = 1, .grid_y = 7, .lx = 20, .ly = 30, .ux = 35, .uy = 40},
  };
  const std::filesystem::path gcell_dir = out_dir / "egr";
  std::filesystem::create_directories(gcell_dir);
  if (!ieval::writeMapLayoutCsv(gcell_dir.string(), gcell_layout)) {
    std::cerr << "writeMapLayoutCsv for explicit cells returned false\n";
    return 1;
  }
  const auto gcell_lines = readLines(gcell_dir / "layout.csv");
  if (gcell_lines.size() != 3) {
    std::cerr << "expected header plus 2 gcell rows, got " << gcell_lines.size() << "\n";
    return 1;
  }
  expectEqual(gcell_lines[1], "0,0,0,7,10,30,20,40", "explicit gcell row 0 mismatch");
  expectEqual(gcell_lines[2], "0,1,1,7,20,30,35,40", "explicit gcell row 1 mismatch");

  std::filesystem::remove_all(out_dir);
  return 0;
}

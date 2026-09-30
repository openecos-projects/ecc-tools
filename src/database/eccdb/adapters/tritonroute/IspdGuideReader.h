// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eccdb::tritonroute::guide_input {

struct Rectangle
{
  int x1 = 0;
  int y1 = 0;
  int x2 = 0;
  int y2 = 0;
  std::string layer;
};

struct GridStatistics
{
  std::map<int, std::size_t> widths;
  std::map<int, std::size_t> heights;
  int min_x = std::numeric_limits<int>::max();
  int min_y = std::numeric_limits<int>::max();
};

inline int parseCoordinate(std::string_view text)
{
  int result = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
    throw std::runtime_error("invalid guide coordinate");
  }
  return result;
}

// ISPD guide files contain a net name followed by parenthesized rectangles.
template <typename Consumer>
GridStatistics readGuides(const std::filesystem::path& path, Consumer&& consumer)
{
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open guide file: " + path.string());
  GridStatistics grid;
  std::string name;
  std::string token;
  while (input >> name) {
    if (!(input >> token) || token != "(") {
      throw std::runtime_error("expected '(' after guide net name " + name);
    }
    std::vector<Rectangle> rectangles;
    bool closed = false;
    while (input >> token) {
      if (token == ")") {
        closed = true;
        break;
      }
      Rectangle rectangle;
      rectangle.x1 = parseCoordinate(token);
      if (!(input >> rectangle.y1 >> rectangle.x2 >> rectangle.y2 >> rectangle.layer)) {
        throw std::runtime_error("truncated guide for net " + name);
      }
      if (rectangle.x1 >= rectangle.x2 || rectangle.y1 >= rectangle.y2) {
        throw std::runtime_error("invalid guide rectangle");
      }
      ++grid.widths[rectangle.x2 - rectangle.x1];
      ++grid.heights[rectangle.y2 - rectangle.y1];
      grid.min_x = std::min(grid.min_x, rectangle.x1);
      grid.min_y = std::min(grid.min_y, rectangle.y1);
      rectangles.push_back(std::move(rectangle));
    }
    if (!closed) throw std::runtime_error("unterminated guide for net " + name);
    consumer(name, rectangles);
  }
  if (!input.eof()) throw std::runtime_error("failed to read guides");
  return grid;
}

}  // namespace eccdb::tritonroute::guide_input

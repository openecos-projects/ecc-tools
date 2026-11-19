// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ista {
struct CellCatalogEntry
{
  std::string family;
  std::string vt;
  double drive_rank;
};
using CellCatalog = std::map<std::string, CellCatalogEntry>;

// Tool-owned TSV contract: master family vt drive_rank, one entry per line.
// An empty catalog deliberately leaves every master in its own family.
inline CellCatalog readCellCatalog(const std::string& path)
{
  CellCatalog result;
  if (path.empty())
    return result;
  std::ifstream stream(path);
  if (!stream)
    throw std::invalid_argument("cannot read cell catalog: " + path);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    std::istringstream row(line);
    std::string master, trailing;
    CellCatalogEntry entry;
    if (!(row >> master >> entry.family >> entry.vt >> entry.drive_rank) || (row >> trailing)
        || !std::isfinite(entry.drive_rank) || entry.drive_rank < 0.0 || !result.emplace(master, entry).second)
      throw std::invalid_argument("invalid or duplicate cell catalog row: " + line);
  }
  return result;
}
}  // namespace ista

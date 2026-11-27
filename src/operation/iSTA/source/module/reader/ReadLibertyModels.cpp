// SPDX-License-Identifier: MulanPSL-2.0
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

#include "Lib.hh"
#include "ReadLiberty.hpp"
#include "liberty/LibertyLibrary.hpp"

namespace ista {
namespace {
bool earlyCheck(const TimingCheckArc& arc)
{
  const auto type = arc.get_check_type();
  return type == TimingCheckType::kHold || type == TimingCheckType::kRemoval || type == TimingCheckType::kClockGatingHold;
}

void mergeChecks(std::vector<TimingCheckArc>& late, std::vector<TimingCheckArc>& early)
{
  std::erase_if(late, earlyCheck);
  for (auto& arc : early)
    if (earlyCheck(arc))
      late.push_back(std::move(arc));
}

void mergeModel(LibertyCell& late, LibertyCell early)
{
  const auto fail = [&] { throw std::invalid_argument("Incompatible min/max Liberty models for cell '" + late.get_cell_name() + "'"); };
  if (late.get_port_map().size() != early.get_port_map().size() || std::abs(late.get_nom_voltage() - early.get_nom_voltage()) > 1e-9)
    fail();
  for (const auto& [name, port] : late.get_port_map()) {
    const auto found = early.get_port_map().find(name);
    if (found == early.get_port_map().end() || port.get_is_input() != found->second.get_is_input() || port.get_is_output() != found->second.get_is_output()
        || port.get_is_clock() != found->second.get_is_clock())
      fail();
  }
  for (auto& [name, port] : late.get_port_map()) {
    auto& early_port = early.get_port_map().at(name);
    for (auto transition : {TransType::kRise, TransType::kFall}) {
      auto& caps = early_port.get_trans_capacitance_map()[AnalysisType::kMin];
      port.get_trans_capacitance_map()[AnalysisType::kMin][transition] = caps.contains(transition) ? caps.at(transition) : early_port.get_capacitance();
    }
  }
  // Keep separate arc-family identities: propagation chooses an analysis
  // lane, rather than taking numerical extrema across different models.
  for (auto& arc : late.get_cell_arc_list())
    arc.setAnalysisType(AnalysisType::kMax);
  for (auto& arc : early.get_cell_arc_list()) {
    arc.setAnalysisType(AnalysisType::kMin);
    late.get_cell_arc_list().push_back(std::move(arc));
  }
  mergeChecks(late.get_check_arc_list(), early.get_check_arc_list());
  mergeChecks(late.get_sdf_check_arc_list(), early.get_sdf_check_arc_list());
}
}  // namespace

void ReadLiberty::readCells(const std::vector<idb::LibLibrary*>& libraries, const std::vector<std::string>& min_paths)
{
  std::set<std::filesystem::path> requested;
  for (const auto& path : min_paths)
    if (!requested.insert(std::filesystem::weakly_canonical(path)).second)
      throw std::invalid_argument("Duplicate min Liberty path: " + path);
  std::vector<idb::LibLibrary*> early_libraries;
  std::map<std::string, std::vector<idb::LibCell*>> late_cells;
  _liberty.get_cell_map().clear();
  for (auto* library : libraries) {
    if (requested.erase(std::filesystem::weakly_canonical(library->get_file_name()))) {
      early_libraries.push_back(library);
      continue;
    }
    for (const auto& cell : library->get_cells()) {
      late_cells[cell->get_cell_name()].push_back(cell.get());
      _liberty.get_cell_map()[cell->get_cell_name()] = readCell(cell.get());
    }
  }
  if (!requested.empty())
    throw std::invalid_argument("Min Liberty was not loaded by lib_init: " + requested.begin()->string());
  std::set<std::string> overridden;
  for (auto* library : early_libraries) {
    if (library->get_cells().empty())
      throw std::invalid_argument("Empty min Liberty: " + std::string(library->get_file_name()));
    for (const auto& cell : library->get_cells()) {
      const std::string name = cell->get_cell_name();
      const auto found = late_cells.find(name);
      if (found == late_cells.end() || found->second.size() != 1 || !overridden.insert(name).second)
        throw std::invalid_argument("Min Liberty needs one unambiguous max model for cell '" + name + "'");
      const auto* late_library = found->second.front()->get_owner_lib();
      if (late_library->get_nom_temperature() != library->get_nom_temperature() || late_library->get_nom_process() != library->get_nom_process())
        throw std::invalid_argument("Min/max Liberty PVT mismatch for cell '" + name + "'");
      mergeModel(_liberty.get_cell_map().at(name), readCell(cell.get()));
    }
  }
}
}  // namespace ista

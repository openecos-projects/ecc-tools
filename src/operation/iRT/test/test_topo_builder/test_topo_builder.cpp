#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Logger.hpp"
#include "PlanarRect.hpp"
#include "TOPOBuilder.hpp"
#include "TopoSvgPlotter.hpp"

#include "utility/logger/Logger.hpp"
namespace {

using irt::PlanarCoord;
using irt::PlanarRect;
using irt::Segment;

struct PlotConfig
{
  std::filesystem::path output_directory;

  bool isEnabled() const { return !output_directory.empty(); }
};

struct TopoRunResult
{
  std::vector<Segment<PlanarCoord>> flute_topo_list;
  std::vector<Segment<PlanarCoord>> legal_topo_list;
  irt::TBSteinerRepairStat steiner_repair_stat;
};

bool parsePlotConfig(int argc, char* argv[], PlotConfig& plot_config)
{
  if (argc == 1) {
    return true;
  }
  if (argc == 3 && std::string(argv[1]) == "--plot-dir" && !std::string(argv[2]).empty()) {
    plot_config.output_directory = argv[2];
    return true;
  }
  ECCLOG.warn(ecc::Loc::current(), "Usage: test_topo_builder [--plot-dir <directory>]");
  return false;
}

bool check(bool condition, const std::string& case_name)
{
  if (!condition) {
    ECCLOG.warn(ecc::Loc::current(), "Failed: ", case_name);
  }
  return condition;
}

bool isSameTopo(const std::vector<Segment<PlanarCoord>>& expected, const std::vector<Segment<PlanarCoord>>& actual)
{
  if (expected.size() != actual.size()) {
    return false;
  }
  for (size_t i = 0; i < expected.size(); ++i) {
    if (expected[i].get_first() != actual[i].get_first() || expected[i].get_second() != actual[i].get_second()) {
      return false;
    }
  }
  return true;
}

bool isInsideAnyObs(const PlanarCoord& coord, const std::vector<PlanarRect>& planar_obs_list)
{
  for (const PlanarRect& planar_obs : planar_obs_list) {
    if (planar_obs.get_ll_x() <= coord.get_x() && coord.get_x() <= planar_obs.get_ur_x() && planar_obs.get_ll_y() <= coord.get_y()
        && coord.get_y() <= planar_obs.get_ur_y()) {
      return true;
    }
  }
  return false;
}

std::vector<PlanarCoord> getSteinerCoordList(const std::vector<PlanarCoord>& terminal_list, const std::vector<Segment<PlanarCoord>>& planar_topo_list)
{
  std::set<PlanarCoord, irt::CmpPlanarCoordByXASC> terminal_coord_set(terminal_list.begin(), terminal_list.end());
  std::set<PlanarCoord, irt::CmpPlanarCoordByXASC> steiner_coord_set;
  for (const Segment<PlanarCoord>& planar_topo : planar_topo_list) {
    for (const PlanarCoord& coord : {planar_topo.get_first(), planar_topo.get_second()}) {
      if (terminal_coord_set.find(coord) == terminal_coord_set.end()) {
        steiner_coord_set.insert(coord);
      }
    }
  }
  return std::vector<PlanarCoord>(steiner_coord_set.begin(), steiner_coord_set.end());
}

bool hasOnlyLegalSteiner(const std::vector<PlanarCoord>& terminal_list, const std::vector<Segment<PlanarCoord>>& planar_topo_list,
                         const std::vector<PlanarRect>& planar_obs_list)
{
  for (const PlanarCoord& steiner_coord : getSteinerCoordList(terminal_list, planar_topo_list)) {
    if (isInsideAnyObs(steiner_coord, planar_obs_list)) {
      return false;
    }
  }
  return true;
}

irt::TBTask makeTask(const std::vector<PlanarCoord>& terminal_list, std::vector<PlanarRect> planar_obs_list = {},
                     PlanarRect planar_search_region = PlanarRect(0, 0, 49, 49))
{
  irt::TBTask tb_task;
  tb_task.set_planar_coord_list(terminal_list);
  tb_task.set_planar_obs_list(std::move(planar_obs_list));
  tb_task.set_planar_search_region(planar_search_region);
  return tb_task;
}

TopoRunResult runTopoCase(const std::vector<PlanarCoord>& terminal_list, const std::vector<PlanarRect>& planar_obs_list, const PlanarRect& planar_search_region)
{
  TopoRunResult result;
  result.flute_topo_list = RTTB.getPlanarTopoList(makeTask(terminal_list, {}, planar_search_region));
  result.steiner_repair_stat = {1, 1, 1};
  result.legal_topo_list = RTTB.getPlanarTopoList(makeTask(terminal_list, planar_obs_list, planar_search_region), result.steiner_repair_stat);
  return result;
}

bool checkRepairStat(const irt::TBSteinerRepairStat& stat, int32_t raw_steiner_num, int32_t fixed_steiner_num, int32_t failed_steiner_num,
                     const std::string& case_name)
{
  bool passed = true;
  passed = check(stat.raw_steiner_in_macro == raw_steiner_num, case_name + " raw Steiner statistic") && passed;
  passed = check(stat.fixed_steiner_in_macro == fixed_steiner_num, case_name + " fixed Steiner statistic") && passed;
  passed = check(stat.failed_steiner_legalize_num == failed_steiner_num, case_name + " failed Steiner statistic") && passed;
  return passed;
}

bool writeTopoPlot(const PlotConfig& plot_config, const std::string& case_id, const std::string& title, const PlanarRect& planar_search_region,
                   const std::vector<PlanarRect>& planar_obs_list, const std::vector<PlanarCoord>& terminal_list, const TopoRunResult& topo_run_result)
{
  if (!plot_config.isEnabled()) {
    return true;
  }
  std::filesystem::path file_path = plot_config.output_directory / case_id / "topology.svg";
  irt::TopoSvgPlotRequest request{
      title, planar_search_region, planar_obs_list, terminal_list, topo_run_result.flute_topo_list, topo_run_result.legal_topo_list};
  std::string error_message;
  if (!irt::writeTopoSvg(file_path.string(), request, error_message)) {
    ECCLOG.warn(ecc::Loc::current(), "Failed to write ", file_path, ": ", error_message);
    return false;
  }

  std::ifstream input_stream(file_path);
  std::string svg_content((std::istreambuf_iterator<char>(input_stream)), std::istreambuf_iterator<char>());
  if (svg_content.find("<svg") == std::string::npos || svg_content.find("data-layer=\"obstacle\"") == std::string::npos
      || svg_content.find("data-layer=\"flute-topology\"") == std::string::npos
      || svg_content.find("data-layer=\"legal-topology\"") == std::string::npos) {
    ECCLOG.warn(ecc::Loc::current(), "The SVG output is incomplete: ", file_path);
    return false;
  }
  return true;
}

const PlanarRect& getDefaultSearchRegion()
{
  static const PlanarRect search_region(0, 0, 49, 49);
  return search_region;
}

std::vector<PlanarCoord> getBaseTerminalList()
{
  return {PlanarCoord(0, 0), PlanarCoord(10, 30), PlanarCoord(30, 10), PlanarCoord(40, 40)};
}

std::vector<Segment<PlanarCoord>> getBaseFluteTopoList()
{
  return {{PlanarCoord(0, 0), PlanarCoord(30, 10)},
          {PlanarCoord(10, 30), PlanarCoord(30, 30)},
          {PlanarCoord(40, 40), PlanarCoord(30, 30)},
          {PlanarCoord(30, 10), PlanarCoord(30, 30)}};
}

bool checkCase1Baseline(const PlotConfig& plot_config)
{
  bool passed = true;
  passed = check(RTTB.getPlanarTopoList(makeTask({})).empty(), "case1 empty FLUTE topology") && passed;
  passed = check(RTTB.getPlanarTopoList(makeTask({PlanarCoord(10, 20)})).empty(), "case1 single-pin FLUTE topology") && passed;

  std::vector<Segment<PlanarCoord>> two_pin_expected = {{PlanarCoord(10, 20), PlanarCoord(40, 20)}};
  passed = check(isSameTopo(two_pin_expected, RTTB.getPlanarTopoList(makeTask({PlanarCoord(10, 20), PlanarCoord(40, 20)}))), "case1 two-pin FLUTE topology")
           && passed;

  std::vector<PlanarCoord> terminal_list = getBaseTerminalList();
  TopoRunResult result = runTopoCase(terminal_list, {}, getDefaultSearchRegion());
  passed = writeTopoPlot(plot_config, "case1", "Baseline without obstacles", getDefaultSearchRegion(), {}, terminal_list, result) && passed;
  passed = check(isSameTopo(getBaseFluteTopoList(), result.flute_topo_list), "case1 multi-pin FLUTE topology") && passed;
  passed = check(isSameTopo(result.flute_topo_list, result.legal_topo_list), "case1 no-obstacle topology remains unchanged") && passed;
  passed = checkRepairStat(result.steiner_repair_stat, 0, 0, 0, "case1") && passed;
  return passed;
}

bool checkCase2IrrelevantObstacle(const PlotConfig& plot_config)
{
  std::vector<PlanarCoord> terminal_list = getBaseTerminalList();
  std::vector<PlanarRect> planar_obs_list = {PlanarRect(45, 0, 49, 4)};
  TopoRunResult result = runTopoCase(terminal_list, planar_obs_list, getDefaultSearchRegion());
  bool passed = true;
  passed = writeTopoPlot(plot_config, "case2", "Irrelevant obstacle", getDefaultSearchRegion(), planar_obs_list, terminal_list, result) && passed;
  passed = check(isSameTopo(result.flute_topo_list, result.legal_topo_list), "case2 topology remains unchanged") && passed;
  passed = checkRepairStat(result.steiner_repair_stat, 0, 0, 0, "case2") && passed;
  return passed;
}

bool checkCase3SingleCellObstacle(const PlotConfig& plot_config)
{
  std::vector<PlanarCoord> terminal_list = getBaseTerminalList();
  std::vector<PlanarRect> planar_obs_list = {PlanarRect(30, 30, 30, 30)};
  TopoRunResult result = runTopoCase(terminal_list, planar_obs_list, getDefaultSearchRegion());
  std::vector<Segment<PlanarCoord>> expected = {{PlanarCoord(0, 0), PlanarCoord(30, 10)},
                                                {PlanarCoord(10, 30), PlanarCoord(29, 30)},
                                                {PlanarCoord(40, 40), PlanarCoord(29, 30)},
                                                {PlanarCoord(30, 10), PlanarCoord(29, 30)}};
  bool passed = true;
  passed = writeTopoPlot(plot_config, "case3", "Single-cell Steiner legalization", getDefaultSearchRegion(), planar_obs_list, terminal_list, result) && passed;
  passed = check(isSameTopo(expected, result.legal_topo_list), "case3 nearest legal Steiner point") && passed;
  passed = checkRepairStat(result.steiner_repair_stat, 1, 1, 0, "case3") && passed;
  return passed;
}

bool checkCase4RectangularObstacle(const PlotConfig& plot_config)
{
  std::vector<PlanarCoord> terminal_list = getBaseTerminalList();
  std::vector<PlanarRect> planar_obs_list = {PlanarRect(30, 30, 31, 31)};
  TopoRunResult result = runTopoCase(terminal_list, planar_obs_list, getDefaultSearchRegion());
  std::vector<Segment<PlanarCoord>> expected = {{PlanarCoord(0, 0), PlanarCoord(30, 10)},
                                                {PlanarCoord(10, 30), PlanarCoord(29, 30)},
                                                {PlanarCoord(40, 40), PlanarCoord(29, 30)},
                                                {PlanarCoord(30, 10), PlanarCoord(29, 30)}};
  bool passed = true;
  passed = writeTopoPlot(plot_config, "case4", "Rectangular Steiner legalization", getDefaultSearchRegion(), planar_obs_list, terminal_list, result) && passed;
  passed = check(isSameTopo(expected, result.legal_topo_list), "case4 rectangular obstacle legalization") && passed;
  passed = checkRepairStat(result.steiner_repair_stat, 1, 1, 0, "case4") && passed;
  return passed;
}

bool checkCase5TerminalInsideObstacle(const PlotConfig& plot_config)
{
  std::vector<PlanarCoord> terminal_list = getBaseTerminalList();
  std::vector<PlanarRect> planar_obs_list = {PlanarRect(30, 10, 30, 10)};
  TopoRunResult result = runTopoCase(terminal_list, planar_obs_list, getDefaultSearchRegion());
  bool passed = true;
  passed = writeTopoPlot(plot_config, "case5", "Terminal inside obstacle", getDefaultSearchRegion(), planar_obs_list, terminal_list, result) && passed;
  passed = check(isInsideAnyObs(PlanarCoord(30, 10), planar_obs_list), "case5 terminal belongs to obstacle") && passed;
  passed = check(isSameTopo(result.flute_topo_list, result.legal_topo_list), "case5 terminal remains unchanged") && passed;
  passed = checkRepairStat(result.steiner_repair_stat, 0, 0, 0, "case5") && passed;
  return passed;
}

bool checkCase6OverlappingObstacles(const PlotConfig& plot_config)
{
  std::vector<PlanarCoord> terminal_list = getBaseTerminalList();
  std::vector<PlanarRect> planar_obs_list = {PlanarRect(30, 30, 31, 31), PlanarRect(31, 29, 32, 31)};
  TopoRunResult result = runTopoCase(terminal_list, planar_obs_list, getDefaultSearchRegion());
  std::vector<Segment<PlanarCoord>> expected = {{PlanarCoord(0, 0), PlanarCoord(30, 10)},
                                                {PlanarCoord(10, 30), PlanarCoord(29, 30)},
                                                {PlanarCoord(40, 40), PlanarCoord(29, 30)},
                                                {PlanarCoord(30, 10), PlanarCoord(29, 30)}};
  bool passed = true;
  passed = writeTopoPlot(plot_config, "case6", "Overlapping obstacles", getDefaultSearchRegion(), planar_obs_list, terminal_list, result) && passed;
  passed = check(isSameTopo(expected, result.legal_topo_list), "case6 obstacle union legalization") && passed;
  passed = checkRepairStat(result.steiner_repair_stat, 1, 1, 0, "case6") && passed;
  return passed;
}

bool checkCase7SearchBoundary(const PlotConfig& plot_config)
{
  std::vector<PlanarCoord> terminal_list = getBaseTerminalList();
  PlanarRect planar_search_region(30, 30, 30, 30);
  std::vector<PlanarRect> planar_obs_list = {PlanarRect(30, 30, 30, 30)};
  TopoRunResult result = runTopoCase(terminal_list, planar_obs_list, planar_search_region);
  bool passed = true;
  passed = writeTopoPlot(plot_config, "case7", "Search-region boundary", planar_search_region, planar_obs_list, terminal_list, result) && passed;
  passed = check(isSameTopo(result.flute_topo_list, result.legal_topo_list), "case7 does not escape search region") && passed;
  passed = checkRepairStat(result.steiner_repair_stat, 1, 0, 1, "case7") && passed;
  return passed;
}

bool checkCase8FullyBlocked(const PlotConfig& plot_config)
{
  std::vector<PlanarCoord> terminal_list = getBaseTerminalList();
  std::vector<PlanarRect> planar_obs_list = {getDefaultSearchRegion()};
  TopoRunResult result = runTopoCase(terminal_list, planar_obs_list, getDefaultSearchRegion());
  bool passed = true;
  passed = writeTopoPlot(plot_config, "case8", "Fully blocked search region", getDefaultSearchRegion(), planar_obs_list, terminal_list, result) && passed;
  passed = check(isSameTopo(result.flute_topo_list, result.legal_topo_list), "case8 keeps raw topology") && passed;
  passed = checkRepairStat(result.steiner_repair_stat, 1, 0, 1, "case8") && passed;
  return passed;
}

bool checkCase9MultipleSteiner(const PlotConfig& plot_config)
{
  std::vector<PlanarCoord> terminal_list
      = {PlanarCoord(0, 0), PlanarCoord(0, 40), PlanarCoord(10, 15), PlanarCoord(25, 30), PlanarCoord(40, 0), PlanarCoord(40, 40)};
  TopoRunResult baseline_result = runTopoCase(terminal_list, {}, getDefaultSearchRegion());
  std::vector<PlanarCoord> raw_steiner_coord_list = getSteinerCoordList(terminal_list, baseline_result.flute_topo_list);
  if (!check(raw_steiner_coord_list.size() >= 2, "case9 produces multiple raw Steiner points")) {
    return false;
  }

  std::vector<PlanarRect> planar_obs_list;
  planar_obs_list.reserve(raw_steiner_coord_list.size());
  for (const PlanarCoord& raw_steiner_coord : raw_steiner_coord_list) {
    planar_obs_list.emplace_back(raw_steiner_coord, raw_steiner_coord);
  }
  TopoRunResult result = runTopoCase(terminal_list, planar_obs_list, getDefaultSearchRegion());
  int32_t raw_steiner_num = static_cast<int32_t>(raw_steiner_coord_list.size());
  bool passed = true;
  passed = writeTopoPlot(plot_config, "case9", "Multiple Steiner legalizations", getDefaultSearchRegion(), planar_obs_list, terminal_list, result) && passed;
  passed = check(isSameTopo(baseline_result.flute_topo_list, result.flute_topo_list), "case9 raw FLUTE topology is stable") && passed;
  passed = check(hasOnlyLegalSteiner(terminal_list, result.legal_topo_list, planar_obs_list), "case9 all Steiner points are legal") && passed;
  passed = checkRepairStat(result.steiner_repair_stat, raw_steiner_num, raw_steiner_num, 0, "case9") && passed;
  return passed;
}

bool checkCase10CollapsedSteinerEdge(const PlotConfig& plot_config)
{
  std::vector<PlanarCoord> terminal_list = getBaseTerminalList();
  std::vector<PlanarRect> planar_obs_list = {PlanarRect(0, 0, 29, 49), PlanarRect(31, 0, 49, 49), PlanarRect(30, 0, 30, 9), PlanarRect(30, 11, 30, 49)};
  TopoRunResult result = runTopoCase(terminal_list, planar_obs_list, getDefaultSearchRegion());
  std::vector<Segment<PlanarCoord>> expected
      = {{PlanarCoord(0, 0), PlanarCoord(30, 10)}, {PlanarCoord(10, 30), PlanarCoord(30, 10)}, {PlanarCoord(40, 40), PlanarCoord(30, 10)}};
  bool passed = true;
  passed = writeTopoPlot(plot_config, "case10", "Collapsed Steiner edge", getDefaultSearchRegion(), planar_obs_list, terminal_list, result) && passed;
  passed = check(isSameTopo(expected, result.legal_topo_list), "case10 removes collapsed edge") && passed;
  passed = check(result.legal_topo_list.size() + 1 == result.flute_topo_list.size(), "case10 removes exactly one edge") && passed;
  passed = checkRepairStat(result.steiner_repair_stat, 1, 1, 0, "case10") && passed;
  return passed;
}

bool checkInvalidObstacleRect()
{
  std::vector<PlanarCoord> terminal_list = getBaseTerminalList();
  std::vector<PlanarRect> planar_obs_list = {PlanarRect(31, 30, 30, 30)};
  TopoRunResult result = runTopoCase(terminal_list, planar_obs_list, getDefaultSearchRegion());
  bool passed = true;
  passed = check(isSameTopo(result.flute_topo_list, result.legal_topo_list), "invalid obstacle keeps raw topology") && passed;
  passed = checkRepairStat(result.steiner_repair_stat, 0, 0, 0, "invalid obstacle") && passed;
  return passed;
}

bool checkMissingSearchRegion()
{
  std::vector<PlanarCoord> terminal_list = getBaseTerminalList();
  std::vector<PlanarRect> planar_obs_list = {PlanarRect(30, 30, 30, 30)};
  std::vector<Segment<PlanarCoord>> flute_topo_list = RTTB.getPlanarTopoList(makeTask(terminal_list));
  irt::TBTask tb_task;
  tb_task.set_planar_coord_list(terminal_list);
  tb_task.set_planar_obs_list(planar_obs_list);
  irt::TBSteinerRepairStat stat = {1, 1, 1};
  std::vector<Segment<PlanarCoord>> legal_topo_list = RTTB.getPlanarTopoList(tb_task, stat);
  bool passed = true;
  passed = check(isSameTopo(flute_topo_list, legal_topo_list), "missing search region keeps raw topology") && passed;
  passed = checkRepairStat(stat, 0, 0, 0, "missing search region") && passed;
  return passed;
}

}  // namespace

int main(int argc, char* argv[])
{
  PlotConfig plot_config;
  if (!parsePlotConfig(argc, argv, plot_config)) {
    return 2;
  }

  irt::Logger::initInst();
  irt::TOPOBuilder::initInst();
  RTTB.init();

  bool passed = true;
  passed = checkCase1Baseline(plot_config) && passed;
  passed = checkCase2IrrelevantObstacle(plot_config) && passed;
  passed = checkCase3SingleCellObstacle(plot_config) && passed;
  passed = checkCase4RectangularObstacle(plot_config) && passed;
  passed = checkCase5TerminalInsideObstacle(plot_config) && passed;
  passed = checkCase6OverlappingObstacles(plot_config) && passed;
  passed = checkCase7SearchBoundary(plot_config) && passed;
  passed = checkCase8FullyBlocked(plot_config) && passed;
  passed = checkCase9MultipleSteiner(plot_config) && passed;
  passed = checkCase10CollapsedSteinerEdge(plot_config) && passed;
  passed = checkInvalidObstacleRect() && passed;
  passed = checkMissingSearchRegion() && passed;

  RTTB.destroy();
  irt::TOPOBuilder::destroyInst();
  irt::Logger::destroyInst();
  return passed ? 0 : 1;
}

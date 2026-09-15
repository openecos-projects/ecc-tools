/*
 * @FilePath: wirelength_app.cpp
 * @Author: Yihang Qiu (qiuyihang23@mails.ucas.ac.cn)
 * @Date: 2024-08-24 15:37:27
 * @Description:
 */

#include "utility/logger/Logger.hpp"
#include <iostream>

#include "idm.h"
#include "wirelength_api.h"

void TestTotalWirelength();
void TestNetWirelength();
void TestPathWirelength();
void TestEgrWirelength(std::string guide_path);
void TestWirelengthEvalFromIDB(const std::string& db_config_path);

void PrintUsage(const char* program_name) {
  ECCLOG.info(ecc::Loc::current(), "Wirelength Evaluation");
  ECCLOG.info(ecc::Loc::current(), "Usage: ", program_name, " <function_name>");
  ECCLOG.info(ecc::Loc::current(), "Available parameters:");
  ECCLOG.info(ecc::Loc::current(), "  <db_config_path> Path to the database configuration file.");
  ECCLOG.info(ecc::Loc::current(), "  --help, -h       Show this help message and exit.");
}

int main(const int argc, const char* argv[])
{
  if (argc == 2) {
    if (const std::string arg = argv[1]; arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      return 0;
    } else {
      ECCLOG.info(ecc::Loc::current(), "db_config_path: ", arg);
      TestWirelengthEvalFromIDB(arg);
      // Here are some test functions that can be uncommented to run
      // TestTotalWirelength();
      // TestNetWirelength();
      // TestPathWirelength();
      // TestEgrWirelength("./rt_temp_directory/early_router/route.guide");
      return 0;
    }
  }

  return 0;
}

void TestTotalWirelength()
{
  ieval::WirelengthAPI wirelength_api;

  std::vector<std::pair<int32_t, int32_t>> point_set;
  std::pair<int32_t, int32_t> point1(0, 0);
  std::pair<int32_t, int32_t> point2(3, 6);
  std::pair<int32_t, int32_t> point3(4, 4);
  std::pair<int32_t, int32_t> point4(6, 3);

  point_set.push_back(point1);
  point_set.push_back(point2);
  point_set.push_back(point3);
  point_set.push_back(point4);

  std::vector<std::vector<std::pair<int32_t, int32_t>>> point_sets;
  point_sets.push_back(point_set);

  ieval::TotalWLSummary total_wl = wirelength_api.totalWL(point_sets);

  ECCLOG.info(ecc::Loc::current(), "Total HPWL: ", total_wl.HPWL);
  ECCLOG.info(ecc::Loc::current(), "Total FLUTE: ", total_wl.FLUTE);
  ECCLOG.info(ecc::Loc::current(), "Total HTree: ", total_wl.HTree);
  ECCLOG.info(ecc::Loc::current(), "Total VTree: ", total_wl.VTree);
}

void TestNetWirelength()
{
  ieval::WirelengthAPI wirelength_api;

  std::vector<std::pair<int32_t, int32_t>> point_set;
  std::pair<int32_t, int32_t> point1(0, 0);
  std::pair<int32_t, int32_t> point2(3, 6);
  std::pair<int32_t, int32_t> point3(4, 4);
  std::pair<int32_t, int32_t> point4(6, 3);

  point_set.push_back(point1);
  point_set.push_back(point2);
  point_set.push_back(point3);
  point_set.push_back(point4);

  ieval::NetWLSummary net_wl = wirelength_api.netWL(point_set);

  ECCLOG.info(ecc::Loc::current(), "Net HPWL: ", net_wl.HPWL);
  ECCLOG.info(ecc::Loc::current(), "Net FLUTE: ", net_wl.FLUTE);
  ECCLOG.info(ecc::Loc::current(), "Net HTree: ", net_wl.HTree);
  ECCLOG.info(ecc::Loc::current(), "Net VTree: ", net_wl.VTree);
}

void TestPathWirelength()
{
  ieval::WirelengthAPI wirelength_api;

  std::vector<std::pair<int32_t, int32_t>> point_set;
  std::pair<int32_t, int32_t> point1(0, 0);
  std::pair<int32_t, int32_t> point2(3, 6);
  std::pair<int32_t, int32_t> point3(4, 4);
  std::pair<int32_t, int32_t> point4(6, 3);

  point_set.push_back(point1);
  point_set.push_back(point2);
  point_set.push_back(point3);
  point_set.push_back(point4);

  std::pair<int32_t, int32_t> point_pair1(4, 4);
  std::pair<int32_t, int32_t> point_pair2(6, 3);

  ieval::PathWLSummary path_wl = wirelength_api.pathWL(point_set, {point_pair1, point_pair2});

  ECCLOG.info(ecc::Loc::current(), "Path HPWL: ", path_wl.HPWL);
  ECCLOG.info(ecc::Loc::current(), "Path FLUTE: ", path_wl.FLUTE);
  ECCLOG.info(ecc::Loc::current(), "Path HTree: ", path_wl.HTree);
  ECCLOG.info(ecc::Loc::current(), "Path VTree: ", path_wl.VTree);
}

void TestEgrWirelength(std::string guide_path)
{
  ieval::WirelengthAPI wirelength_api;

  float total_egr_wl = wirelength_api.totalEGRWL(guide_path);
  ECCLOG.info(ecc::Loc::current(), "Total EGR WL: ", total_egr_wl);

  float net_egr_wl = wirelength_api.netEGRWL(guide_path, "clk");
  ECCLOG.info(ecc::Loc::current(), "Net EGR WL: ", net_egr_wl);

  float path_egr_wl = wirelength_api.pathEGRWL(guide_path, "clk", "clk_0_buf:I");
  ECCLOG.info(ecc::Loc::current(), "Path EGR WL: ", path_egr_wl);
}

void TestWirelengthEvalFromIDB(const std::string& db_config_path)
{
  dmInst->init(db_config_path);
  ieval::WirelengthAPI wirelength_api;
  ieval::TotalWLSummary wl_summary = wirelength_api.totalWL();
  ECCLOG.info(ecc::Loc::current(), "Total HPWL: ", wl_summary.HPWL);
  // ECCLOG.info(ecc::Loc::current(), "Total FLUTE: ", wl_summary.FLUTE);
  // ECCLOG.info(ecc::Loc::current(), "Total HTree: ", wl_summary.HTree);
  // ECCLOG.info(ecc::Loc::current(), "Total VTree: ", wl_summary.VTree);
  // ECCLOG.info(ecc::Loc::current(), "Total GRWL: ", wl_summary.GRWL);

  // ieval::NetWLSummary net_wl_summary = wirelength_api.netWL("req_msg[31]");
  // ECCLOG.info(ecc::Loc::current(), ">> Net: req_msg[31], Wirelength:");
  // ECCLOG.info(ecc::Loc::current(), "Net HPWL: ", net_wl_summary.HPWL);
  // ECCLOG.info(ecc::Loc::current(), "Net FLUTE: ", net_wl_summary.FLUTE);
  // ECCLOG.info(ecc::Loc::current(), "Net HTree: ", net_wl_summary.HTree);
  // ECCLOG.info(ecc::Loc::current(), "Net VTree: ", net_wl_summary.VTree);
  // ECCLOG.info(ecc::Loc::current(), "Net GRWL: ", net_wl_summary.GRWL);
}

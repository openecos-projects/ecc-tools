// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file CTSAPILifecycleTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-08-10
 * @brief Public CTS destruction lifecycle and session-ownership tests.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "CTSAPI.hh"
#include "IdbDesign.h"
#include "Logger.hh"
#include "Utility.hh"
#include "data_manager/DataManager.hh"
#include "data_manager/io/Wrapper.hh"
#include "idm.h"

namespace icts_test {
namespace {

auto MakeUniqueOutputDir(const std::string& label) -> std::filesystem::path
{
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() / ("icts_lifecycle_" + label + "_" + std::to_string(suffix));
}

void RemoveOutputDir(const std::filesystem::path& output_dir)
{
  std::error_code error_code;
  std::filesystem::remove_all(output_dir, error_code);
}

auto WriteTextFile(const std::filesystem::path& path, const std::string& content) -> bool
{
  std::ofstream stream(path);
  stream << content;
  return stream.good();
}

class ScopedMinimalExternalIdb
{
 public:
  explicit ScopedMinimalExternalIdb(const std::string& label) : _output_dir(MakeUniqueOutputDir(label))
  {
    std::filesystem::create_directories(_output_dir);
    _config_path = _output_dir / "cts_config.json";
    const auto tech_lef_path = _output_dir / "minimal.lef";
    const auto def_path = _output_dir / "minimal.def";
    _work_dir = _output_dir / "cts_work";

    constexpr auto tech_lef = R"lef(VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.10 ;
  WIDTH 0.05 ;
  RESISTANCE RPERSQ 0.10 ;
  CAPACITANCE CPERSQDIST 0.001 ;
END M1
END LIBRARY
)lef";
    constexpr auto design_def = R"def(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN icts_lifecycle ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 1000 1000 ) ;
COMPONENTS 0 ;
END COMPONENTS
PINS 0 ;
END PINS
NETS 0 ;
END NETS
END DESIGN
)def";

    dmInst->reset();
    _ready = WriteTextFile(_config_path, "{}") && WriteTextFile(tech_lef_path, tech_lef) && WriteTextFile(def_path, design_def)
             && dmInst->readLef(std::vector<std::string>{tech_lef_path.string()}, true) && dmInst->readDef(def_path.string());
    if (_ready) {
      dmInst->get_config().set_sdc_path("");
    }
  }

  ~ScopedMinimalExternalIdb()
  {
    (void) icts::CTSAPI::destroyCTS();
    dmInst->reset();
    RemoveOutputDir(_output_dir);
  }

  ScopedMinimalExternalIdb(const ScopedMinimalExternalIdb& rhs) = delete;
  ScopedMinimalExternalIdb(ScopedMinimalExternalIdb&& rhs) = delete;
  auto operator=(const ScopedMinimalExternalIdb& rhs) -> ScopedMinimalExternalIdb& = delete;
  auto operator=(ScopedMinimalExternalIdb&& rhs) -> ScopedMinimalExternalIdb& = delete;

  auto ready() const -> bool { return _ready; }
  auto configPath() const -> const std::filesystem::path& { return _config_path; }
  auto workDir() const -> const std::filesystem::path& { return _work_dir; }

 private:
  std::filesystem::path _output_dir;
  std::filesystem::path _config_path;
  std::filesystem::path _work_dir;
  bool _ready = false;
};

auto Median(std::array<double, 4> samples) -> double
{
  std::ranges::sort(samples);
  return (samples.at(1U) + samples.at(2U)) / 2.0;
}

struct ExternalDesignFingerprint
{
  std::vector<std::string> insts;
  std::vector<std::string> nets;
  std::vector<std::string> pins;

  auto operator==(const ExternalDesignFingerprint& rhs) const -> bool = default;
};

auto Fingerprint(idb::IdbDesign& design) -> ExternalDesignFingerprint
{
  ExternalDesignFingerprint fingerprint;
  for (auto* inst : design.get_instance_list()->get_instance_list()) {
    if (inst == nullptr) {
      continue;
    }
    auto* coordinate = inst->get_coordinate();
    fingerprint.insts.push_back(inst->get_name() + "@" + std::to_string(coordinate == nullptr ? 0 : coordinate->get_x()) + ","
                                + std::to_string(coordinate == nullptr ? 0 : coordinate->get_y()));
    for (auto* pin : inst->get_pin_list()->get_pin_list()) {
      if (pin == nullptr) {
        continue;
      }
      auto* location = pin->get_location();
      fingerprint.pins.push_back(inst->get_name() + "/" + pin->get_pin_name() + "->" + pin->get_net_name() + "@"
                                 + std::to_string(location == nullptr ? 0 : location->get_x()) + ","
                                 + std::to_string(location == nullptr ? 0 : location->get_y()));
    }
  }
  for (auto* net : design.get_net_list()->get_net_list()) {
    if (net == nullptr) {
      continue;
    }
    std::vector<std::string> connections;
    for (auto* pin : net->get_instance_pin_list()->get_pin_list()) {
      if (pin != nullptr && pin->get_instance() != nullptr) {
        connections.push_back(pin->get_instance()->get_name() + "/" + pin->get_pin_name());
      }
    }
    std::ranges::sort(connections);
    std::string record = net->get_net_name();
    for (const auto& connection : connections) {
      record.append("|").append(connection);
    }
    fingerprint.nets.push_back(std::move(record));
  }
  std::ranges::sort(fingerprint.insts);
  std::ranges::sort(fingerprint.nets);
  std::ranges::sort(fingerprint.pins);
  return fingerprint;
}

void PopulateRepresentativeSession(std::size_t inst_count)
{
  for (std::size_t index = 0U; index < inst_count; ++index) {
    ASSERT_NE(CTSDM.getDesign().makeInst("session_inst_" + std::to_string(index)), nullptr);
  }
}

TEST(CTSAPILifecycleTest, DestroyIsSuccessfulBeforeInitializationAndWhenRepeated)
{
  const auto first = icts::CTSAPI::destroyCTS();
  const auto second = icts::CTSAPI::destroyCTS();

  EXPECT_EQ(first.code, icts::CTSStatusCode::kOk);
  EXPECT_EQ(second.code, icts::CTSStatusCode::kOk);
  EXPECT_TRUE(icts::CTSAPI::lastStatus().ok());
  EXPECT_TRUE(icts::CTSAPI::outputClockTiming().empty());
}

TEST(CTSAPILifecycleTest, FailedInitializationUsesCanonicalCleanupAndLeavesAPIUninitialized)
{
  const auto output_dir = MakeUniqueOutputDir("failed_init");
  const auto status = icts::CTSAPI::init((output_dir / "missing_config.json").string(), output_dir.string());

  EXPECT_EQ(status.code, icts::CTSStatusCode::kConfigError);
  EXPECT_EQ(icts::CTSAPI::runCTS().code, icts::CTSStatusCode::kNotInitialized);
  EXPECT_EQ(icts::CTSAPI::report(output_dir.string()).code, icts::CTSStatusCode::kNotInitialized);
  EXPECT_TRUE(icts::CTSAPI::outputClockTiming().empty());
  EXPECT_TRUE(icts::CTSAPI::destroyCTS().ok());
  EXPECT_TRUE(icts::CTSAPI::destroyCTS().ok());

  RemoveOutputDir(output_dir);
}

TEST(CTSAPILifecycleTest, DestroyReleasesRepresentativeSessionAndFreshOwnerStartsEmpty)
{
  icts::DataManager::initInst();
  PopulateRepresentativeSession(256U);
  ASSERT_NE(CTSDM.getDesign().findInst("session_inst_255"), nullptr);

  EXPECT_TRUE(icts::CTSAPI::destroyCTS().ok());
  EXPECT_TRUE(icts::CTSAPI::outputClockTiming().empty());

  icts::DataManager::initInst();
  EXPECT_EQ(CTSDM.getState(), icts::CTSRunState::kEmpty);
  EXPECT_TRUE(CTSDM.getDesign().get_insts().empty());
  EXPECT_EQ(CTSDM.getDesign().findInst("session_inst_255"), nullptr);
}

TEST(CTSAPILifecycleTest, PublicInitializedSessionCanDestroyAndReinitialize)
{
  const ScopedMinimalExternalIdb external_idb("public_reinit");
  ASSERT_TRUE(external_idb.ready());

  const auto first_init = icts::CTSAPI::init(external_idb.configPath().string(), external_idb.workDir().string());
  ASSERT_EQ(first_init.code, icts::CTSStatusCode::kOk) << first_init.message;
  EXPECT_TRUE(icts::CTSAPI::outputClockTiming().empty());
  EXPECT_EQ(icts::CTSAPI::runCTS().code, icts::CTSStatusCode::kNoOp);

  EXPECT_TRUE(icts::CTSAPI::destroyCTS().ok());
  EXPECT_EQ(icts::CTSAPI::runCTS().code, icts::CTSStatusCode::kNotInitialized);
  EXPECT_EQ(icts::CTSAPI::report(external_idb.workDir().string()).code, icts::CTSStatusCode::kNotInitialized);

  const auto second_init = icts::CTSAPI::init(external_idb.configPath().string(), external_idb.workDir().string());
  ASSERT_EQ(second_init.code, icts::CTSStatusCode::kOk) << second_init.message;
  EXPECT_EQ(icts::CTSAPI::runCTS().code, icts::CTSStatusCode::kNoOp);
  EXPECT_TRUE(icts::CTSAPI::destroyCTS().ok());
  EXPECT_TRUE(icts::CTSAPI::destroyCTS().ok());
}

TEST(CTSAPILifecycleTest, DestroyPreservesBorrowedExternalIdbObjectsAndConnectivity)
{
  icts::DataManager::initInst();
  idb::IdbDesign external_design;
  auto* driver_inst = external_design.get_instance_list()->add_instance("cts_driver");
  auto* load_inst = external_design.get_instance_list()->add_instance("cts_load");
  ASSERT_NE(driver_inst, nullptr);
  ASSERT_NE(load_inst, nullptr);
  driver_inst->set_coodinate(100, 200, false);
  load_inst->set_coodinate(300, 400, false);
  auto* driver_pin = driver_inst->addPin("Y");
  auto* load_pin = load_inst->addPin("A");
  ASSERT_NE(driver_pin, nullptr);
  ASSERT_NE(load_pin, nullptr);
  driver_pin->set_location(110, 210);
  load_pin->set_location(310, 410);

  auto* clock_net = external_design.get_net_list()->add_net("cts_clock_net", idb::IdbConnectType::kClock);
  ASSERT_NE(clock_net, nullptr);
  clock_net->add_instance_pin(driver_pin);
  clock_net->add_instance_pin(load_pin);
  driver_pin->set_net(clock_net);
  driver_pin->set_net_name(clock_net->get_net_name());
  load_pin->set_net(clock_net);
  load_pin->set_net_name(clock_net->get_net_name());

  CTSDM.getWrapper().set_idb_design(&external_design);
  const auto before = Fingerprint(external_design);

  EXPECT_TRUE(icts::CTSAPI::destroyCTS().ok());

  const auto after = Fingerprint(external_design);
  EXPECT_EQ(after, before);
  ASSERT_EQ(after.insts.size(), 2U);
  ASSERT_EQ(after.nets.size(), 1U);
  ASSERT_EQ(after.pins.size(), 2U);
}

TEST(CTSAPILifecycleTest, CompletionIsLoggedBeforeLoggerDestruction)
{
  icts::Logger::initInst();
  icts::DataManager::initInst();
  const auto output_dir = MakeUniqueOutputDir("log_order");
  std::filesystem::create_directories(output_dir);
  const auto log_path = output_dir / "cts.log";
  CTSLOG.openLogFileStream(log_path.string());
  PopulateRepresentativeSession(32U);

  ASSERT_TRUE(icts::CTSAPI::destroyCTS().ok());

  std::ifstream log_file(log_path);
  const std::string log_text((std::istreambuf_iterator<char>(log_file)), std::istreambuf_iterator<char>());
  const auto start_pos = log_text.find("Starting CTS destruction");
  const auto completion_pos = log_text.find("Completed CTS destruction");
  EXPECT_NE(start_pos, std::string::npos);
  EXPECT_NE(completion_pos, std::string::npos);
  EXPECT_LT(start_pos, completion_pos);

  RemoveOutputDir(output_dir);
}

TEST(CTSAPILifecycleTest, TenCyclesRemainWithinApprovedPostDestroyRssThreshold)
{
#ifdef __linux__
  const ScopedMinimalExternalIdb external_idb("ten_cycles");
  ASSERT_TRUE(external_idb.ready());
  std::array<double, 10> post_destroy_rss_mb{};
  for (std::size_t cycle = 0U; cycle < post_destroy_rss_mb.size(); ++cycle) {
    const auto init_status = icts::CTSAPI::init(external_idb.configPath().string(), external_idb.workDir().string());
    ASSERT_EQ(init_status.code, icts::CTSStatusCode::kOk) << "cycle " << cycle << ": " << init_status.message;
    EXPECT_TRUE(icts::CTSAPI::outputClockTiming().empty());
    EXPECT_EQ(icts::CTSAPI::runCTS().code, icts::CTSStatusCode::kNoOp);
    PopulateRepresentativeSession(4096U);
    ASSERT_TRUE(icts::CTSAPI::destroyCTS().ok());
    ASSERT_TRUE(icts::CTSAPI::destroyCTS().ok());
    const auto rss_mb = icts::Utility::currentRssMb();
    ASSERT_TRUE(rss_mb.has_value());
    post_destroy_rss_mb.at(cycle) = rss_mb.value_or(0.0);
  }

  const std::array<double, 4> early_samples = {post_destroy_rss_mb.at(1U), post_destroy_rss_mb.at(2U), post_destroy_rss_mb.at(3U), post_destroy_rss_mb.at(4U)};
  const std::array<double, 4> late_samples = {post_destroy_rss_mb.at(6U), post_destroy_rss_mb.at(7U), post_destroy_rss_mb.at(8U), post_destroy_rss_mb.at(9U)};
  const double early_median_mb = Median(early_samples);
  const double late_median_mb = Median(late_samples);
  constexpr double absolute_tolerance_mb = 16.0;
  const double allowed_growth_mb = std::max(absolute_tolerance_mb, early_median_mb * 0.05);
  EXPECT_LE(late_median_mb, early_median_mb + allowed_growth_mb);

  bool sustained_growth = true;
  for (std::size_t index = 2U; index < post_destroy_rss_mb.size(); ++index) {
    sustained_growth = sustained_growth && post_destroy_rss_mb.at(index) > post_destroy_rss_mb.at(index - 1U);
  }
  EXPECT_FALSE(sustained_growth);
#else
  GTEST_SKIP() << "Linux /proc RSS sampling is unavailable.";
#endif
}

}  // namespace
}  // namespace icts_test

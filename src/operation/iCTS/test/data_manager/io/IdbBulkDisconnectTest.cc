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
 * @file IdbBulkDisconnectTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-08-11
 * @brief Regression and scale tests for iDB-owned bulk regular-net disconnection.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "IdbDesign.h"
#include "IdbInstance.h"
#include "IdbNet.h"
#include "IdbPins.h"

namespace icts_test {
namespace {

using SteadyClock = std::chrono::steady_clock;

struct DisconnectObservation
{
  std::size_t disconnected_pin_count = 0U;
  std::size_t net_pin_count = 0U;
  std::size_t net_instance_count = 0U;
  bool all_pins_disconnected = false;
  bool all_net_names_empty = false;

  auto operator==(const DisconnectObservation&) const -> bool = default;
};

struct DisconnectSample
{
  double duration_us = 0.0;
  DisconnectObservation observation;
};

struct ScaleSamples
{
  std::size_t pin_count = 0U;
  std::vector<double> legacy_us;
  std::vector<double> bulk_us;
};

class HighFanoutIoFixture
{
 public:
  explicit HighFanoutIoFixture(std::size_t pin_count) : _owned_pins(pin_count)
  {
    _net = _design.createOrFindNet("high_fanout", idb::IdbConnectType::kClock);
    _pin_refs.reserve(pin_count);
    for (std::size_t index = 0U; index < pin_count; ++index) {
      auto* pin_ref = &_owned_pins[index];
      pin_ref->set_as_io();
      pin_ref->set_pin_name("P" + std::to_string(index));
      _pin_refs.push_back(pin_ref);
      if (!_design.connectPinToNet(pin_ref, _net)) {
        _setup_ok = false;
        break;
      }
    }
  }

  auto setupOk() const -> bool { return _setup_ok && _net != nullptr && static_cast<std::size_t>(_net->get_pin_number()) == _pin_refs.size(); }
  auto design() -> idb::IdbDesign& { return _design; }
  auto net() -> idb::IdbNet* { return _net; }
  auto pinRefs() -> const std::vector<idb::IdbPin*>& { return _pin_refs; }

 private:
  std::vector<idb::IdbPin> _owned_pins;
  idb::IdbDesign _design;
  idb::IdbNet* _net = nullptr;
  std::vector<idb::IdbPin*> _pin_refs;
  bool _setup_ok = true;
};

auto ObserveDisconnected(HighFanoutIoFixture& fixture, std::size_t disconnected_pin_count) -> DisconnectObservation
{
  auto* net = fixture.net();
  const auto& pins = fixture.pinRefs();

  DisconnectObservation observation;
  observation.disconnected_pin_count = disconnected_pin_count;
  observation.net_pin_count = net == nullptr ? 0U : static_cast<std::size_t>(net->get_pin_number());
  observation.net_instance_count = net == nullptr || net->get_instance_list() == nullptr ? 0U : net->get_instance_list()->get_instance_list().size();
  observation.all_pins_disconnected = std::ranges::all_of(pins, [](auto* pin) -> bool { return pin != nullptr && pin->get_net() == nullptr; });
  observation.all_net_names_empty = std::ranges::all_of(pins, [](auto* pin) -> bool { return pin != nullptr && pin->get_net_name().empty(); });
  return observation;
}

auto RunLegacyDisconnect(std::size_t pin_count) -> DisconnectSample
{
  HighFanoutIoFixture fixture(pin_count);
  EXPECT_TRUE(fixture.setupOk());

  std::size_t disconnected_pin_count = 0U;
  const auto start = SteadyClock::now();
  for (auto* pin : fixture.pinRefs()) {
    disconnected_pin_count += fixture.design().disconnectPinFromNet(pin) ? 1U : 0U;
  }
  const auto stop = SteadyClock::now();

  return DisconnectSample{.duration_us = std::chrono::duration<double, std::micro>(stop - start).count(),
                          .observation = ObserveDisconnected(fixture, disconnected_pin_count)};
}

auto RunBulkDisconnect(std::size_t pin_count) -> DisconnectSample
{
  HighFanoutIoFixture fixture(pin_count);
  EXPECT_TRUE(fixture.setupOk());

  const auto start = SteadyClock::now();
  const std::size_t disconnected_pin_count = fixture.design().disconnectAllPinsFromNet(fixture.net());
  const auto stop = SteadyClock::now();

  return DisconnectSample{.duration_us = std::chrono::duration<double, std::micro>(stop - start).count(),
                          .observation = ObserveDisconnected(fixture, disconnected_pin_count)};
}

void ExpectCompleteDisconnect(const DisconnectObservation& observation, std::size_t pin_count)
{
  EXPECT_EQ(observation.disconnected_pin_count, pin_count);
  EXPECT_EQ(observation.net_pin_count, 0U);
  EXPECT_EQ(observation.net_instance_count, 0U);
  EXPECT_TRUE(observation.all_pins_disconnected);
  EXPECT_TRUE(observation.all_net_names_empty);
}

void RecordMeasuredPair(ScaleSamples& samples, bool bulk_first)
{
  DisconnectSample legacy;
  DisconnectSample bulk;
  if (bulk_first) {
    bulk = RunBulkDisconnect(samples.pin_count);
    legacy = RunLegacyDisconnect(samples.pin_count);
  } else {
    legacy = RunLegacyDisconnect(samples.pin_count);
    bulk = RunBulkDisconnect(samples.pin_count);
  }

  ExpectCompleteDisconnect(legacy.observation, samples.pin_count);
  ExpectCompleteDisconnect(bulk.observation, samples.pin_count);
  EXPECT_EQ(legacy.observation, bulk.observation);
  samples.legacy_us.push_back(legacy.duration_us);
  samples.bulk_us.push_back(bulk.duration_us);
}

auto Median(std::vector<double> samples) -> double
{
  std::ranges::sort(samples);
  const std::size_t middle = samples.size() / 2U;
  if (samples.size() % 2U != 0U) {
    return samples[middle];
  }
  return (samples[middle - 1U] + samples[middle]) / 2.0;
}

void PrintSamples(const ScaleSamples& samples)
{
  const auto print_route = [&](const char* route, const std::vector<double>& durations) -> void {
    std::cout << "IDB_BULK_PERF pin_count=" << samples.pin_count << " route=" << route << " samples_us=[";
    for (std::size_t index = 0U; index < durations.size(); ++index) {
      if (index != 0U) {
        std::cout << ',';
      }
      std::cout << std::fixed << std::setprecision(3) << durations[index];
    }
    std::cout << "] median_us=" << Median(durations) << '\n';
  };

  print_route("legacy", samples.legacy_us);
  print_route("bulk", samples.bulk_us);
}

void WriteSamplesIfRequested(const std::vector<ScaleSamples>& scale_samples)
{
  const char* output_value = std::getenv("ICTS_IDB_PERF_OUTPUT");
  if (output_value == nullptr || *output_value == '\0') {
    return;
  }

  const std::filesystem::path output_path(output_value);
  std::error_code error;
  if (!output_path.parent_path().empty()) {
    std::filesystem::create_directories(output_path.parent_path(), error);
  }
  ASSERT_FALSE(error) << "Unable to create iDB performance evidence directory: " << error.message();

  std::ofstream output(output_path, std::ios::trunc);
  ASSERT_TRUE(output.is_open()) << "Unable to write iDB performance evidence: " << output_path;
  output << "pin_count,route,round,duration_us\n";
  output << std::fixed << std::setprecision(3);
  for (const auto& samples : scale_samples) {
    for (std::size_t index = 0U; index < samples.legacy_us.size(); ++index) {
      output << samples.pin_count << ",legacy," << (index + 1U) << ',' << samples.legacy_us[index] << '\n';
    }
    for (std::size_t index = 0U; index < samples.bulk_us.size(); ++index) {
      output << samples.pin_count << ",bulk," << (index + 1U) << ',' << samples.bulk_us[index] << '\n';
    }
  }
}

TEST(IdbPinsTest, ClearPinRefsPreservesBorrowedPinsAndResetsLazyIndex)
{
  idb::IdbPins refs;
  std::vector<std::unique_ptr<idb::IdbPin>> owned_pins;
  owned_pins.reserve(40U);
  for (std::size_t index = 0U; index < 40U; ++index) {
    auto pin = std::make_unique<idb::IdbPin>();
    pin->set_pin_name("P" + std::to_string(index));
    auto* pin_ref = pin.get();
    owned_pins.emplace_back(std::move(pin));
    EXPECT_EQ(refs.add_pin_ref_unique(pin_ref), pin_ref);
  }
  ASSERT_EQ(refs.get_pin_num(), 40U);

  refs.clear_pin_refs();

  EXPECT_EQ(refs.get_pin_num(), 0U);
  EXPECT_EQ(owned_pins.front()->get_pin_name(), "P0");
  EXPECT_EQ(owned_pins.back()->get_pin_name(), "P39");
  for (const auto& pin : owned_pins) {
    EXPECT_EQ(refs.add_pin_ref_unique(pin.get()), pin.get());
  }
  EXPECT_EQ(refs.get_pin_num(), 40U);
  EXPECT_EQ(refs.add_pin_ref_unique(owned_pins.back().get()), owned_pins.back().get());
  EXPECT_EQ(refs.get_pin_num(), 40U);
}

TEST(IdbInstanceListTest, ContainsUsesNamedMapWithoutLosingPointerFallback)
{
  idb::IdbDesign owning_design;
  auto* first = owning_design.get_instance_list()->add_instance("u_first");
  ASSERT_NE(first, nullptr);
  idb::IdbNet borrowed_instance_owner;
  auto* instances = borrowed_instance_owner.get_instance_list();
  ASSERT_NE(instances, nullptr);
  ASSERT_TRUE(instances->add_instance_ref(first));
  EXPECT_TRUE(instances->contains(first));
  EXPECT_TRUE(instances->add_instance_ref(first));
  EXPECT_EQ(instances->get_instance_list().size(), 1U);

  idb::IdbInstance duplicate_name;
  duplicate_name.set_name("u_first");
  EXPECT_FALSE(instances->contains(&duplicate_name));
  EXPECT_FALSE(instances->add_instance_ref(&duplicate_name));
  EXPECT_EQ(instances->get_instance_list().size(), 1U);

  first->set_name("u_renamed");
  EXPECT_TRUE(instances->contains(first));
  EXPECT_TRUE(instances->add_instance_ref(first));
  EXPECT_EQ(instances->get_instance_list().size(), 1U);
}

TEST(IdbDesignBulkDisconnectTest, PreservesSpecialNameBorrowedObjectsAndRepeatUse)
{
  idb::IdbDesign design;
  EXPECT_EQ(design.disconnectAllPinsFromNet(nullptr), 0U);

  auto* instance = design.get_instance_list()->add_instance("u_sink");
  ASSERT_NE(instance, nullptr);
  auto* first_inst_pin = instance->get_pin_list()->add_pin_list("A");
  auto* second_inst_pin = instance->get_pin_list()->add_pin_list("B");
  ASSERT_NE(first_inst_pin, nullptr);
  ASSERT_NE(second_inst_pin, nullptr);
  first_inst_pin->set_instance(instance);
  second_inst_pin->set_instance(instance);

  auto* io_pin = design.createOrFindIoPin("clk_in");
  auto* regular_net = design.createOrFindNet("clk", idb::IdbConnectType::kClock);
  auto* special_net = design.createOrFindSpecialNet("VDD", idb::IdbConnectType::kPower);
  ASSERT_NE(io_pin, nullptr);
  ASSERT_NE(regular_net, nullptr);
  ASSERT_NE(special_net, nullptr);
  ASSERT_TRUE(design.connectPinToNet(io_pin, regular_net));
  ASSERT_TRUE(design.connectPinToNet(first_inst_pin, regular_net));
  ASSERT_TRUE(design.connectPinToNet(second_inst_pin, regular_net));
  ASSERT_TRUE(design.connectPinToSpecialNet(first_inst_pin, special_net));

  EXPECT_EQ(design.disconnectAllPinsFromNet(regular_net), 3U);
  EXPECT_TRUE(regular_net->get_io_pins()->get_pin_list().empty());
  EXPECT_TRUE(regular_net->get_instance_pin_list()->get_pin_list().empty());
  EXPECT_TRUE(regular_net->get_instance_list()->get_instance_list().empty());
  EXPECT_EQ(io_pin->get_net(), nullptr);
  EXPECT_TRUE(io_pin->get_net_name().empty());
  EXPECT_EQ(first_inst_pin->get_net(), nullptr);
  EXPECT_EQ(first_inst_pin->get_special_net(), special_net);
  EXPECT_EQ(first_inst_pin->get_net_name(), "VDD");
  EXPECT_EQ(second_inst_pin->get_net(), nullptr);
  EXPECT_TRUE(second_inst_pin->get_net_name().empty());
  EXPECT_EQ(design.get_instance_list()->find_instance("u_sink"), instance);
  EXPECT_EQ(instance->get_pin("A"), first_inst_pin);

  ASSERT_TRUE(design.connectPinToNet(io_pin, regular_net));
  ASSERT_TRUE(design.connectPinToNet(first_inst_pin, regular_net));
  ASSERT_TRUE(design.connectPinToNet(second_inst_pin, regular_net));
  EXPECT_EQ(design.disconnectAllPinsFromNet(regular_net), 3U);
  EXPECT_EQ(design.disconnectAllPinsFromNet(regular_net), 0U);
  EXPECT_EQ(first_inst_pin->get_net_name(), "VDD");
  EXPECT_EQ(design.get_instance_list()->find_instance("u_sink"), instance);
}

TEST(IdbDesignBulkDisconnectTest, RemoveNetSafePreservesPinAndSpecialNetName)
{
  idb::IdbDesign design;
  auto* pin = design.createOrFindIoPin("clk_in");
  auto* regular_net = design.createOrFindNet("clk", idb::IdbConnectType::kClock);
  auto* special_net = design.createOrFindSpecialNet("VDD", idb::IdbConnectType::kPower);
  ASSERT_NE(pin, nullptr);
  ASSERT_NE(regular_net, nullptr);
  ASSERT_NE(special_net, nullptr);
  ASSERT_TRUE(design.connectPinToNet(pin, regular_net));
  ASSERT_TRUE(design.connectPinToSpecialNet(pin, special_net));

  EXPECT_TRUE(design.removeNetSafe("clk"));
  EXPECT_EQ(design.get_net_list()->find_net("clk"), nullptr);
  EXPECT_EQ(design.get_io_pin_list()->find_pin("clk_in"), pin);
  EXPECT_EQ(pin->get_net(), nullptr);
  EXPECT_EQ(pin->get_special_net(), special_net);
  EXPECT_EQ(pin->get_net_name(), "VDD");
}

TEST(IdbDesignBulkDisconnectTest, MergeNetIntoMovesPinsAfterOneSourceDetach)
{
  idb::IdbDesign design;
  auto* instance = design.get_instance_list()->add_instance("u_sink");
  ASSERT_NE(instance, nullptr);
  auto* inst_pin = instance->get_pin_list()->add_pin_list("A");
  ASSERT_NE(inst_pin, nullptr);
  inst_pin->set_instance(instance);

  auto* io_pin = design.createOrFindIoPin("clk_in");
  auto* target_net = design.createOrFindNet("target", idb::IdbConnectType::kClock);
  auto* source_net = design.createOrFindNet("source", idb::IdbConnectType::kClock);
  ASSERT_NE(io_pin, nullptr);
  ASSERT_NE(target_net, nullptr);
  ASSERT_NE(source_net, nullptr);
  ASSERT_TRUE(design.connectPinToNet(io_pin, source_net));
  ASSERT_TRUE(design.connectPinToNet(inst_pin, source_net));

  EXPECT_TRUE(design.mergeNetInto("target", "source", false));
  EXPECT_EQ(design.get_net_list()->find_net("source"), nullptr);
  EXPECT_EQ(io_pin->get_net(), target_net);
  EXPECT_EQ(io_pin->get_net_name(), "target");
  EXPECT_EQ(inst_pin->get_net(), target_net);
  EXPECT_EQ(inst_pin->get_net_name(), "target");
  EXPECT_TRUE(target_net->has_io_pin(io_pin));
  EXPECT_TRUE(target_net->has_instance_pin(inst_pin));
  EXPECT_TRUE(target_net->has_instance(instance));
}

TEST(IdbDesignBulkDisconnectPerformanceTest, LegacyAndBulkPathsMatchAtTenAndHundredThousandPins)
{
  constexpr std::size_t ten_thousand_pin_count = 10000U;
  constexpr std::size_t hundred_thousand_pin_count = 100000U;
  constexpr std::size_t ten_thousand_round_count = 5U;
  constexpr std::size_t hundred_thousand_round_count = 3U;

  const auto warmup_legacy = RunLegacyDisconnect(ten_thousand_pin_count);
  const auto warmup_bulk = RunBulkDisconnect(ten_thousand_pin_count);
  ExpectCompleteDisconnect(warmup_legacy.observation, ten_thousand_pin_count);
  ExpectCompleteDisconnect(warmup_bulk.observation, ten_thousand_pin_count);
  EXPECT_EQ(warmup_legacy.observation, warmup_bulk.observation);

  ScaleSamples ten_thousand{.pin_count = ten_thousand_pin_count, .legacy_us = {}, .bulk_us = {}};
  for (std::size_t round = 0U; round < ten_thousand_round_count; ++round) {
    RecordMeasuredPair(ten_thousand, round % 2U != 0U);
  }

  ScaleSamples hundred_thousand{.pin_count = hundred_thousand_pin_count, .legacy_us = {}, .bulk_us = {}};
  for (std::size_t round = 0U; round < hundred_thousand_round_count; ++round) {
    RecordMeasuredPair(hundred_thousand, round % 2U == 0U);
  }

  PrintSamples(ten_thousand);
  PrintSamples(hundred_thousand);
  WriteSamplesIfRequested({ten_thousand, hundred_thousand});

  const double ten_thousand_legacy_median = Median(ten_thousand.legacy_us);
  const double ten_thousand_bulk_median = Median(ten_thousand.bulk_us);
  const double hundred_thousand_legacy_median = Median(hundred_thousand.legacy_us);
  const double hundred_thousand_bulk_median = Median(hundred_thousand.bulk_us);
  std::cout << "IDB_BULK_PERF_SUMMARY pin_count=10000 legacy_median_us=" << ten_thousand_legacy_median << " bulk_median_us=" << ten_thousand_bulk_median
            << " speedup=" << (ten_thousand_legacy_median / ten_thousand_bulk_median) << '\n';
  std::cout << "IDB_BULK_PERF_SUMMARY pin_count=100000 legacy_median_us=" << hundred_thousand_legacy_median
            << " bulk_median_us=" << hundred_thousand_bulk_median << " speedup=" << (hundred_thousand_legacy_median / hundred_thousand_bulk_median)
            << " bulk_scaling=" << (hundred_thousand_bulk_median / ten_thousand_bulk_median) << '\n';

  EXPECT_LE(hundred_thousand_bulk_median, hundred_thousand_legacy_median * 0.20);
  EXPECT_LE(hundred_thousand_bulk_median, ten_thousand_bulk_median * 15.0);
}

}  // namespace
}  // namespace icts_test

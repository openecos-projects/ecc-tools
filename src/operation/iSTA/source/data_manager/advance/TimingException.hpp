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
#pragma once

#include "STAHeader.hpp"
#include "TransType.hpp"

namespace ista {

enum class TimingExceptionType
{
  kFalsePath,
  kMaxDelay,
  kMinDelay,
  kMulticycle
};

class TimingExceptionThrough
{
 public:
  TimingExceptionThrough() = default;
  ~TimingExceptionThrough() = default;
  // getter
  const std::set<std::string>& get_objects() const { return _objects; }
  TransType get_trans_type() const { return _trans_type; }
  // setter
  void set_objects(const std::set<std::string>& objects) { _objects = objects; }
  void set_trans_type(const TransType trans_type) { _trans_type = trans_type; }
  // function

 private:
  std::set<std::string> _objects;
  TransType _trans_type = TransType::kNone;
};

class TimingException
{
 public:
  TimingException() = default;
  ~TimingException() = default;
  // getter
  const std::set<std::string>& get_from_objects() const { return _from_objects; }
  const std::vector<TimingExceptionThrough>& get_through_list() const { return _through_list; }
  const std::set<std::string>& get_to_objects() const { return _to_objects; }
  TimingExceptionType get_type() const { return _type; }
  TransType get_from_trans_type() const { return _from_trans_type; }
  TransType get_to_trans_type() const { return _to_trans_type; }
  bool get_setup() const { return _setup; }
  bool get_hold() const { return _hold; }
  bool get_rise() const { return _rise; }
  bool get_fall() const { return _fall; }
  const std::string& get_comment() const { return _comment; }
  double get_delay() const { return _delay; }
  bool get_ignore_clock_latency() const { return _ignore_clock_latency; }
  bool get_probe() const { return _probe; }
  const std::optional<int32_t>& get_setup_multiplier() const { return _setup_multiplier; }
  const std::optional<int32_t>& get_hold_multiplier() const { return _hold_multiplier; }
  bool get_setup_use_end_clock() const { return _setup_use_end_clock; }
  bool get_hold_use_end_clock() const { return _hold_use_end_clock; }
  // setter
  void set_from_objects(const std::set<std::string>& objects) { _from_objects = objects; }
  void set_through_list(const std::vector<TimingExceptionThrough>& through_list) { _through_list = through_list; }
  void set_to_objects(const std::set<std::string>& objects) { _to_objects = objects; }
  void set_type(TimingExceptionType type) { _type = type; }
  void set_from_trans_type(const TransType trans_type) { _from_trans_type = trans_type; }
  void set_to_trans_type(const TransType trans_type) { _to_trans_type = trans_type; }
  void set_setup(bool value) { _setup = value; }
  void set_hold(bool value) { _hold = value; }
  void set_rise(bool value) { _rise = value; }
  void set_fall(bool value) { _fall = value; }
  void set_comment(const std::string& comment) { _comment = comment; }
  void set_delay(double delay) { _delay = delay; }
  void set_ignore_clock_latency(bool value) { _ignore_clock_latency = value; }
  void set_probe(bool value) { _probe = value; }
  void set_setup_multiplier(std::optional<int32_t> multiplier) { _setup_multiplier = multiplier; }
  void set_hold_multiplier(std::optional<int32_t> multiplier) { _hold_multiplier = multiplier; }
  void set_setup_use_end_clock(bool value) { _setup_use_end_clock = value; }
  void set_hold_use_end_clock(bool value) { _hold_use_end_clock = value; }
  // function

 private:
  std::set<std::string> _from_objects;
  std::vector<TimingExceptionThrough> _through_list;
  std::set<std::string> _to_objects;
  TimingExceptionType _type = TimingExceptionType::kFalsePath;
  TransType _from_trans_type = TransType::kNone;
  TransType _to_trans_type = TransType::kNone;
  bool _setup = true;
  bool _hold = true;
  bool _rise = true;
  bool _fall = true;
  std::string _comment;
  double _delay = 0.0;
  bool _ignore_clock_latency = false;
  bool _probe = false;
  std::optional<int32_t> _setup_multiplier;
  std::optional<int32_t> _hold_multiplier;
  bool _setup_use_end_clock = true;
  bool _hold_use_end_clock = false;
};

struct ResolvedTimingExceptions
{
  bool false_path = false;
  const TimingException* path_delay = nullptr;
  const TimingException* setup_multicycle = nullptr;
  const TimingException* hold_multicycle = nullptr;
};

enum class TimingClockGroupType
{
  kAsynchronous,
  kLogicallyExclusive,
  kPhysicallyExclusive,
  kExclusive
};

class TimingClockGroup
{
 public:
  TimingClockGroup() = default;
  ~TimingClockGroup() = default;
  // getter
  const std::vector<std::set<std::string>>& get_groups() const { return _groups; }
  bool get_allow_paths() const { return _allow_paths; }
  TimingClockGroupType get_type() const { return _type; }
  const std::string& get_name() const { return _name; }
  const std::string& get_comment() const { return _comment; }
  // setter
  void set_groups(const std::vector<std::set<std::string>>& groups) { _groups = groups; }
  void set_allow_paths(bool value) { _allow_paths = value; }
  void set_type(TimingClockGroupType value) { _type = value; }
  void set_name(std::string value) { _name = std::move(value); }
  void set_comment(std::string value) { _comment = std::move(value); }
  // function

 private:
  std::vector<std::set<std::string>> _groups;
  bool _allow_paths = false;
  TimingClockGroupType _type = TimingClockGroupType::kAsynchronous;
  std::string _name;
  std::string _comment;
};

}  // namespace ista

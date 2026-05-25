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

#include <string>
#include <utility>

#include "Environment.hh"
#include "ProcessVariation.hh"
#include "Types.hh"

namespace ircx {

#define RCX_FLOW_INST (ircx::Flow::getInst())

class Flow
{
 public:
  // Meyer's singleton
  static auto getInst() -> Flow&
  {
    static Flow inst;  // C++11 thread-safe
    return inst;
  }

  // Disallow copy/move
  Flow(const Flow& other) = delete;
  Flow(Flow&& other) = delete;
  auto operator=(const Flow& other) -> Flow& = delete;
  auto operator=(Flow&& other) -> Flow& = delete;

  auto runRCX() -> void;
  auto readData() -> bool;
  auto run() -> void;
  auto report(const Str& output_dir) -> void;

  auto checkShortOpen() -> bool;
  auto buildTopology() -> bool;
  auto buildEnvironment() -> bool;
  auto buildProcessVariation() -> bool;
  auto extractParasitics() -> bool;

  auto setSetupReady(bool setup_ready) -> void { setup_ready_ = setup_ready; }
  [[nodiscard]] auto setup_ready() const -> bool { return setup_ready_; }
  [[nodiscard]] auto run_success() const -> bool { return run_success_; }
  [[nodiscard]] auto report_success() const -> bool { return report_success_; }

  // setters & getters
  void set_num_threads(unsigned value) { num_threads_ = value == 0 ? 1U : value; }
  void set_operating_temperature(F64 value) { operating_temperature_ = value; }
  void set_output_dir(Str value) { output_dir_ = value.empty() ? "." : std::move(value); }
  [[nodiscard]] auto num_threads() const -> unsigned { return num_threads_; }
  [[nodiscard]] auto operating_temperature() const -> F64 { return operating_temperature_; }
  [[nodiscard]] auto output_dir() const -> const Str& { return output_dir_; }

  void reset();

 private:
  Flow();
  ~Flow();

  auto dumpSpef(const Str& output_dir) -> bool;

 private:
  // running settings
  unsigned num_threads_{};
  F64 operating_temperature_{};
  Str output_dir_{"."};

  Environment environment_;
  ProcessVariation process_variation_;

  bool setup_ready_{false};
  bool run_success_{false};
  bool report_success_{false};
};

}  // namespace ircx

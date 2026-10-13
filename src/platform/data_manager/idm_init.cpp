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
 * @File Name: dm_init.cpp
 * @Brief :
 * @Author : Yell (12112088@qq.com)
 * @Version : 1.0
 * @Creat Date : 2022-04-15
 *
 */

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "idm.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <thread>
#include <utility>

#ifdef __GLIBC__
#include <malloc.h>
#endif

#include "liberty/Lib.hh"

namespace idm {
namespace {

class LibertySilentOutputGuard
{
 public:
  LibertySilentOutputGuard() : _old_silent_output(idb::Lib::isSilentOutput()) { idb::Lib::setSilentOutput(true); }
  ~LibertySilentOutputGuard() { idb::Lib::setSilentOutput(_old_silent_output); }

  LibertySilentOutputGuard(const LibertySilentOutputGuard& other) = delete;
  LibertySilentOutputGuard& operator=(const LibertySilentOutputGuard& rhs) = delete;

 private:
  bool _old_silent_output = false;
};

}  // namespace

RawLibertyGeneration::RawLibertyGeneration(vector<std::unique_ptr<idb::LibLibrary>> libraries, vector<string> paths,
                                           int32_t configured_workers, size_t active_workers, double elapsed_seconds)
    : _libraries(std::move(libraries)),
      _paths(std::move(paths)),
      _configured_workers(configured_workers),
      _active_workers(active_workers),
      _elapsed_seconds(elapsed_seconds)
{
}

RawLibertyGeneration::~RawLibertyGeneration() = default;

bool DataManager::initLef(vector<string> lef_path, bool b_techlef)
{
  _idb_lef_service = _idb_builder->buildLef(lef_path, b_techlef);
  _layout = get_idb_layout();

  return _idb_lef_service == nullptr ? false : true;
}

bool DataManager::initDef(string def_path)
{
  _idb_def_service = _idb_builder->buildDef(def_path);
  if (_idb_def_service == nullptr) {
    _design = nullptr;
    return false;
  }

  _design = get_idb_design();

  /// make original coordinate on (0,0)
  if (isNeedTransformByDie()) {
    /// transform
    transformByDie();
  }

  return true;
}

bool DataManager::initVerilog(string verilog_path, string top_module)
{
  _idb_def_service = _idb_builder->buildVerilog(verilog_path, top_module);
  _design = get_idb_design();

  return _idb_def_service == nullptr ? false : true;
}

bool DataManager::initLib(vector<string> lib_paths)
{
  std::lock_guard<std::mutex> load_lock(_liberty_load_mutex);
  const auto start_time = std::chrono::steady_clock::now();
  const int32_t configured_workers = std::max(_config.get_thread_number(), 1);
  const size_t active_workers = lib_paths.empty() ? 0U : std::min(static_cast<size_t>(configured_workers), lib_paths.size());

  vector<std::unique_ptr<idb::LibLibrary>> libraries(lib_paths.size());
  vector<string> errors(lib_paths.size());
  std::atomic_size_t next_index = 0U;
  std::atomic_size_t parse_count = 0U;
  std::atomic_size_t link_count = 0U;

  string worker_launch_error;
  {
    LibertySilentOutputGuard silent_output_guard;
    try {
      vector<std::jthread> workers;
      workers.reserve(active_workers);
      for (size_t worker_index = 0U; worker_index < active_workers; ++worker_index) {
        workers.emplace_back([&]() {
          while (true) {
            const size_t lib_index = next_index.fetch_add(1U, std::memory_order_relaxed);
            if (lib_index >= lib_paths.size()) {
              return;
            }

            try {
              idb::LibertyReader reader(lib_paths[lib_index].c_str());
              parse_count.fetch_add(1U, std::memory_order_relaxed);
              if (reader.readLib() == 0U) {
                errors[lib_index] = "parse failed";
                continue;
              }
              link_count.fetch_add(1U, std::memory_order_relaxed);
              if (reader.linkLib() == 0U) {
                errors[lib_index] = "link failed";
                continue;
              }
              libraries[lib_index] = reader.takeLib();
              if (libraries[lib_index] == nullptr) {
                errors[lib_index] = "linked library is null";
              }
            } catch (const std::exception& exception) {
              errors[lib_index] = exception.what();
            } catch (...) {
              errors[lib_index] = "unknown parser failure";
            }
          }
        });
      }
    } catch (const std::exception& exception) {
      worker_launch_error = exception.what();
    } catch (...) {
      worker_launch_error = "unknown worker launch failure";
    }
  }

#ifdef __GLIBC__
  if (active_workers > 0U) {
    (void) ::malloc_trim(0);
  }
#endif

  bool success = worker_launch_error.empty();
  if (!worker_launch_error.empty()) {
    ECCLOG.warn(ecc::Loc::current(), "Liberty worker launch failed: ", worker_launch_error);
  }
  for (size_t lib_index = 0U; lib_index < errors.size(); ++lib_index) {
    if (!errors[lib_index].empty()) {
      ECCLOG.warn(ecc::Loc::current(), "Liberty load failed for ", lib_paths[lib_index], ": ", errors[lib_index]);
      success = false;
    }
  }
  if (!success) {
    ECCLOG.warn(ecc::Loc::current(), "Liberty generation was not published: libraries=", lib_paths.size(),
                ", parse_count=", parse_count.load(std::memory_order_relaxed), ", link_count=", link_count.load(std::memory_order_relaxed),
                "; the previous generation remains active.");
    return false;
  }

  const double elapsed_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
  auto generation
      = std::make_shared<RawLibertyGeneration>(std::move(libraries), lib_paths, configured_workers, active_workers, elapsed_seconds);
  {
    std::lock_guard<std::mutex> generation_lock(_liberty_generation_mutex);
    _liberty_generation = std::move(generation);
  }
  ECCLOG.info(ecc::Loc::current(), "Liberty generation published: libraries=", lib_paths.size(),
              ", configured_workers=", configured_workers, ", active_workers=", active_workers,
              ", parse_count=", parse_count.load(std::memory_order_relaxed), ", link_count=", link_count.load(std::memory_order_relaxed),
              ", parse_link_seconds=", elapsed_seconds);
  return true;
}

bool DataManager::initSpef(string spef_path)
{
  _spef_reader = std::make_unique<spef::SpefReader>();
  if (!_spef_reader->read(spef_path)) {
    return false;
  }
  _spef_reader->expandName();

  return true;
}

bool DataManager::initVcd(string vcd_path)
{
  _vcd_reader = std::make_unique<vcd::VcdReader>();
  if (!_vcd_reader->read(vcd_path)) {
    return false;
  }

  return true;
}

}  // namespace idm

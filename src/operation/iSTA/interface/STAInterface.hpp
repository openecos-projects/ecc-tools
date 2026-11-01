// SPDX-License-Identifier: MulanPSL-2.0
#pragma once
#include <any>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "TimingData.hpp"

namespace idb {
class LibLibrary;
}  // namespace idb

namespace spef {
class SpefReader;
}  // namespace spef

namespace vcd {
class VcdReader;
}  // namespace vcd

namespace ista {
#define STAI (ista::STAInterface::getInst())

// External design sources borrow timing inputs from the surrounding command
// layer. The caller owns every pointed-to reader and library.
struct NativeTimingInputs
{
  std::vector<idb::LibLibrary*> liberty_libraries;
  spef::SpefReader* spef_reader = nullptr;
  vcd::VcdReader* vcd_reader = nullptr;
  std::string sdc_path;
  std::string spef_path;
};

// Cross-tool entry to the shared STA session. Calls must be serialized with
// design/database mutation; this interface does not provide independent sessions.
class STAInterface
{
 public:
  static STAInterface& getInst();
  static void destroyInst();

  // Uses the design already loaded in iDB unless an external source is selected.
  // runSTA() makes timing export ready.
  void initSTA(std::map<std::string, std::any> config_map);
  // Build structural timing state without numeric clock/data propagation.
  void prepareTimingModel();
  void runSTA();
  void extractLib();
  // Export the effective constraints after initSTA(); timing need not be run.
  void writeSdc(const std::string& file_path);
  void destroySTA();
  // Release a failed or partly initialized session without emitting output.
  void abortSTA() noexcept;

  // Passing an empty callback restores the normal iDB input path. The current
  // inputs are supplied at invocation so callers may update them between runs.
  void setDatabaseWrapper(std::function<void(const NativeTimingInputs&)> database_wrapper);
  void setTimingInputs(NativeTimingInputs inputs);
  // Populate iSTA's owned domains from the selected source. This is public for
  // focused source-adapter tests; normal callers should use initSTA().
  void wrapDatabase();

  bool isInitialized() const { return _initialized; }
  bool isModelReady() const { return _model_ready; }
  bool isTimingReady() const;

  // Requires a successful runSTA(); throws std::runtime_error if not ready.
  // The returned value owns its data and remains usable after destroySTA().
  TimingData exportTimingData();
  // Export the structural model after prepareTimingModel() without running
  // numeric clock/data/required propagation.
  TimingData exportTimingModel();

 private:
  static STAInterface* _sta_interface_instance;

  void readDesign(std::map<std::string, std::any>& config_map);

  std::function<void(const NativeTimingInputs&)> _database_wrapper;
  NativeTimingInputs _native_timing_inputs;

  bool _initialized = false;
  bool _model_ready = false;
  bool _timing_ready = false;
  std::uint64_t _analyzed_sdc_revision = 0;

  STAInterface() = default;
  STAInterface(const STAInterface& other) = delete;
  STAInterface(STAInterface&& other) = delete;
  ~STAInterface() = default;
  STAInterface& operator=(const STAInterface& other) = delete;
  STAInterface& operator=(STAInterface&& other) = delete;
};

}  // namespace ista

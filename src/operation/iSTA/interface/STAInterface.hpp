// SPDX-License-Identifier: MulanPSL-2.0
#pragma once
#include <any>
#include <map>
#include <string>

#include "TimingData.hpp"

namespace ista {
#define STAI (ista::STAInterface::getInst())

// Cross-tool entry to the shared STA session. Calls must be serialized with
// design/database mutation; this interface does not provide independent sessions.
class STAInterface
{
 public:
  static STAInterface& getInst();
  static void destroyInst();

  // Uses the design already loaded in iDB. runSTA() makes timing export ready.
  void initSTA(std::map<std::string, std::any> config_map);
  // Build structural timing state without numeric clock/data propagation.
  void prepareTimingModel();
  void runSTA();
  void extractLib();
  // Export the effective constraints after initSTA(); timing need not be run.
  void writeSdc(const std::string& file_path);
  void destroySTA();
  bool isInitialized() const { return _initialized; }
  bool isModelReady() const { return _model_ready; }
  bool isTimingReady() const { return _timing_ready; }

  // Requires a successful runSTA(); throws std::runtime_error if not ready.
  // The returned value owns its data and remains usable after destroySTA().
  TimingData exportTimingData();
  // Export the structural model after prepareTimingModel() without running
  // numeric clock/data/required propagation.
  TimingData exportTimingModel();

 private:
  static STAInterface* _sta_interface_instance;

  void readDesign(std::map<std::string, std::any>& config_map);

  bool _initialized = false;
  bool _model_ready = false;
  bool _timing_ready = false;

  STAInterface() = default;
  STAInterface(const STAInterface& other) = delete;
  STAInterface(STAInterface&& other) = delete;
  ~STAInterface() = default;
  STAInterface& operator=(const STAInterface& other) = delete;
  STAInterface& operator=(STAInterface&& other) = delete;
};

}  // namespace ista

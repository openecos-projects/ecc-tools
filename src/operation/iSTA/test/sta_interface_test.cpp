// SPDX-License-Identifier: MulanPSL-2.0
#include <unistd.h>

#include <any>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

#include "DataManager.hpp"
#include "IdbLayout.h"
#include "STAInterface.hpp"
#include "SdcCommand.hpp"
#include "idm.h"
#include "tcl_register_ista.h"

using namespace ista;

static void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}

struct DesignFixture
{
  idb::IdbLayout layout;
  idb::IdbDefService service{&layout};
  idb::IdbDefService* saved_service = dmInst->get_idb_def_service();
  std::string saved_sdc = dmInst->get_config().get_sdc_path();
  std::filesystem::path directory = std::filesystem::temp_directory_path() / ("ista-interface-" + std::to_string(getpid()));

  std::filesystem::path sdc_file = directory.string() + ".sdc";

  DesignFixture()
  {
    auto* design = service.get_design();
    design->set_design_name("interface_contract");
    auto* net = design->get_net_list()->add_net("wire", idb::IdbConnectType::kSignal);
    for (const auto* name : {"in", "out", "other"}) {
      auto* pin = design->get_io_pin_list()->add_pin_list(name);
      pin->set_as_io();
      auto* term = pin->set_term();
      term->set_direction(std::string(name) == "in" ? idb::IdbConnectDirection::kInput : idb::IdbConnectDirection::kOutput);
      term->set_type(idb::IdbConnectType::kSignal);
      pin->set_net(net);
      net->add_io_pin(pin);
    }
    dmInst->set_idb_def_service(&service);
    std::ofstream(sdc_file) << "set_input_transition 0.1 [get_ports in]\n";
    dmInst->get_config().get_sdc_path() = sdc_file.string();
  }
  ~DesignFixture()
  {
    dmInst->set_idb_def_service(saved_service);
    dmInst->get_config().get_sdc_path() = saved_sdc;
    std::filesystem::remove_all(directory);
    std::filesystem::remove(sdc_file);
  }
};

int main()
{
  setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
  DesignFixture fixture;
  auto& sta = STAInterface::getInst();
  sta.initSTA({{"-temp_directory_path", fixture.directory.string()},
               {"-thread_number", int32_t{1}},
               {"-output_timing_reports", int32_t{0}},
               {"-output_timing_features", int32_t{0}}});
  require(STADM.getDatabase().sdc().get_sdc_file_path() == fixture.sdc_file.string()
              && STADM.getDatabase().sdc().get_port_constraint_map().at("in").get_input_transition(AnalysisType::kMax, TransType::kRise) == 0.1,
          "session did not read the configured SDC path");
  require(SdcCommand::getInst().evalString("create_clock -name clock -period 10; set_input_delay -clock clock 1 [get_ports in]; "
                                           "set_output_delay -clock clock 2 [get_ports {out other}]; "
                                           "set_load -min 0.1 [get_ports out]; set_load -min 0.2 [get_ports other]")
              == TCL_OK,
          "constraint setup failed");
  require(!sta.isModelReady() && !sta.isTimingReady(), "initialized session published a stale timing state");
  sta.prepareTimingModel();
  require(sta.isModelReady() && !sta.isTimingReady(), "structural model preparation published the wrong ready state");
  const auto structural_model = sta.exportTimingModel();
  require(structural_model.phase_calls == std::map<std::string, int64_t>{
              {"structural_preparation", 1}, {"clock_network", 1}, {"clock_propagation", 0},
              {"data_slew", 0}, {"data_arrival", 0}, {"required", 0}, {"path_analysis", 0}, {"delay_graph_initialization", 0}},
          "model preparation executed a full numeric timing phase");
  require(structural_model.sparse.enabled && !structural_model.pins.empty(),
          "model-only export did not publish the structural sparse model");
  require(structural_model.min_launches.size() == 2 && structural_model.min_contexts.size() == 4,
          "model-only export lost the min launch/endpoint rules");
  for (const auto& seed : structural_model.min_launches)
    require(seed.start_point == "in" && seed.arrival_ps == structural_model.time_unit_ps,
            "model-only min seed lost the SDC input delay");
  for (const auto& pin : structural_model.pins) {
    const double expected = pin.name == "out" ? 0.1 : pin.name == "other" ? 0.2 : 0.0;
    require(std::abs(pin.min_output_rise_load_pf - expected * structural_model.capacitance_unit_pf) < 1E-12
                && std::abs(pin.min_output_fall_load_pf - expected * structural_model.capacitance_unit_pf) < 1E-12,
            "min endpoint external load includes another pin's load on the same net");
  }
  require(!sta.isTimingReady(), "model-only export published numeric timing as ready");
  sta.runSTA();
  require(sta.isTimingReady(), "successful analysis is not ready");
  const auto complete = sta.exportTimingData();
  require(complete.phase_calls == std::map<std::string, int64_t>{
              {"structural_preparation", 1}, {"clock_network", 2}, {"clock_propagation", 1},
              {"data_slew", 1}, {"data_arrival", 1}, {"required", 1}, {"path_analysis", 1}, {"delay_graph_initialization", 1}},
          "complete STA did not record its actual numeric phases");
  require(!complete.pins.empty(), "successful export is empty");
  require(complete.min_launches.size() == structural_model.min_launches.size()
              && complete.min_contexts.size() == structural_model.min_contexts.size(),
          "model-only and complete preparation produced different min domains");
  for (std::size_t index = 0; index < complete.min_contexts.size(); ++index) {
    const auto& expected = complete.min_contexts[index];
    const auto& actual = structural_model.min_contexts[index];
    require(actual.end_point == expected.end_point && actual.path_tag == expected.path_tag
                && actual.required_ps - actual.check_time_ps == expected.required_ps - expected.check_time_ps,
            "model-only and complete preparation produced different min required rules");
  }

  tcl::registerCmdSTA();
  auto* engine = ecc::ScriptEngine::getOrCreateInstance();
  const auto path = fixture.directory / "effective.sdc";
  Tcl_SetVar(engine->get_interp(), "destination", path.c_str(), TCL_GLOBAL_ONLY);
  require(engine->evalString("write_sdc -nosplit $destination") == TCL_OK, "public write_sdc failed on initialized session");
  require(std::filesystem::file_size(path) > 0, "public write_sdc produced no data");
  require(engine->evalString("write_sdc") == TCL_ERROR, "public write_sdc retained previous filename");
  require(engine->evalString("write_sdc -version 1.9 $destination") == TCL_ERROR, "public write_sdc accepted unsupported version");
  require(engine->evalString("info commands output_sdc") == TCL_OK && std::string(engine->getResult()).empty(), "obsolete output_sdc still registered");

  for (const auto operation : {&STAInterface::runSTA, &STAInterface::extractLib}) {
    auto& pin = STADM.getDatabase().network().pins().at("out");
    pin.set_direction(PinDirection::kInput);  // Both ports now drive the same net.
    bool failed = false;
    try {
      (sta.*operation)();
    } catch (const std::runtime_error& error) {
      failed = std::string(error.what()).find("multiple driver") != std::string::npos;
    }
    require(failed, "reanalysis did not encounter the intended graph failure");
    require(!sta.isTimingReady(), "failed reanalysis retained stale ready status");
    bool rejected = false;
    try {
      sta.exportTimingData();
    } catch (const std::runtime_error&) {
      rejected = true;
    }
    require(rejected, "partial analysis was exported as ready timing");
    pin.set_direction(PinDirection::kOutput);
    sta.runSTA();
    require(sta.isTimingReady() && !sta.exportTimingData().pins.empty(), "repaired analysis did not recover");
  }
  // A partially read SDC must invalidate both the old model and its timing.
  std::ofstream(fixture.sdc_file) << "set_input_transition 0.2 [get_ports in]\nunknown_sdc_command\n";
  bool init_failed = false;
  try {
    sta.initSTA({{"-temp_directory_path", fixture.directory.string()}, {"-thread_number", int32_t{1}}});
  } catch (const std::runtime_error&) {
    init_failed = true;
  }
  require(init_failed, "invalid SDC did not fail initialization");
  require(!sta.isInitialized() && !sta.isTimingReady(), "failed initialization retained an initialized or ready session");
  for (int operation : {0, 1, 2}) {
    bool rejected = false;
    try {
      if (operation == 0)
        sta.exportTimingData();
      if (operation == 1)
        sta.runSTA();
      if (operation == 2)
        sta.writeSdc((fixture.directory / "invalid.sdc").string());
    } catch (const std::runtime_error&) {
      rejected = true;
    }
    require(rejected, "failed initialization published a partial model");
  }
  std::ofstream(fixture.sdc_file) << "set_input_transition 0.1 [get_ports in]\n";
  sta.initSTA({{"-temp_directory_path", fixture.directory.string()},
               {"-thread_number", int32_t{1}},
               {"-output_timing_reports", int32_t{0}},
               {"-output_timing_features", int32_t{0}}});
  sta.runSTA();
  require(sta.isTimingReady() && !sta.exportTimingData().pins.empty(), "valid reinitialization did not recover");
  sta.extractLib();
  require(sta.isTimingReady(), "successful model extraction lost completed timing");
  sta.destroySTA();
  require(!sta.isTimingReady(), "destroy retained ready timing");
  STAInterface::destroyInst();
  ecc::ScriptEngine::destroyInstance();
}

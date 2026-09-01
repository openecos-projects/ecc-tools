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

#include "idm.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

#include "liberty/CppLibertyDriver.hh"
#include "liberty/Lib.hh"

namespace {

class TempDirectory
{
 public:
  TempDirectory()
      : _path(std::filesystem::temp_directory_path() / ("ecc_liberty_generation_test_" + std::to_string(static_cast<long long>(getpid()))))
  {
    std::filesystem::remove_all(_path);
    std::filesystem::create_directories(_path);
  }

  ~TempDirectory() { std::filesystem::remove_all(_path); }

  TempDirectory(const TempDirectory& other) = delete;
  TempDirectory& operator=(const TempDirectory& rhs) = delete;

  const std::filesystem::path& path() const { return _path; }

 private:
  std::filesystem::path _path;
};

bool expect(bool condition, const std::string& message)
{
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
  }
  return condition;
}

std::string makeLiberty(const std::string& library_name, const std::string& cell_name)
{
  return "library (" + library_name + R"lib() {
  time_unit : "1ns";
  capacitive_load_unit (1, pf);
  voltage_unit : "1V";
  current_unit : "1mA";
  pulling_resistance_unit : "1kohm";
  leakage_power_unit : "1nW";
  nom_voltage : 1.0;
  cell ()lib"
         + cell_name + R"lib() {
    area : 1.0;
    pin (A) {
      direction : input;
      capacitance : 0.01;
    }
    pin (Y) {
      direction : output;
      function : "A";
      max_capacitance : 1.0;
    }
  }
}
)lib";
}

bool writeText(const std::filesystem::path& path, const std::string& text)
{
  std::ofstream stream(path);
  stream << text;
  return stream.good();
}

std::string makeDataConfig(const std::string& thread_number_entry)
{
  return R"json({
  "INPUT": {
    "tech_lef_path": "",
    "lef_paths": [],
    "def_path": "",
    "verilog_path": "",
    "lib_path": [],
    "sdc_path": "",
    "spef_path": "",
    "vcd_path": "")json"
         + thread_number_entry + R"json(
  },
  "OUTPUT": {"output_dir_path": ""},
  "LayerSettings": {"routing_layer_1st": ""}
}
)json";
}

}  // namespace

int main()
{
  bool success = true;
  idm::DataConfig config;
  success &= expect(config.get_thread_number() == 4, "DataConfig must default to four Liberty workers");
  config.set_thread_number(6);
  success &= expect(config.get_thread_number() == 6, "positive worker override must be retained");
  config.set_thread_number(0);
  success &= expect(config.get_thread_number() == 1, "zero workers must normalize to one");
  config.set_thread_number(-8);
  success &= expect(config.get_thread_number() == 1, "negative workers must normalize to one");

  TempDirectory temp_directory;
  const auto default_config_path = temp_directory.path() / "default_config.json";
  const auto override_config_path = temp_directory.path() / "override_config.json";
  success &= expect(writeText(default_config_path, makeDataConfig("")), "default JSON config must be writable");
  success &= expect(writeText(override_config_path, makeDataConfig(",\n    \"thread_number\": 6")), "override JSON config must be writable");
  idm::DataConfig default_json_config;
  success &= expect(default_json_config.initConfig(default_config_path.string()), "JSON config without thread_number must load");
  success &= expect(default_json_config.get_thread_number() == 4, "missing JSON thread_number must preserve the default four");
  idm::DataConfig override_json_config;
  success &= expect(override_json_config.initConfig(override_config_path.string()), "JSON config with thread_number must load");
  success &= expect(override_json_config.get_thread_number() == 6, "JSON thread_number override must be retained");

  std::vector<std::string> lib_paths;
  for (size_t index = 0U; index < 6U; ++index) {
    const auto lib_path = temp_directory.path() / ("lib_" + std::to_string(index) + ".lib");
    success &= expect(writeText(lib_path, makeLiberty("lib_" + std::to_string(index), "CELL_" + std::to_string(index))),
                      "test Liberty file must be writable");
    lib_paths.push_back(lib_path.string());
  }

  auto* data_manager = dmInst;
  data_manager->reset();
  success &= expect(data_manager->readLib({}), "an empty explicit load must publish an empty generation");
  auto generation = data_manager->get_liberty_generation();
  success &= expect(generation != nullptr && generation->get_libraries().empty(), "empty load must have no libraries");
  success &= expect(generation != nullptr && generation->get_active_workers() == 0U, "empty load must start no workers");

  data_manager->get_config().set_thread_number(8);
  success &= expect(data_manager->readLib({lib_paths[0]}), "one-library load must succeed");
  generation = data_manager->get_liberty_generation();
  success &= expect(generation != nullptr && generation->get_active_workers() == 1U, "one library must bound active workers to one");

  data_manager->get_config().set_thread_number(4);
  const std::vector<std::string> ordered_paths{lib_paths[4], lib_paths[1], lib_paths[5], lib_paths[0], lib_paths[3]};
  success &= expect(data_manager->readLib(ordered_paths), "five-library parallel load must succeed");
  auto held_generation = data_manager->get_liberty_generation();
  success &= expect(held_generation != nullptr && held_generation->get_active_workers() == 4U,
                    "five libraries with config four must start four workers");
  success &= expect(held_generation != nullptr && held_generation->get_paths() == ordered_paths,
                    "published paths must retain input order");
  if (held_generation != nullptr && held_generation->get_libraries().size() == ordered_paths.size()) {
    const std::vector<std::string> expected_names{"lib_4", "lib_1", "lib_5", "lib_0", "lib_3"};
    for (size_t index = 0U; index < expected_names.size(); ++index) {
      success &= expect(held_generation->get_libraries()[index] != nullptr
                            && held_generation->get_libraries()[index]->get_lib_name() == expected_names[index],
                        "library ownership must retain input order at index " + std::to_string(index));
    }
  } else {
    success = false;
    std::cerr << "FAILED: five-library generation has the wrong size\n";
  }

  std::weak_ptr<const idm::RawLibertyGeneration> old_generation = held_generation;
  data_manager->get_config().set_thread_number(6);
  success &= expect(data_manager->readLib({lib_paths[2], lib_paths[0]}), "two-library reload must succeed");
  auto current_generation = data_manager->get_liberty_generation();
  success &= expect(current_generation != nullptr && current_generation->get_active_workers() == 2U,
                    "two libraries must bound active workers to two");
  success &= expect(current_generation != held_generation, "successful reload must replace the active generation");
  success &= expect(!old_generation.expired(), "a held consumer snapshot must keep the old generation alive across reload");
  held_generation.reset();
  success &= expect(old_generation.expired(), "the replaced generation must release after its last snapshot");

  auto before_failure = current_generation;
  const auto malformed_partial_path = temp_directory.path() / "malformed_partial.lib";
  success &= expect(writeText(malformed_partial_path, R"lib(library (partial) {
  cell (BROKEN) {
    pin (A) {
      direction : ;
    }
  }
}
)lib"),
                    "malformed partial-AST Liberty file must be writable");
  {
    liberty::LibertyDriver reusable_driver;
    success &= expect(!reusable_driver.parse(malformed_partial_path.c_str()), "a malformed partial AST must fail parsing");
    success &= expect(reusable_driver.getParseResult() == nullptr, "failed parsing must retain no published AST root");
    success &= expect(reusable_driver.parse(lib_paths[0].c_str()), "a driver must remain reusable after partial-AST cleanup");
  }
  {
    idb::LibertyReader malformed_reader(malformed_partial_path.c_str());
    success &= expect(malformed_reader.readLib() == 0U, "LibertyReader must reject and release a malformed partial AST");
  }
  success &= expect(!data_manager->readLib({malformed_partial_path.string()}), "a worker-side partial-AST parse failure must report failure");
  success &= expect(data_manager->get_liberty_generation() == before_failure,
                    "a worker-side partial-AST failure must preserve the active generation");

  const auto missing_path = (temp_directory.path() / "missing.lib").string();
  success &= expect(!data_manager->readLib({missing_path}), "a parse failure must report failure");
  success &= expect(data_manager->get_liberty_generation() == before_failure, "failed reload must not replace the active generation");
  const auto unsupported_root_path = temp_directory.path() / "unsupported_root.lib";
  success &= expect(writeText(unsupported_root_path, "unsupported (root) {}\n"), "unsupported-root Liberty file must be writable");
  success &= expect(!data_manager->readLib({unsupported_root_path.string()}), "a linked input without a library must report failure");
  success &= expect(data_manager->get_liberty_generation() == before_failure,
                    "linked-library failure must not replace the active generation");

  std::weak_ptr<const idm::RawLibertyGeneration> reset_generation = current_generation;
  data_manager->resetData();
  success &= expect(data_manager->get_liberty_generation() == nullptr, "reset must clear the active generation");
  success &= expect(!reset_generation.expired(), "held snapshots must survive DataManager reset");
  before_failure.reset();
  current_generation.reset();
  generation.reset();
  success &= expect(reset_generation.expired(), "reset generation must release after all snapshots are dropped");

  {
    idb::LibertyReader unlinked_reader(lib_paths[0].c_str());
    success &= expect(unlinked_reader.readLib() != 0U, "an unlinked reader must parse for destructor coverage");
  }
  {
    idb::LibertyReader source_reader(lib_paths[1].c_str());
    success &= expect(source_reader.readLib() != 0U, "move source must parse");
    idb::LibertyReader moved_reader(std::move(source_reader));
    success &= expect(moved_reader.linkLib() != 0U && moved_reader.takeLib() != nullptr,
                      "move construction must transfer AST and builder ownership");
  }
  {
    idb::LibertyReader destination_reader(lib_paths[2].c_str());
    idb::LibertyReader source_reader(lib_paths[3].c_str());
    success &= expect(destination_reader.readLib() != 0U && source_reader.readLib() != 0U, "move assignment inputs must parse");
    destination_reader = std::move(source_reader);
    success &= expect(destination_reader.linkLib() != 0U && destination_reader.takeLib() != nullptr,
                      "move assignment must release prior AST and transfer source ownership");
  }

  data_manager->reset();
  return success ? 0 : 1;
}

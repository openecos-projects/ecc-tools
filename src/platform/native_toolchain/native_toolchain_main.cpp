// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// Licensed under Mulan PSL v2. See the project LICENSE for details.
#include "NativeToolchain.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "json.hpp"

namespace {
using Json = nlohmann::json;
void saveJson(const std::filesystem::path& file, const Json& value)
{
  std::ofstream output(file);
  if (!output) throw std::runtime_error("Cannot write diagnostic: " + file.string());
  output << value.dump(2) << '\n';
}
}  // namespace

int main(int argc, char** argv)
{
  if (argc != 2) {
    std::cerr << "Usage: ecc_native_flow manifest.json\n";
    return 2;
  }
  // Existing tool loggers otherwise exit directly and prevent phase diagnostics.
  setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
  Json report{{"backend", "eccdb"}, {"status", "running"}, {"stages", Json::array()}};
  std::filesystem::path output_directory;
  std::string active_stage = "import";
  try {
    std::ifstream input(argv[1]);
    if (!input) throw std::runtime_error("Cannot open flow manifest");
    const auto manifest = Json::parse(input);
    if (manifest.value("backend", "eccdb") != "eccdb") {
      throw std::invalid_argument("ecc_native_flow supports only backend=eccdb");
    }
    const auto base = std::filesystem::absolute(argv[1]).parent_path();
    const auto resolve = [&](const std::string& value) {
      std::filesystem::path path(value);
      return (path.is_absolute() ? path : base / path).lexically_normal().string();
    };
    output_directory = resolve(manifest.at("output_directory").get<std::string>());
    std::filesystem::create_directories(output_directory);
    saveJson(output_directory / "manifest.json", manifest);
    std::unique_ptr<ecc::NativeToolchain> owner;
    if (manifest.contains("binary_directory")) {
      owner = std::make_unique<ecc::NativeToolchain>(resolve(manifest["binary_directory"].get<std::string>()));
    } else {
      std::vector<std::string> lefs;
      for (const auto& path : manifest.at("lef_files")) lefs.push_back(resolve(path.get<std::string>()));
      owner = std::make_unique<ecc::NativeToolchain>(lefs, resolve(manifest.at("def_file").get<std::string>()));
    }
    auto& flow = *owner;
    report["input"] = Json::parse(flow.summary());
    if (manifest.contains("liberty_files")) {
      std::vector<std::string> libraries;
      for (const auto& path : manifest["liberty_files"]) libraries.push_back(resolve(path.get<std::string>()));
      flow.loadLiberty(libraries);
    }
    if (manifest.contains("sdc_file")) flow.loadSdc(resolve(manifest["sdc_file"].get<std::string>()));
    if (manifest.contains("reference_verilog")) {
      flow.loadReferenceVerilog(resolve(manifest["reference_verilog"].get<std::string>()), manifest.value("top", ""));
    }
    std::size_t ordinal = 0;
    for (const auto& stage : manifest.at("stages")) {
      active_stage = stage.at("name").get<std::string>();
      const auto stage_directory = output_directory / (std::to_string(++ordinal) + "-" + active_stage);
      std::filesystem::create_directories(stage_directory);
      if (stage.contains("spef_file")) flow.loadSpef(resolve(stage["spef_file"].get<std::string>()));
      const auto config = stage.contains("config") ? resolve(stage["config"].get<std::string>()) : "";
      auto options = stage.value("options", Json::object());
      if (!options.contains("-temp_directory_path") && !options.contains("temp_directory_path")) {
        options["-temp_directory_path"] = stage_directory.string();
      }
      if (!options.contains("-work_dir") && !options.contains("work_dir")) {
        options["-work_dir"] = stage_directory.string();
      }
      const auto result = Json::parse(flow.run(active_stage, config, options.dump()));
      saveJson(stage_directory / "result.json", result);
      flow.writeDef((stage_directory / "output.def").string());
      if (manifest.value("binary_checkpoints", false)) flow.writeBinary(stage_directory.string());
      report["stages"].push_back(result);
      saveJson(output_directory / "result.json", report);
      std::cout << "NATIVE_STAGE " << result.dump() << std::endl;
    }
    report["output"] = Json::parse(flow.summary());
    report["status"] = "passed";
    saveJson(output_directory / "result.json", report);
    return 0;
  } catch (const std::exception& error) {
    report["status"] = "failed";
    report["failed_stage"] = active_stage;
    report["error"] = error.what();
    if (!output_directory.empty()) saveJson(output_directory / "result.json", report);
    std::cerr << report.dump(2) << '\n';
    return 1;
  }
}

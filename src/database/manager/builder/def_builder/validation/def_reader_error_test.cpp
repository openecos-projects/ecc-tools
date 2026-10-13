// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2. This file is a regression test.
// ***************************************************************************************

#include "def_read.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <zlib.h>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

const std::string kMissingMasterDef = "VERSION 5.8 ;\n"
                                      "DESIGN def_reader_error_test ;\n"
                                      "UNITS DISTANCE MICRONS 1000 ;\n"
                                      "COMPONENTS 1 ;\n"
                                      "- missing_instance MISSING_MASTER ;\n"
                                      "END COMPONENTS\n"
                                      "END DESIGN\n";

const std::string kZeroRouteHaloDef = "VERSION 5.8 ;\n"
                                      "DESIGN def_reader_route_halo_test ;\n"
                                      "UNITS DISTANCE MICRONS 1000 ;\n"
                                      "COMPONENTS 1 ;\n"
                                      "- zero_halo TEST_CELL + ROUTEHALO 0 M1 M2 + PLACED ( 0 0 ) N ;\n"
                                      "END COMPONENTS\n"
                                      "END DESIGN\n";

void requireMissingMasterFailure(const std::filesystem::path& def_path)
{
  idb::IdbLayout layout;
  idb::IdbDefService service(&layout);
  idb::DefRead reader(&service);
  const bool parsed = reader.createDb(def_path.c_str());

  require(!parsed, "DEF reader must fail when a component master is missing");
  const auto* error = reader.get_last_error();
  require(error != nullptr, "DEF reader failure must provide structured error data");
  require(error->file_path == def_path.string(), "error report must identify the input file");
  require(error->line_number > 0, "error report must identify the source line");
  require(error->stage == "component", "error report must identify the failing callback");
  require(error->status == kDbFail, "error report must preserve the callback failure status");
  require(!error->message.empty(), "error report must include a failure message");
}

void testComponentFailureIsReported()
{
  const auto def_path = std::filesystem::temp_directory_path() / "def_reader_error_test.def";
  {
    std::ofstream def_file(def_path);
    def_file << kMissingMasterDef;
  }

  requireMissingMasterFailure(def_path);
  std::filesystem::remove(def_path);
}

void testGzipComponentFailureIsReported()
{
  const auto def_path = std::filesystem::temp_directory_path() / "def_reader_error_test.def.gz";
  gzFile def_file = gzopen(def_path.c_str(), "wb");
  require(def_file != nullptr, "failed to create gzip DEF input");
  require(gzwrite(def_file, kMissingMasterDef.data(), kMissingMasterDef.size()) == static_cast<int>(kMissingMasterDef.size()),
          "failed to write gzip DEF input");
  require(gzclose(def_file) == Z_OK, "failed to close gzip DEF input");

  requireMissingMasterFailure(def_path);
  std::filesystem::remove(def_path);
}

void testZeroRouteHaloIsImported()
{
  const auto def_path = std::filesystem::temp_directory_path() / "def_reader_zero_route_halo_test.def";
  {
    std::ofstream def_file(def_path);
    def_file << kZeroRouteHaloDef;
  }

  idb::IdbLayout layout;
  auto* layers = layout.get_layers();
  require(layers->set_layer("M1", "ROUTING") != nullptr, "failed to create M1 test layer");
  require(layers->set_layer("M2", "ROUTING") != nullptr, "failed to create M2 test layer");
  require(layout.get_cell_master_list()->set_cell_master("TEST_CELL") != nullptr, "failed to create test cell master");

  idb::IdbDefService service(&layout);
  idb::DefRead reader(&service);
  require(reader.createDb(def_path.c_str()), "failed to parse DEF with ROUTEHALO 0");
  std::filesystem::remove(def_path);

  auto* instance = service.get_design()->get_instance_list()->find_instance("zero_halo");
  require(instance != nullptr, "ROUTEHALO component was not imported");
  auto* route_halo = instance->get_route_halo();
  require(route_halo != nullptr, "zero-distance ROUTEHALO must remain present");
  require(route_halo->get_route_distance() == 0, "zero-distance ROUTEHALO distance was not preserved");
  require(route_halo->get_layer_bottom() != nullptr && route_halo->get_layer_bottom()->get_name() == "M1",
          "ROUTEHALO minimum layer was not preserved");
  require(route_halo->get_layer_top() != nullptr && route_halo->get_layer_top()->get_name() == "M2",
          "ROUTEHALO maximum layer was not preserved");
}

}  // namespace

int main()
{
  try {
    testComponentFailureIsReported();
    testGzipComponentFailureIsReported();
    testZeroRouteHaloIsImported();
  } catch (const std::exception& error) {
    std::cout << error.what() << '\n';
    return 1;
  }
  return 0;
}

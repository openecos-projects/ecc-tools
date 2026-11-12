// SPDX-License-Identifier: MulanPSL-2.0
#include <filesystem>
#include <stdexcept>
#include <string>

#include "Database.hpp"
#include "Logger.hpp"
#include "ReadLiberty.hpp"
#include "idm.h"

using namespace ista;

static void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

static void read(Database& database)
{
  ReadLiberty(database.liberty(), database.network()).read();
}

static void requireMissing(Database& database)
{
  std::string diagnostic;
  try {
    read(database);
  } catch (const std::runtime_error& error) {
    diagnostic = error.what();
  }
  require(diagnostic.find("memory") != std::string::npos && diagnostic.find("sram0") != std::string::npos,
          "missing signal-cell model was silently accepted instead of identifying the cell and instance");
}

int main(int argc, char** argv)
{
  require(argc == 2, "expected fixture directory");
  Logger::initInst();
  const auto fixtures = std::filesystem::path(argv[1]);
  const auto standard = (fixtures / "clock_check_reader.lib").string();
  const auto memory = (fixtures / "retaining_arc.lib").string();
  Database database;
  // Network import removes PG pins. Physical-only filler/tap cells therefore
  // do not require a timing model, even when their master is not in Liberty.
  auto& filler = database.network().instances()["fill0"];
  filler.set_instance_name("fill0");
  filler.set_cell_name("physical_filler");
  require(dmInst->readLib({standard}), "failed to load standard-cell generation");
  read(database);

  auto& sram = database.network().instances()["sram0"];
  sram.set_instance_name("sram0");
  sram.set_cell_name("memory");
  for (const auto* name : {"CLK", "Q"}) {
    const std::string full_name = std::string("sram0:") + name;
    auto& pin = database.network().pins()[full_name];
    pin.set_full_name(full_name);
    pin.set_instance_name("sram0");
    pin.set_pin_name(name);
    pin.set_direction(std::string(name) == "CLK" ? PinDirection::kInput : PinDirection::kOutput);
  }
  // Instance pin lists have not been linked at the reader stage. The check
  // must use imported pins and must also catch unconnected signal terminals.
  require(sram.get_pin_name_list().empty(), "fixture prematurely linked instance pins");
  requireMissing(database);

  require(dmInst->readLib({standard, memory}), "failed to load complete generation");
  read(database);
  require(database.liberty().get_cell_map().contains("memory"), "complete generation lost SRAM model");
  require(!database.liberty().get_cell_map().at("memory").get_cell_arc_list().empty(), "SRAM timing arcs were discarded");

  // A later corner reload must not silently reuse the old corner's SRAM or
  // replace its clock-to-Q arcs by input-to-output combinational connectivity.
  require(dmInst->readLib({standard}), "failed to reload standard-cell generation");
  requireMissing(database);
  Logger::destroyInst();
}

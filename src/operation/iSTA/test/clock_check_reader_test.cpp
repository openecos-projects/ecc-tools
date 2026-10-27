// SPDX-License-Identifier: MulanPSL-2.0
#include <unistd.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "Database.hpp"
#include "Lib.hh"
#include "LibParserCpp.hh"
#include "ReadLiberty.hpp"
#include "idm.h"
using namespace ista;
int main(int argc, char** argv)
{
  idb::LibertyReader reader(argv[1]);
  if (!reader.readLib() || !reader.linkLib())
    throw std::runtime_error("clock check library parse failed");
  auto library = reader.takeLib();
  Database db;
  auto verify = [&](idb::LibPort* port) {
    if (!port)
      throw std::runtime_error("clock pin not imported");
    LibertyCell cell;
    ReadLiberty(db.liberty(), db.network()).readPort(cell, port);
    if (cell.get_sdf_check_arc_list().size() != 2)
      throw std::runtime_error("scalar clock constraints lost by Liberty reader");
    for (auto& arc : cell.get_sdf_check_arc_list()) {
      for (auto& [edge, table] : arc.get_timing_arc_list().front().get_check_table_map()) {
        const double expected = arc.get_check_type() == TimingCheckType::kPeriod ? 1.2 : edge == ista::TransType::kRise ? .6 : .7;
        if (std::abs(table.findValue(0., 0.) - expected) > 1e-12)
          throw std::runtime_error("scalar clock units or edge lost");
      }
    }
  };
  auto* cell = library->findCell("clock_cell");
  if (!dmInst->readLib({argv[1]}))
    throw std::runtime_error("Liberty generation load failed");
  ReadLiberty(db.liberty(), db.network()).read();
  auto& converted = db.liberty().get_cell_map().at("clock_cell");
  if (std::abs(db.liberty().get_nom_voltage() - 3.3) > 1e-12 || std::abs(converted.get_nom_voltage() - 3.3) > 1e-12 || converted.get_cell_arc_list().empty()
      || std::abs(converted.get_cell_arc_list().front().get_timing_arc_list().front().get_nom_voltage() - 3.3) > 1e-12)
    throw std::runtime_error("driving arc lost its owning library voltage or voltage unit");
  for (const char* name : {"CK", "BUS[0]", "BUS[1]"})
    verify(cell->get_cell_port_or_port_bus(name));
  const auto output = std::filesystem::temp_directory_path() / ("ista-clock-constraints-" + std::to_string(getpid()) + ".lib");
  library->printLibertyLibrary(output.c_str());
  idb::LibertyReader reread(output.c_str());
  if (!reread.readLib() || !reread.linkLib())
    throw std::runtime_error("clock constraint library roundtrip failed");
  auto restored = reread.takeLib();
  for (const char* name : {"CK", "BUS[0]", "BUS[1]"})
    verify(restored->findCell("clock_cell")->get_cell_port_or_port_bus(name));
  // Unsupported unit multipliers must never silently become ns/pF.
  for (const auto* units : {"time_unit : \"10ps\"; capacitive_load_unit(1,pf);", "time_unit : \"1ns\"; capacitive_load_unit(0.5,pf);"}) {
    {
      std::ofstream file(output);
      file << "library (bad_units) { " << units << " }";
    }
    idb::LibertyReader invalid(output.c_str());
    bool rejected = false;
    try {
      rejected = !invalid.readLib() || !invalid.linkLib();
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    if (!rejected)
      throw std::runtime_error("unsupported library unit was silently accepted");
  }
  std::filesystem::remove(output);
  idb::LibPort moved(std::move(*cell->get_cell_port_or_port_bus("CK")));
  verify(&moved);
  idb::LibPort assigned("tmp");
  assigned = std::move(moved);
  verify(&assigned);
}

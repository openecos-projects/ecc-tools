// SPDX-License-Identifier: MulanPSL-2.0
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "Lib.hh"
#include "LibParserCpp.hh"
#include "LibertyWriter.hpp"
#include "Logger.hpp"
#include "liberty/LibertyLibrary.hpp"
#include "liberty/TimingModel.hpp"

void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}

int main()
{
  ista::Logger::initInst();
  ista::LibertyLibrary source_library;
  ista::TimingModel model;
  ista::CharacterizedLibrary cell;
  cell.set_design_name("writer_cell");
  cell.set_analysis_type(ista::AnalysisType::kMax);
  cell.set_area(4.25);
  for (const auto* name : {"A", "Y"}) {
    auto& port = cell.get_port_map()[name];
    port.set_port_name(name);
    port.set_direction(std::string(name) == "A" ? ista::PinDirection::kInput : ista::PinDirection::kOutput);
    port.set_capacitance(0.03);
    port.set_is_clock(false);
  }
  ista::CharacterizedTimingArc arc;
  arc.set_source_port("A");
  arc.set_sink_port("Y");
  arc.set_timing_type("combinational");
  arc.set_timing_sense("positive_unate");
  for (const auto* name : {"cell_rise", "cell_fall", "rise_transition", "fall_transition"}) {
    ista::ScalarTable table;
    table.set_table_name(name);
    table.set_value(0.125);
    arc.get_scalar_table_list().push_back(table);
  }
  cell.get_timing_arc_list().push_back(arc);
  model.get_lib_list().push_back(cell);
  char pattern[] = "/tmp/ista-writer-XXXXXX";
  auto* directory = mkdtemp(pattern);
  require(directory != nullptr, "cannot create output directory");
  std::string output = std::string(directory) + "/";
  try {
    auto built = ista::liberty_writer::buildLib(model.get_lib_list().front(), source_library);
    require(built->get_time_unit() == idb::TimeUnit::kNS && built->get_cap_unit() == idb::CapacitiveUnit::kPF
                && built->get_resistance_unit() == idb::ResistanceUnit::kkOHM,
            "fallback units changed");
    require(built->findCell("writer_cell")->get_cell_port_or_port_bus("A")->get_port_cap() == 0.03, "written capacitance changed");
    ista::liberty_writer::writeLib(model, source_library, output);
    const auto file = output + "writer_cell_max.lib";
    require(std::filesystem::is_regular_file(file), "writer filename changed");
    idb::LibertyReader reader(file.c_str());
    require(reader.readLib() && reader.linkLib(), "written library does not parse/link");
    auto library = reader.takeLib();
    auto* parsed = library->findCell("writer_cell");
    require(parsed != nullptr && parsed->get_cell_area() == 4.25, "written cell metadata changed");
    auto* port = parsed->get_cell_port_or_port_bus("A");
    require(port != nullptr && parsed->get_cell_arcs().size() == 1, "written port/arc lost");
  } catch (...) {
    std::filesystem::remove_all(output);
    throw;
  }
  std::filesystem::remove_all(output);
  ista::Logger::destroyInst();
}

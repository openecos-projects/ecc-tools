// SPDX-License-Identifier: MulanPSL-2.0
#include <unistd.h>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include "Database.hpp"
#include "DelayCalculator.hpp"
#include "Lib.hh"
#include "LibParserCpp.hh"
#include "ReadLiberty.hpp"
#include "SdfValueExtractor.hpp"
#include "SdfWriter.hpp"
#include "idm.h"
using namespace ista;
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void verify(const char* path)
{
  Database db;
  Config config;
  require(dmInst->readLib({path}), "retaining library generation failed");
  ReadLiberty(db.liberty(), db.network()).read();
  auto& cell = db.liberty().get_cell_map().at("memory");
  require(cell.get_cell_arc_list().size() == 2, "retaining import changed cell connections");
  auto& instance = db.network().instances()["u"];
  instance.set_instance_name("u"); instance.set_cell_name("memory");
  for (const char* name : {"CLK", "EN", "Q", "R"}) {
    auto& pin = db.network().pins()[std::string("u:") + name];
    pin.set_full_name(std::string("u:") + name); pin.set_instance_name("u"); pin.set_pin_name(name);
  }
  DelayCalculator calculator(db, config);
  std::size_t cell_index = 0;
  for (auto& cell_arc : cell.get_cell_arc_list()) {
    require(cell_arc.get_timing_arc_list().size() == 2, "Liberty retaining tables were discarded");
    const bool partial = cell_arc.get_sink_port() == "R";
    const auto trigger = partial ? ista::TransType::kFall : ista::TransType::kRise;
    std::string pin = std::string("u:") + cell_arc.get_sink_port();
    require(!calculator.cellDelay(pin, cell_arc, AnalysisType::kMin, partial ? ista::TransType::kRise : ista::TransType::kFall, ista::TransType::kRise, .2),
            "retaining arc ignored inactive clock edge");
    require(!cell_arc.get_timing_arc_list()[0].isRetaining() && cell_arc.get_timing_arc_list()[1].isRetaining()
                && cell_arc.get_timing_arc_list()[0].get_arc_idx() != cell_arc.get_timing_arc_list()[1].get_arc_idx(),
            "retaining candidate lost its distinct identity");
    auto& arc = db.graph().arcs().emplace_back();
    arc.set_type(ArcType::kCell); arc.set_owner_name("u"); arc.set_source_pin("u:CLK"); arc.set_sink_pin(pin);
    arc.setLibertyArcId(LibertyArcId{"memory", cell_index++});
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) for (auto edge : {ista::TransType::kRise, ista::TransType::kFall}) {
      auto result = calculator.cellDelay(pin, cell_arc, mode, trigger, edge, .2);
      auto graph_result = calculator.arcDelay(arc, mode, trigger, edge, .2);
      const double delay = edge == ista::TransType::kRise ? (mode == AnalysisType::kMin ? .5 : .8) : (mode == AnalysisType::kMin || partial ? 1.2 : 2.);
      require(result && graph_result && std::abs(graph_result->get_delay() - delay) < 1e-12 && std::abs(result->get_delay() - delay) < 1e-12, "ordinary/retain candidate selection or unit scaling wrong");
    }
    const std::string condition = partial ? "" : "EN";
    auto values = SdfValueExtractor(db).graphCellArcDelays(arc);
    require(values.size() == 2 && std::abs(values.at({condition, trigger}).get_rise_min_delay().value_or(-1.) - .8) < 1e-12
                && std::abs(values.at({condition, trigger, true}).get_rise_min_delay().value_or(-1.) - .5) < 1e-12,
            "normal and retain SDF values were merged");
    if (!partial) {
      db.graph().simValues().setValue("u:EN", TimingCaseValue::kZero);
      require(!calculator.cellDelay(pin, cell_arc, AnalysisType::kMin, trigger, ista::TransType::kRise, .2),
              "false retain condition was ignored");
      db.graph().simValues().clear();
      require(calculator.cellDelay(pin, cell_arc, AnalysisType::kMin, trigger, ista::TransType::kRise, .2).has_value(),
              "unknown condition was incorrectly disabled");
    }
  }
  std::ostringstream both;
  SdfWriter(db, config).write(both);
  require(both.str().find("Q (RETAIN (0.5000000000::0.5000000000) (2.0000000000::2.0000000000)) (0.8000000000::0.8000000000) (1.2000000000::1.2000000000)") != std::string::npos,
          "SDF lost ordinary/retain identity or original delay values");
  require(both.str().find("R (RETAIN (0.5000000000::0.5000000000) ())") != std::string::npos,
          "missing retain fall was serialized as a shared rise/fall value");
  for (auto& arc : db.graph().arcs()) {
    if (arc.get_sink_pin() != "u:Q") continue;
    auto& family = *db.liberty().findCellArc(*arc.libertyArcId());
    for (auto& variant : family.get_timing_arc_list())
      if (!variant.isRetaining()) arc.get_timing_arc_delay_map().erase(variant.get_arc_idx());
    arc.get_graph_delay_map()[AnalysisType::kMin][ista::TransType::kRise][ista::TransType::kRise] = .5;
    auto values = SdfValueExtractor(db).graphCellArcDelays(arc);
    require(values.size() == 1 && values.begin()->first.retaining, "uncomputed normal reused a retain aggregate");
  }
  std::ostringstream only;
  SdfWriter(db, config).write(only);
  require(only.str().find("Q (RETAIN (0.5000000000::0.5000000000) (2.0000000000::2.0000000000)) () ()") != std::string::npos,
          "retain-only SDF record was lost or invented a normal delay");
}
int main(int argc, char** argv)
{
  require(argc == 2, "missing fixture");
  idb::LibertyReader reader(argv[1]);
  require(reader.readLib() && reader.linkLib(), "retaining library parse failed");
  auto library = reader.takeLib();
  verify(argv[1]);
  const auto output = std::filesystem::temp_directory_path() / ("ista-retaining-" + std::to_string(getpid()) + ".lib");
  library->printLibertyLibrary(output.c_str());
  idb::LibertyReader reread(output.c_str());
  require(reread.readLib() && reread.linkLib(), "retaining roundtrip parse failed");
  auto restored = reread.takeLib(); verify(output.c_str());
  std::filesystem::remove(output);
}

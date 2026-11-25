// SPDX-License-Identifier: MulanPSL-2.0
//
// Phase 0 of the string-interning refactor: lock the ordering invariants that
// the refactor must preserve before any name-keyed container becomes a dense
// id-indexed table.
//
// Today every name-keyed core container (Network::_pins/_instances/_nets,
// Graph::_points/_driver_points, Sdc object sets) is a std::map/std::set, so
// iteration is name-lexicographic regardless of insertion order. The planned
// SymbolTable-backed ids must reproduce that order explicitly through
// sorted_ids() rather than insertion order, or:
//   - vertex numbering changes, which can reorder equal-slack path reports
//     while every timing number stays correct (the hardest regression class
//     to notice);
//   - PyPlaceDB's pin/clock/liberty id spaces, which are assigned by
//     iterating these same containers, shift under it;
//   - Tcl get_pins/get_ports/get_cells/get_nets/get_clocks results reorder;
//   - SDF/SDC/.lib emission order changes line-for-line.
//
// Each helper below inserts names in a scrambled order on purpose, so a
// future refactor that preserves insertion order instead of name order fails
// these checks regardless of how the id space is populated internally.
#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "DataManager.hpp"
#include "GraphBuilder.hpp"
#include "Logger.hpp"
#include "SdcCommand.hpp"
#include "SdfWriter.hpp"

using namespace ista;

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void addPort(const std::string& name, PinDirection direction)
{
  Database& database = STADM.getDatabase();
  Pin& pin = database.network().pins()[name];
  pin.set_full_name(name);
  pin.set_pin_name(name);
  pin.set_direction(direction);
  pin.set_is_port(true);
}

void addInstanceWithCellArc(const std::string& instance_name)
{
  Database& database = STADM.getDatabase();
  Instance& instance = database.network().instances()[instance_name];
  instance.set_instance_name(instance_name);
  const std::string in_pin = instance_name + ":A";
  const std::string out_pin = instance_name + ":Z";
  for (const std::string& pin_name : {in_pin, out_pin}) {
    Pin& pin = database.network().pins()[pin_name];
    pin.set_full_name(pin_name);
    pin.set_instance_name(instance_name);
    pin.set_pin_name(pin_name.substr(pin_name.find(':') + 1));
  }
  database.network().pins()[in_pin].set_direction(PinDirection::kInput);
  database.network().pins()[out_pin].set_direction(PinDirection::kOutput);
  instance.get_pin_name_list() = {in_pin, out_pin};

  // ArcType::kCell with a non-empty input_output_delay_map is enough for
  // SdfWriter::hasSdfCellDelay() to emit a CELL block for this instance
  // without any Liberty data.
  Arc& arc = database.graph().arcs().emplace_back();
  arc.set_type(ArcType::kCell);
  arc.set_owner_name(instance_name);
  arc.set_source_pin(in_pin);
  arc.set_sink_pin(out_pin);
  arc.get_input_output_delay_map().set(AnalysisType::kMax, TransType::kRise, TransType::kRise, 0.1);
}

// I3: GraphConnectivityBuilder::buildGraphVertexList iterates
// network().pins() and assigns vertex indices in that (name-sorted) order.
void checkVertexOrderIsNameSorted()
{
  DataManager::initInst();
  Database& database = STADM.getDatabase();
  database.network().setName("vertex_order_invariant");
  for (const char* name : {"zebra", "mango", "apple", "kiwi"}) {
    addPort(name, PinDirection::kInput);
  }
  GraphBuilder(database).build();
  const std::vector<std::string> expected = {"apple", "kiwi", "mango", "zebra"};
  for (std::size_t i = 0; i + 1 < expected.size(); ++i) {
    const int32_t lower = database.network().pins().at(expected[i]).get_load_vertex_idx();
    const int32_t higher = database.network().pins().at(expected[i + 1]).get_load_vertex_idx();
    require(lower < higher, "vertex index order diverged from pin-name sort order: " + expected[i] + " vs " + expected[i + 1]);
  }
  DataManager::destroyInst();
}

// I2: sdc::query::ObjectQuery returns matches from a name-keyed std::set, so
// every get_pins/get_ports/get_cells/get_nets/get_clocks result handed to
// Tcl is name-sorted.
void checkTclQueryOrderIsNameSorted()
{
  DataManager::initInst();
  Database& database = STADM.getDatabase();
  database.network().setName("tcl_order_invariant");
  for (const char* name : {"zebra", "mango", "apple", "kiwi"}) {
    addPort(name, PinDirection::kInput);
  }
  SdcCommand::initInst();
  SdcCommand& interpreter = SdcCommand::getInst();
  sdc::registerSdcCommands(interpreter);
  require(interpreter.evalString("get_ports *") == TCL_OK, "get_ports * failed");
  const std::string result = Tcl_GetStringResult(interpreter.getInterp());
  int count = 0;
  const char** elements = nullptr;
  require(Tcl_SplitList(interpreter.getInterp(), result.c_str(), &count, &elements) == TCL_OK, "Tcl_SplitList failed on get_ports result");
  std::vector<std::string> names(elements, elements + count);
  Tcl_Free(reinterpret_cast<char*>(elements));
  require(names.size() == 4, "get_ports * returned the wrong element count");
  require(std::is_sorted(names.begin(), names.end()), "get_ports * result order is not name-sorted");
  SdcCommand::destroyInst();
  DataManager::destroyInst();
}

// I2: SdfWriter::outputSdfCellList iterates network().instances() directly,
// so the SDF (CELL ...) block sequence is instance-name sorted.
void checkSdfCellOrderIsNameSorted()
{
  DataManager::initInst();
  Database& database = STADM.getDatabase();
  Config config;
  database.network().setName("sdf_order_invariant");
  for (const char* name : {"zz_inst", "mm_inst", "aa_inst"}) {
    addInstanceWithCellArc(name);
  }
  std::ostringstream out;
  SdfWriter(database, config).write(out);
  const std::string text = out.str();
  const std::size_t aa = text.find("(INSTANCE aa_inst)");
  const std::size_t mm = text.find("(INSTANCE mm_inst)");
  const std::size_t zz = text.find("(INSTANCE zz_inst)");
  require(aa != std::string::npos && mm != std::string::npos && zz != std::string::npos, "SDF did not emit all three CELL blocks");
  require(aa < mm && mm < zz, "SDF CELL block order is not instance-name sorted");
  DataManager::destroyInst();
}

}  // namespace

int main()
{
  Logger::initInst();
  checkVertexOrderIsNameSorted();
  checkTclQueryOrderIsNameSorted();
  checkSdfCellOrderIsNameSorted();
  Logger::destroyInst();
}

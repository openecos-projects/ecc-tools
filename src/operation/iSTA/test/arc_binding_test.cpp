// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>
#include <utility>

#include "Database.hpp"

namespace {
void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}
}  // namespace

int main()
{
  ista::Database original;
  auto& arcs = original.liberty().get_cell_map()["MUX"].get_cell_arc_list();
  arcs.resize(2);
  for (auto& arc : arcs) {
    arc.set_source_port("A");
    arc.set_sink_port("Y");
  }
  arcs[0].set_delay(2.0);
  arcs[1].set_delay(7.0);
  ista::Arc edge;
  edge.setLibertyArcId(ista::LibertyArcId{"MUX", 1});
  original.graph().arcs().push_back(edge);
  ista::Database copy = original;
  auto* bound = copy.liberty().findCellArc(*copy.graph().arcs().front().libertyArcId());
  require(bound == &copy.liberty().get_cell_map().at("MUX").get_cell_arc_list()[1], "copied graph still refers to original Liberty storage");
  original = ista::Database{};
  ista::Database moved = std::move(copy);
  // Force library vector relocation, then resolve the exact second same-port set.
  moved.liberty().get_cell_map().at("MUX").get_cell_arc_list().resize(100);
  const auto& library = std::as_const(moved).liberty();
  const auto* retained = library.findCellArc(*moved.graph().arcs().front().libertyArcId());
  require(retained && retained->get_delay() == 7.0, "binding lost after owner destruction/move/vector relocation");
  require(library.findCellArc({"missing", 1}) == nullptr, "missing cell resolved");
  require(library.findCellArc({"MUX", 100}) == nullptr, "out-of-range arc resolved");
  require(!ista::Arc{}.libertyArcId(), "unbound edge unexpectedly references a library");
}

// SPDX-License-Identifier: MulanPSL-2.0
#include <string>
#include <vector>

#include "query/ObjectQuery.hpp"
#if __has_include(<tcl8.6/tcl.h>)
#include <tcl8.6/tcl.h>
#else
#include <tcl.h>
#endif
#include <memory>
#include <stdexcept>

int main(int, char** argv)
{
  Tcl_FindExecutable(argv[0]);
  std::unique_ptr<Tcl_Interp, decltype(&Tcl_DeleteInterp)> interpreter(Tcl_CreateInterp(), Tcl_DeleteInterp);
  auto* interp = interpreter.get();
  ista::Database db;
  for (const auto* name : {"bus[0]", "DATA", "dout"}) {
    auto& pin = db.network().pins()[name];
    pin.set_is_port(true);
    pin.set_pin_name(name);
    pin.set_direction(ista::PinDirection::kInput);
  }
  using namespace ista::sdc;
  if (findObjects(interp, db, {"bus[0]"}, QueryObjectType::kPort) != std::vector<std::string>{"bus[0]"})
    throw std::runtime_error("literal bus fallback changed");
  ObjectQueryOptions options;
  options.regexp = true;
  options.nocase = true;
  options.filter = "direction == in && name =~ d.*";
  if (findObjects(interp, db, {"d.*"}, QueryObjectType::kPort, options) != std::vector<std::string>{"DATA", "dout"})
    throw std::runtime_error("Tcl regexp/filter semantics changed");
  if (parseObjectPatterns(interp, "{bus[0]} DATA", false) != std::vector<std::string>{"bus[0]", "DATA"})
    throw std::runtime_error("Tcl list parsing changed");
  auto& cell = db.liberty().get_cell_map()["pad"];
  cell.set_library_name("io");
  auto& port = cell.get_port_map()["PAD"];
  port.set_is_input(true);
  port.set_is_output(true);
  ObjectQueryOptions inout;
  inout.filter = "direction == inout";
  if (findObjects(interp, db, {"*"}, QueryObjectType::kLibPin, inout) != std::vector<std::string>{"io/pad/PAD"})
    throw std::runtime_error("library bidirectional pin was reported as input-only");
  bool rejected = false;
  try {
    findObjects(interp, db, {"["}, QueryObjectType::kPort, true);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  if (!rejected)
    throw std::runtime_error("invalid regexp accepted");
}

// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

#include "def_read.h"
#include "def_service.h"
#include "def_write.h"
#include "verilog_read.h"
#include "verilog_write.h"

namespace {
void require(bool value, const std::string& message)
{
  if (!value)
    throw std::runtime_error(message);
}
struct Fixture
{
  std::filesystem::path directory = std::filesystem::temp_directory_path() / ("verilog_net_names_" + std::to_string(getpid()));
  idb::IdbLayout layout;
  Fixture()
  {
    std::filesystem::create_directory(directory);
    layout.get_units()->set_microns_dbu(1000);
    auto* cell = layout.get_cell_master_list()->set_cell_master("BUF");
    cell->add_term("A")->set_direction(idb::IdbConnectDirection::kInput);
    cell->add_term("Y")->set_direction(idb::IdbConnectDirection::kOutput);
  }
  ~Fixture() { std::filesystem::remove_all(directory); }
  std::string write(const std::string& name, const std::string& text) const
  {
    const auto path = directory / name;
    std::ofstream file(path);
    file << text;
    require(file.good(), "cannot write fixture: " + name);
    return path.string();
  }
};

using Connectivity = std::map<std::string, std::set<std::string>>;
Connectivity snapshot(idb::IdbDesign& design)
{
  Connectivity result;
  for (auto* net : design.get_net_list()->get_net_list()) {
    auto& pins = result[net->get_net_name()];
    for (auto* pin : net->get_io_pins()->get_pin_list()) {
      require(pin->get_net() == net && pin->get_net_name() == net->get_net_name(), "stale IO pin connection/name");
      pins.insert("PIN/" + pin->get_pin_name());
    }
    for (auto* pin : net->get_instance_pin_list()->get_pin_list()) {
      require(pin->get_net() == net && pin->get_net_name() == net->get_net_name(), "stale instance pin connection/name");
      pins.insert(pin->get_instance()->get_name() + "/" + pin->get_pin_name());
    }
  }
  return result;
}

void roundTrip(Fixture& fixture, idb::IdbDefService& service, const std::string& suffix)
{
  service.get_design()->get_units()->set_microns_dbu(1000);
  const auto before = snapshot(*service.get_design());
  const auto def_path = fixture.directory / ("saved.def" + suffix);
  const auto verilog_path = fixture.directory / ("saved.v" + suffix);
  require(idb::DefWrite(&service, idb::DefWriteType::kSynthesis).writeDb(def_path.c_str()), "DEF export failed");
  const std::set<std::string> excluded;
  idb::VerilogWriter(verilog_path.c_str(), excluded, *service.get_design(), false).writeModule();
  require(snapshot(*service.get_design()) == before, "saving must not rename or reconnect live iDB objects");
  idb::IdbDefService from_def(&fixture.layout), from_verilog(&fixture.layout);
  require(idb::DefRead(&from_def).createDb(def_path.c_str()), "DEF reload failed");
  require(idb::VerilogRead(&from_verilog).createDbAutoTop(verilog_path.string()), "Verilog reload failed");
  require(snapshot(*from_def.get_design()) == before, "DEF changed net names or connectivity");
  require(snapshot(*from_verilog.get_design()) == before, "Verilog changed net names or connectivity");
}

void aliases(Fixture& fixture)
{
  // The port alias is declared first, but the physical tie/buffer output is mem_addr_0.
  for (const auto* assigns : {"assign eoi_0=alias; assign alias=mem_addr_0;", "assign alias=mem_addr_0; assign eoi_0=alias;"}) {
    const auto path = fixture.write("aliases.v", std::string("module top(input clk, output eoi_0, output mem_addr_0); wire alias; ")
                                                     + assigns + " BUF tie(.A(clk),.Y(mem_addr_0)); endmodule");
    idb::IdbDefService service(&fixture.layout);
    require(idb::VerilogRead(&service).createDbAutoTop(path), "alias import failed");
    const Connectivity expected{{"clk", {"PIN/clk", "tie/A"}}, {"mem_addr_0", {"PIN/eoi_0", "PIN/mem_addr_0", "tie/Y"}}};
    require(snapshot(*service.get_design()) == expected, "output alias replaced the physical net name");
    roundTrip(fixture, service, "");
  }
  // For an input port, the assignment direction is reversed; blindly choosing RHS loses the internal net name.
  const auto path = fixture.write("input.v",
                                  "module top(input a, output y); wire internal; assign internal=a;"
                                  " BUF u(.A(internal),.Y(y)); endmodule");
  idb::IdbDefService service(&fixture.layout);
  require(idb::VerilogRead(&service).createDbAutoTop(path), "input alias import failed");
  const Connectivity expected{{"internal", {"PIN/a", "u/A"}}, {"y", {"PIN/y", "u/Y"}}};
  require(snapshot(*service.get_design()) == expected, "input alias replaced the physical net name");
  roundTrip(fixture, service, ".gz");
}

void collisions(Fixture& fixture)
{
  const auto path = fixture.write("collision.def", R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;
COMPONENTS 2 ;
- driver BUF ;
- hold22 BUF ;
END COMPONENTS
PINS 3 ;
- clk + NET clk + DIRECTION INPUT + USE SIGNAL ;
- o_quotient_31 + NET o_flags_2 + DIRECTION OUTPUT + USE SIGNAL ;
- o_flags_2 + NET o_flags_2 + DIRECTION OUTPUT + USE SIGNAL ;
END PINS
NETS 5 ;
- clk ( PIN clk ) ( driver A ) ;
- o_quotient_31 ( driver Y ) ( hold22 A ) ;
- o_flags_2 ( hold22 Y ) ( PIN o_quotient_31 ) ( PIN o_flags_2 ) ;
- hold22 ;
- __ecc_net_0 ;
END NETS
END DESIGN
)");
  idb::IdbDefService service(&fixture.layout);
  require(idb::DefRead(&service).createDb(path.c_str()), "DEF fixture import failed");
  auto& design = *service.get_design();
  auto* internal = design.get_instance_list()->find_instance("hold22")->get_pin("A")->get_net();
  require(internal->get_net_name() != "o_quotient_31", "port/net collision must be resolved in iDB before export");
  require(!design.get_net_list()->find_net("hold22"), "instance/net collision must be resolved in iDB");
  const auto expected = snapshot(design);
  require(expected.at(internal->get_net_name()) == std::set<std::string>{"driver/Y", "hold22/A"}, "buffer input changed");
  require(expected.at("o_flags_2") == std::set<std::string>{"hold22/Y", "PIN/o_quotient_31", "PIN/o_flags_2"}, "buffer output changed");
  require(expected.at("clk") == std::set<std::string>{"PIN/clk", "driver/A"}, "legal shared port/net name changed");
  require(expected.contains("__ecc_net_0") && expected.size() == 5, "renaming collided with an existing net");
  require(design.canonicalizeNetNames() == 0 && snapshot(design) == expected, "net naming must be idempotent");
  roundTrip(fixture, service, "");
  roundTrip(fixture, service, ".gz");
}

void topologyEdit(Fixture& fixture)
{
  const auto path = fixture.write("edit.v", "module top(input clk, output y); BUF driver(.A(clk),.Y(y)); endmodule");
  idb::IdbDefService service(&fixture.layout);
  require(idb::VerilogRead(&service).createDbAutoTop(path), "edit fixture import failed");
  auto& design = *service.get_design();
  auto* old_net = design.get_net_list()->find_net("y");
  auto* buffer = design.createInstance("buffer", "BUF");
  auto* new_net = design.createOrFindNet("buffer_out");
  design.connectPinToNet(buffer->get_pin("A"), old_net);
  design.connectPinToNet(buffer->get_pin("Y"), new_net);
  design.connectPinToNet(design.get_io_pin_list()->find_pin("y"), new_net);
  const auto before = snapshot(design);
  bool rejected = false;
  try {
    const std::set<std::string> excluded;
    idb::VerilogWriter((fixture.directory / "conflicting.v").c_str(), excluded, design, false).writeModule();
  } catch (const std::runtime_error& error) {
    rejected = std::string(error.what()).find("iDB net name conflicts") != std::string::npos;
  }
  require(rejected && snapshot(design) == before, "writer must reject an unresolved conflict without silently renaming");
  require(design.canonicalizeNetNames() == 1, "output buffering must rename exactly the detached port net");
  require(design.get_net_list()->find_net(old_net->get_net_name()) == old_net, "renaming invalidated the iDB lookup index");
  require(buffer->get_pin("A")->get_net() == old_net && buffer->get_pin("Y")->get_net() == new_net, "renaming changed net identity");
  for (const auto* reserved : {"y", "buffer", "buffer_out"})
    require(design.makeUniqueNetName(reserved) != reserved, "generated net name conflicts with a port, instance or net");
  roundTrip(fixture, service, "");

  // Bidirectional ports cannot use assign aliases; the underlying iDB net must
  // take the port name as well, after moving any conflicting internal net.
  auto* io = design.createOrFindIoPin("pad");
  io->set_term();
  io->get_term()->set_name("pad");
  io->get_term()->set_direction(idb::IdbConnectDirection::kInOut);
  auto* pad_net = design.createOrFindNet("pad_internal");
  auto* conflict = design.createOrFindNet("pad");
  design.connectPinToNet(io, pad_net);
  require(design.canonicalizeNetNames() == 2, "inout name transfer did not resolve both names");
  require(design.get_net_list()->find_net("pad") == pad_net && conflict->get_net_name() != "pad", "inout stole a different net");
  require(design.canonicalizeNetNames() == 0, "inout naming must be idempotent");
  roundTrip(fixture, service, ".gz");
}
}  // namespace

int main(int argc, char** argv)
{
  try {
    setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
    Fixture fixture;
    require(argc == 2, "expected aliases or def");
    if (std::string(argv[1]) == "aliases")
      aliases(fixture);
    else {
      collisions(fixture);
      topologyEdit(fixture);
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

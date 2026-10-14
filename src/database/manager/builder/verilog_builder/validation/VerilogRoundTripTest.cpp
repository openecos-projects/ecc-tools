// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <type_traits>

#include "def_service.h"
#include "verilog/VerilogSyntax.hh"
#include "verilog_read.h"
#include "verilog_write.h"
static_assert(!std::is_copy_constructible_v<idb::VerilogWriter>);
namespace {
void require(bool value, const std::string& message)
{
  if (!value)
    throw std::runtime_error(message);
}
struct Files
{
  std::filesystem::path directory = std::filesystem::temp_directory_path() / ("verilog_roundtrip_" + std::to_string(getpid()));
  Files() { std::filesystem::create_directory(directory); }
  ~Files() { std::filesystem::remove_all(directory); }
};
void names(const Files& files, const std::string& suffix)
{
  idb::IdbLayout layout;
  auto* master = layout.get_cell_master_list()->set_cell_master("cell");
  master->add_term("input")->set_direction(idb::IdbConnectDirection::kInput);
  master->add_term("Y")->set_direction(idb::IdbConnectDirection::kOutput);
  const auto source = files.directory / "names.v";
  {
    std::ofstream f(source);
    f << R"(module \module (input \input , output \out\name ); wire \literal[0] ;
 \cell \u\0 (.\input (\input ),.Y(\out\name ));
 \cell \assign (.\input (\literal[0] )); endmodule)";
  }
  idb::IdbDefService original(&layout);
  require(idb::VerilogRead(&original).createDbAutoTop(source.string()), "fixture import failed");
  const auto saved = files.directory / ("names" + suffix);
  {
    std::set<std::string> excluded;
    idb::VerilogWriter writer(saved.c_str(), excluded, *original.get_design(), false);
    writer.writeModule();
  }
  auto parsed = idb::verilog::readFile(saved.string());
  require(bool(parsed), parsed ? "" : "export is invalid Verilog: " + parsed.diagnostics.front().text());
  idb::IdbDefService reloaded(&layout);
  require(idb::VerilogRead(&reloaded).createDbAutoTop(saved.string()), "name roundtrip import failed");
  require(reloaded.get_design()->get_design_name() == "module", "module keyword changed");
  auto* cell = reloaded.get_design()->get_instance_list()->find_instance("u\\0");
  require(cell && cell->get_pin("input")->get_net()->get_net_name() == "input", "keyword pin or internal backslash lost");
  require(cell->get_pin("Y")->get_net()->get_net_name() == "out\\name", "internal backslash in net changed");
  auto* other = reloaded.get_design()->get_instance_list()->find_instance("assign");
  require(other && other->get_pin("input")->get_net()->get_net_name() == "literal[0]", "literal brackets became a bus");
}
void openBus(const Files& files)
{
  idb::IdbLayout layout;
  auto* master = layout.get_cell_master_list()->set_cell_master("BUS");
  for (auto direction : {idb::IdbConnectDirection::kInput, idb::IdbConnectDirection::kOutput})
    for (int bit = 0; bit < 2; ++bit)
      master->add_term(std::string(direction == idb::IdbConnectDirection::kInput ? "D[" : "Y[") + std::to_string(bit) + "]")
          ->set_direction(direction);
  const auto source = files.directory / "bus.v";
  {
    std::ofstream f(source);
    f << R"(module top(input a,output y); BUS partial(.\D[0] (a),.\Y[1] (y)); BUS empty(.D(),.Y()); endmodule)";
  }
  idb::IdbDefService original(&layout);
  require(idb::VerilogRead(&original).createDbAutoTop(source.string()), "partial bus fixture failed");
  const auto saved = files.directory / "bus_saved.v";
  {
    std::set<std::string> excluded;
    idb::VerilogWriter writer(saved.c_str(), excluded, *original.get_design(), true);
    writer.writeModule();
  }
  idb::IdbDefService reloaded(&layout);
  require(idb::VerilogRead(&reloaded).createDbAutoTop(saved.string()), "open bus roundtrip failed");
  auto* cell = reloaded.get_design()->get_instance_list()->find_instance("partial");
  require(cell->get_pin("D[0]")->get_net() == reloaded.get_design()->get_io_pin_list()->find_pin("a")->get_net(),
          "input bit order changed");
  require(cell->get_pin("Y[1]")->get_net() == reloaded.get_design()->get_io_pin_list()->find_pin("y")->get_net(),
          "output bit order changed");
  for (const auto* name : {"D[1]", "Y[0]"}) {
    auto* net = cell->get_pin(name)->get_net();
    require(!net
                || (!net->is_ground() && !net->is_power() && net->get_instance_pin_list()->get_pin_num() == 1
                    && net->get_io_pins()->get_pin_num() == 0),
            "open bit tied to a driver");
  }
  auto* empty = reloaded.get_design()->get_instance_list()->find_instance("empty");
  for (auto* pin : empty->get_pin_list()->get_pin_list())
    require(!pin->get_net(), "fully open bus must remain open");
}
void dollarMaster(const Files& files)
{
  idb::IdbLayout layout;
  layout.get_cell_master_list()->set_cell_master("$cell");
  idb::IdbDefService service(&layout);
  service.get_design()->set_design_name("top");
  require(service.get_design()->createInstance("u", "$cell") != nullptr, "dollar master fixture failed");
  const auto saved = files.directory / "dollar.v";
  std::set<std::string> excluded;
  idb::VerilogWriter writer(saved.c_str(), excluded, *service.get_design(), false);
  writer.writeModule();
  idb::IdbDefService reloaded(&layout);
  require(idb::VerilogRead(&reloaded).createDbAutoTop(saved.string()), "dollar master roundtrip failed");
  require(reloaded.get_design()->get_instance_list()->find_instance("u")->get_cell_master()->get_name() == "$cell",
          "dollar master changed");
}
void connectedPowerPins(const Files& files)
{
  idb::IdbLayout layout;
  auto* master = layout.get_cell_master_list()->set_cell_master("PLL");
  for (const auto* name : {"AVDD", "AVSS"}) {
    auto* term = master->add_term(name);
    term->set_direction(idb::IdbConnectDirection::kInOut);
    term->set_type(std::string(name) == "AVDD" ? idb::IdbConnectType::kPower : idb::IdbConnectType::kGround);
  }
  const auto input = files.directory / "power.v";
  {
    std::ofstream file(input);
    file << "module top(inout vdd,inout vss); PLL u(.AVDD(vdd),.AVSS(vss)); endmodule";
  }
  idb::IdbDefService service(&layout);
  require(idb::VerilogRead(&service).createDbAutoTop(input.string()), "power fixture failed");
  const auto saved = files.directory / "power-saved.v";
  std::set<std::string> excluded;
  idb::VerilogWriter writer(saved.c_str(), excluded, *service.get_design(), false);
  writer.writeModule();
  idb::IdbDefService reloaded(&layout);
  require(idb::VerilogRead(&reloaded).createDbAutoTop(saved.string()), "power roundtrip failed");
  auto* cell = reloaded.get_design()->get_instance_list()->find_instance("u");
  require(cell && cell->get_pin("AVDD")->get_net() && cell->get_pin("AVSS")->get_net(), "connected power pins were dropped on export");
  require(cell->get_pin("AVDD")->get_net() == reloaded.get_design()->get_io_pin_list()->find_pin("vdd")->get_net(), "power net changed");
  require(cell->get_pin("AVSS")->get_net() == reloaded.get_design()->get_io_pin_list()->find_pin("vss")->get_net(), "ground net changed");
}
void outputError(const Files& files)
{
  idb::IdbLayout layout;
  idb::IdbDefService service(&layout);
  service.get_design()->set_design_name("top");
  bool failed = false;
  try {
    std::set<std::string> excluded;
    idb::VerilogWriter writer((files.directory / "missing/out.v").c_str(), excluded, *service.get_design(), true);
    writer.writeModule();
  } catch (const std::runtime_error&) {
    failed = true;
  }
  require(failed, "unwritable output must report an error");
}
}  // namespace
int main()
{
  try {
    setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
    Files files;
    names(files, ".v");
    names(files, ".v.gz");
    openBus(files);
    dollarMaster(files);
    connectedPowerPins(files);
    outputError(files);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

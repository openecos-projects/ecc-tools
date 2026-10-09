// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include "verilog_read.h"

#include "VerilogImportPlan.hh"
#include "VerilogLibrary.hh"
#include "def_service.h"
#include "utility/logger/Logger.hpp"
#include "verilog/VerilogParser.hh"
namespace idb {
namespace {
using namespace verilog;
using namespace verilog_import;
struct ImportError : std::runtime_error
{
  using std::runtime_error::runtime_error;
};
IdbConnectDirection direction(Direction direction)
{
  switch (direction) {
    case Direction::input:
      return IdbConnectDirection::kInput;
    case Direction::output:
      return IdbConnectDirection::kOutput;
    case Direction::inout:
      return IdbConnectDirection::kInOut;
    default:
      return IdbConnectDirection::kNone;
  }
}

// Execution only: all names, widths, constants and bus members were resolved by planning.
class Importer
{
 public:
  Importer(IdbDesign& design, const VerilogImportPlan& plan) : _design(design), _plan(plan) {}
  void run()
  {
    _design.set_design_name(_plan.top);
    _nets.reserve(_plan.nets.size());
    for (const auto& net : _plan.nets) {
      const auto type = net.type == NetType::supply0   ? IdbConnectType::kGround
                        : net.type == NetType::supply1 ? IdbConnectType::kPower
                                                       : IdbConnectType::kSignal;
      auto* created = _design.createOrFindNet(net.name, type);
      if (!created)
        throw ImportError("cannot create net: " + net.name);
      _nets.push_back(created);
    }
    std::vector<IdbPin*> ports;
    ports.reserve(_plan.ports.size());
    for (const auto& port : _plan.ports) {
      auto* pin = _design.createOrFindIoPin(port.name);
      if (!pin)
        throw ImportError("cannot create IO pin: " + port.name);
      if (!pin->get_term())
        pin->set_term();
      pin->get_term()->set_name(port.name);
      pin->get_term()->set_direction(direction(port.direction));
      pin->get_term()->set_type(IdbConnectType::kSignal);
      pin->set_as_io();
      connect(pin, port.net);
      ports.push_back(pin);
    }
    pinBuses(_plan.port_buses, ports, IdbBus::kBusType::kBusIo);
    for (const auto& entry : _plan.net_buses) {
      IdbBus bus(entry.name, entry.left, entry.right);
      bus.set_type(IdbBus::kBusType::kBusNet);
      for (const auto& member : entry.members)
        bus.addNet(_nets.at(member.member), member.index);
      _design.get_bus_list()->addBusObject(std::move(bus));
    }
    for (const auto& cell : _plan.instances) {
      auto* instance = _design.createInstance(cell.name, cell.master);
      if (!instance)
        throw ImportError("cannot create instance: " + cell.name);
      std::vector<IdbPin*> pins;
      pins.reserve(cell.pins.size());
      for (const auto& entry : cell.pins) {
        auto* pin = instance->get_pin(entry.name);
        if (!pin)
          throw ImportError("cannot create instance pin: " + cell.name + "/" + entry.name);
        connect(pin, entry.net);
        pins.push_back(pin);
      }
      pinBuses(cell.buses, pins, IdbBus::kBusType::kBusInstancePin);
    }
  }

 private:
  void connect(IdbPin* pin, NetId net)
  {
    if (!_design.connectPinToNet(pin, _nets.at(net)))
      throw ImportError("cannot connect pin: " + pin->get_pin_name());
  }
  void pinBuses(const std::vector<PlannedBus>& entries, const std::vector<IdbPin*>& pins, IdbBus::kBusType type)
  {
    for (const auto& entry : entries) {
      IdbBus bus(entry.name, entry.left, entry.right);
      bus.set_type(type);
      for (const auto& member : entry.members)
        bus.addPin(pins.at(member.member), member.index);
      _design.get_bus_list()->addBusObject(std::move(bus));
    }
  }
  IdbDesign& _design;
  const VerilogImportPlan& _plan;
  std::vector<IdbNet*> _nets;
};
std::string import(IdbDefService* service, const std::string& file, const std::string& top)
{
  // Return diagnostics after RAII cleanup: the caller's Error policy may terminate the process.
  auto parsed = readFile(file);
  if (!parsed)
    return parsed.diagnostics.front().text();
  if (!service || !service->get_layout() || !service->get_design())
    return "missing Verilog import service, design or LEF layout";
  try {
    VerilogLibrary library(*service->get_layout());
    auto flat = elaborate(*parsed.design, top, [&](std::string_view cell, std::string_view pin) { return library.resolve(cell, pin); });
    if (!flat)
      return flat.diagnostics.front().text();
    parsed.design.reset();
    auto planned = makeImportPlan(*flat.design, library);
    if (!planned)
      return planned.diagnostics.front().text();
    flat.design.reset();
    Importer(*service->get_design(), *planned.plan).run();
  } catch (const detail::Error& error) {
    return Diagnostic{file, error.location, error.what()}.text();
  } catch (const ImportError& error) {
    return error.what();
  }
  return {};
}
}  // namespace
bool VerilogRead::createDb(std::string file, std::string top_module_name)
{
  const auto error = import(_def_service, file, top_module_name);
  if (!error.empty()) {
    ECCLOG.error(ecc::Loc::current(), "Verilog import failed: input=", file, ", ", error);
    return false;
  }
  return true;
}
bool VerilogRead::createDbAutoTop(std::string file)
{
  return createDb(std::move(file), {});
}
}  // namespace idb

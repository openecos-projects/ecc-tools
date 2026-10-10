// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include "verilog_write.h"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "IdbDesign.h"
#include "VerilogConstantNet.hh"
#include "VerilogLibrary.hh"
#include "utility/logger/Logger.hpp"
#include "verilog/VerilogLexer.hh"
namespace idb {
namespace {
using verilog::encodeIdentifier;
class Output
{
 public:
  explicit Output(const std::string& path)
  {
    if (path.ends_with(".gz")) {
      _gzip.reset(gzopen(path.c_str(), "wb"));
      if (!_gzip)
        throw std::runtime_error("cannot open output: " + path);
      gzbuffer(_gzip.get(), 1U << 20);
    } else {
      _plain.reset(std::fopen(path.c_str(), "wb"));
      if (!_plain)
        throw std::runtime_error("cannot open output: " + path);
    }
  }
  void write(std::string_view text)
  {
    while (!text.empty()) {
      const auto size = std::min(text.size(), size_t(1U << 20));
      const auto count = _gzip ? gzwrite(_gzip.get(), text.data(), static_cast<unsigned>(size))
                               : static_cast<int>(std::fwrite(text.data(), 1, size, _plain.get()));
      if (count != static_cast<int>(size))
        throw std::runtime_error("failed writing Verilog output");
      text.remove_prefix(size);
    }
  }
  void finish()
  {
    const int result = _gzip ? gzclose(_gzip.release()) : std::fclose(_plain.release());
    if (result != 0)
      throw std::runtime_error("failed closing Verilog output");
  }

 private:
  struct CloseFile
  {
    void operator()(FILE* file) const { std::fclose(file); }
  };
  struct CloseGzip
  {
    void operator()(gzFile_s* file) const { gzclose(file); }
  };
  std::unique_ptr<FILE, CloseFile> _plain;
  std::unique_ptr<gzFile_s, CloseGzip> _gzip;
};
bool powerPin(IdbPin* pin)
{
  return pin->get_term()->get_type() == IdbConnectType::kPower || pin->get_term()->get_type() == IdbConnectType::kGround;
}
struct ExportPort
{
  std::string name;
  std::vector<std::string> pins;
  bool power = false;
};
class Serializer
{
 public:
  Serializer(IdbDesign& design, const std::set<std::string>& excluded) : _design(design), _excluded(excluded) {}
  void run(Output& out)
  {
    prepareNames();
    out.write("module " + encodeIdentifier(_design.get_design_name()) + " (\n");
    for (size_t i = 0; i < _ports.size(); ++i) {
      if (i)
        out.write(",\n");
      out.write("  " + encodeIdentifier(_ports[i]->get_pin_name()));
    }
    out.write("\n);\n");
    for (auto* port : _ports) {
      const auto d = port->get_term()->get_direction();
      out.write(std::string(d == IdbConnectDirection::kInput    ? "input "
                            : d == IdbConnectDirection::kOutput ? "output "
                                                                : "inout ")
                + encodeIdentifier(port->get_pin_name()) + ";\n");
    }
    for (auto* net : _design.get_net_list()->get_net_list()) {
      const auto name = net->get_net_name();
      const bool zero = net->get_net_name() == verilogZeroNet && net->is_ground();
      const bool one = net->get_net_name() == verilogOneNet && net->is_power();
      if (zero || one || !_port_names.count(name))
        out.write(std::string(zero ? "supply0 " : one ? "supply1 " : "wire ") + encodeIdentifier(name) + ";\n");
    }
    for (auto* port : _ports) {
      if (!port->get_net())
        continue;
      const auto name = port->get_net()->get_net_name();
      if (name == port->get_pin_name())
        continue;
      const auto d = port->get_term()->get_direction();
      if (d == IdbConnectDirection::kInOut)
        throw std::runtime_error("multiple aliased inout ports require an unsupported tran connection: " + port->get_pin_name());
      const auto pin = encodeIdentifier(port->get_pin_name()), net = encodeIdentifier(name);
      out.write("assign " + (d == IdbConnectDirection::kInput ? net + " = " + pin : pin + " = " + net) + ";\n");
    }
    for (auto* instance : _design.get_instance_list()->get_instance_list()) {
      if (_excluded.count(instance->get_cell_master()->get_name()))
        continue;
      writeInstance(out, *instance);
    }
    out.write("endmodule\n");
  }

 private:
  std::string fresh(std::string_view prefix)
  {
    std::string name;
    do {
      name = std::string(prefix) + std::to_string(_next++);
    } while (!_used.insert(name).second);
    return name;
  }
  void prepareNames()
  {
    std::unordered_map<std::string, IdbNet*> port_nets;
    for (auto* pin : _design.get_io_pin_list()->get_pin_list()) {
      if (powerPin(pin) && !pin->get_net())
        continue;
      const auto direction = pin->get_term()->get_direction();
      if (direction != IdbConnectDirection::kInput && direction != IdbConnectDirection::kOutput && direction != IdbConnectDirection::kInOut)
        continue;
      _ports.push_back(pin);
      _port_names.insert(pin->get_pin_name());
      port_nets.emplace(pin->get_pin_name(), pin->get_net());
      _used.insert(pin->get_pin_name());
    }
    std::unordered_set<std::string> instance_names;
    for (auto* cell : _design.get_instance_list()->get_instance_list()) {
      _used.insert(cell->get_name());
      instance_names.insert(cell->get_name());
    }
    for (auto* net : _design.get_net_list()->get_net_list())
      _used.insert(net->get_net_name());
    std::unordered_set<std::string> chosen;
    for (auto* net : _design.get_net_list()->get_net_list()) {
      const auto& name = net->get_net_name();
      const auto port = port_nets.find(name);
      if ((port != port_nets.end() && port->second != net) || instance_names.count(name) || chosen.count(name))
        throw std::runtime_error("iDB net name conflicts with a port or instance: " + name
                                 + "; canonicalize net names after the topology edit, before initializing timing");
      chosen.insert(name);
    }
  }
  const std::vector<ExportPort>& ports(IdbCellMaster& master)
  {
    auto found = _interfaces.find(&master);
    if (found != _interfaces.end())
      return found->second;
    auto interface = verilog_import::VerilogLibrary::describe(master);
    std::unordered_set<std::string> grouped;
    std::vector<ExportPort> ports;
    for (const auto& [name, port] : interface.ports)
      if (port.range) {
        bool power = true;
        for (const auto& pin : port.pins) {
          auto* term = master.findTerm(pin);
          power &= term->get_type() == IdbConnectType::kPower || term->get_type() == IdbConnectType::kGround;
          grouped.insert(pin);
        }
        ports.push_back({name, port.pins, power});
      }
    for (auto* term : master.get_term_list())
      if (!grouped.count(term->get_name()))
        ports.push_back({term->get_name(),
                         {term->get_name()},
                         term->get_type() == IdbConnectType::kPower || term->get_type() == IdbConnectType::kGround});
    std::sort(ports.begin(), ports.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    return _interfaces.emplace(&master, std::move(ports)).first->second;
  }
  void writeInstance(Output& out, IdbInstance& instance)
  {
    // Missing bits of a partially connected bus use separate undriven wires.
    // Entirely open ports use .P(), preserving disconnection without a constant driver.
    std::string connections;
    for (const auto& port : ports(*instance.get_cell_master())) {
      std::vector<IdbNet*> nets;
      bool connected = false;
      for (const auto& name : port.pins) {
        auto* pin = instance.get_pin(name);
        auto* net = pin ? pin->get_net() : nullptr;
        nets.push_back(net);
        connected |= net != nullptr;
      }
      // Preserve explicit signal-net connections even on LEF POWER/GROUND terms.
      if (port.power && !connected)
        continue;
      if (!connections.empty())
        connections += ", ";
      connections += "." + encodeIdentifier(port.name) + "(";
      if (connected) {
        if (nets.size() > 1)
          connections += "{";
        for (size_t i = 0; i < nets.size(); ++i) {
          if (i)
            connections += ", ";
          if (nets[i])
            connections += encodeIdentifier(nets[i]->get_net_name());
          else {
            const auto name = fresh("__ecc_verilog_open_");
            out.write("wire " + name + ";\n");
            connections += name;
          }
        }
        if (nets.size() > 1)
          connections += "}";
      }
      connections += ")";
    }
    out.write(encodeIdentifier(instance.get_cell_master()->get_name()) + " " + encodeIdentifier(instance.get_name()) + " (" + connections
              + ");\n");
  }
  IdbDesign& _design;
  const std::set<std::string>& _excluded;
  std::vector<IdbPin*> _ports;
  std::unordered_map<IdbCellMaster*, std::vector<ExportPort>> _interfaces;
  std::unordered_set<std::string> _used, _port_names;
  uint64_t _next = 0;
};
}  // namespace
VerilogWriter::VerilogWriter(const char* file_name, const std::set<std::string>& excluded, IdbDesign& design, bool)
    : _file_name(file_name ? file_name : ""), _exclude_cell_names(excluded), _design(design)
{
}
void VerilogWriter::writeModule()
{
  std::string error;
  try {
    Output output(_file_name);
    Serializer(_design, _exclude_cell_names).run(output);
    output.finish();
  } catch (const std::exception& exception) {
    error = exception.what();
  }
  if (!error.empty())
    ECCLOG.error(ecc::Loc::current(), "Verilog export failed: output=", _file_name, ", ", error);
}
}  // namespace idb

// SPDX-License-Identifier: MulanPSL-2.0
#include <unistd.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "ReadParasitics.hpp"
#include "idm.h"
#include "network/Network.hpp"
#include "parasitics/Parasitics.hpp"

namespace {
void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}

void checkImport(const std::filesystem::path& file, const char* unit, const char* resistance_unit, double resistance_scale = 1.)
{
  // Both spellings use the same physical fF scale. Repeated CAP records must
  // sum before the 1 zF node floor; RES-only nodes need the same treatment.
  std::ofstream output(file);
  output << R"(*SPEF "IEEE 1481-1999"
*DESIGN "probe"
*DATE "test"
*VENDOR "test"
*PROGRAM "test"
*VERSION "1"
*DESIGN_FLOW "PIN_CAP NONE"
*DIVIDER /
*DELIMITER :
*BUS_DELIMITER [ ]
*T_UNIT 1 NS
*C_UNIT )"
         << unit << R"(
*R_UNIT )"
         << resistance_unit << R"(
*L_UNIT 1 HENRY
*D_NET n 2.0005
*CONN
*P in I
*I load:A I
*CAP
1 in 0
2 n:1 0.0000005
3 n:2 -0.001
4 n:3 0.00000075
5 n:3 0.00000075
6 n:4 2
7 load:A 0
8 n:6 0.0005
*RES
)";
  const std::array<const char*, 8> resistor_nodes{"in", "n:1", "n:2", "n:3", "n:4", "n:5", "n:6", "load:A"};
  const std::array<double, 7> resistances{-1., 0., .001, .009, .01, 10., .1};
  for (std::size_t index = 0; index < resistances.size(); ++index)
    output << index + 1 << ' ' << resistor_nodes[index] << ' ' << resistor_nodes[index + 1] << ' ' << resistances[index] / resistance_scale << '\n';
  output << "*END\n";
  output.close();
  require(dmInst->readSpef(file.string()), "SPEF fixture failed to parse");
  ista::Parasitics parasitics;
  ista::Network network;
  network.nets()["n"].set_pin_name_list({"in", "load:A"});
  ista::ReadParasitics reader(parasitics, network);
  for (int pass = 0; pass < 2; ++pass) {
    reader.read();
    auto& net = parasitics.nets().at("n");
    auto& nodes = net.get_node_map();
    require(nodes.size() == 8 && net.get_resistor_list().size() == 7, "import changed RC topology");
    for (const auto* name : {"in", "n:1", "n:2", "n:5", "load:A"})
      require(std::abs(nodes.at(name).get_capacitance() - 1e-9) < 1e-15, "missing, zero, negative or sub-zF node capacitance was not normalized");
    require(std::abs(nodes.at("n:3").get_capacitance() - 1.5e-9) < 1e-15, "node floor applied before summing duplicate CAP records");
    require(std::abs(nodes.at("n:4").get_capacitance() - .002) < 1e-15, "positive extracted capacitance changed");
    require(std::abs(nodes.at("n:6").get_capacitance() - 5e-7) < 1e-15, "sub-aF positive capacitance was inflated");
    double wire_cap = 0.;
    for (const auto& [name, node] : nodes)
      wire_cap += node.get_capacitance();
    require(std::abs(wire_cap - .0020005065) < 1e-15, "incorrect imported wire capacitance or repeated-read accumulation");
    const std::array<double, 7> expected_resistances{.01, .01, .01, .01, .01, 10., .1};
    for (std::size_t index = 0; index < expected_resistances.size(); ++index)
      require(std::abs(net.get_resistor_list()[index].get_resistance() - expected_resistances[index]) < 1e-12,
              "extracted resistance floor was not applied in physical ohms");
  }
}

void checkConnectivity(const std::filesystem::path& file)
{
  std::ofstream(file) << R"(*SPEF "IEEE 1481-1999"
*DESIGN "probe"
*DIVIDER /
*DELIMITER :
*BUS_DELIMITER [ ]
*T_UNIT 1 NS
*C_UNIT 1 PF
*R_UNIT 1 OHM
*L_UNIT 1 HENRY
*D_NET n 0.02
*CONN
*P in I
*I removed:A I
*CAP
1 in 0.01
2 removed:A 0.01
*RES
1 in removed:A 20
*END
)";
  require(dmInst->readSpef(file.string()), "stale SPEF fixture failed to parse");
  ista::Network network;
  network.nets()["n"].set_pin_name_list({"in", "block/load:A"});
  network.pins()["in"].set_is_port(true);
  network.pins()["block/load:A"].set_pin_name("A");
  ista::Parasitics parasitics;
  ista::ReadParasitics reader(parasitics, network);
  reader.read();
  require(parasitics.nets().empty(), "SPEF referencing a removed ECO pin was accepted");

  auto text = [&] {
    std::ifstream in(file);
    return std::string(std::istreambuf_iterator<char>(in), {});
  }();
  for (size_t pos = 0; (pos = text.find("removed:A", pos)) != std::string::npos; pos += 12)
    text.replace(pos, 9, "block/load/A");
  std::ofstream(file) << text;
  require(dmInst->readSpef(file.string()), "hierarchical SPEF fixture failed to parse");
  reader.read();
  require(parasitics.nets().contains("n"), "valid hierarchical pin separator was rejected");
  // Linking an assign can choose an internal net name while extraction keeps
  // the top-level port alias as *D_NET. All terminals still have to match.
  auto extracted = network.nets().extract("n");
  extracted.key() = "internal";
  network.nets().insert(std::move(extracted));
  text.replace(text.find("*D_NET n "), 9, "*D_NET in ");
  std::ofstream(file) << text;
  require(dmInst->readSpef(file.string()), "port-alias SPEF fixture failed to parse");
  network.pins()["in"].set_net_name("internal");
  reader.read();
  require(parasitics.nets().contains("internal") && !parasitics.nets().contains("in"), "top-level alias discarded a valid extracted RC network");
  require(parasitics.nets().at("internal").get_net_name() == "internal" && parasitics.nets().at("internal").get_resistor_list().size() == 1,
          "alias annotation changed the RC topology or lookup identity");
  network.nets()["internal"].set_pin_name_list({"in"});
  reader.read();
  require(parasitics.nets().empty(), "SPEF pin moved to another net retained stale parasitics");
}
}  // namespace

int main()
{
  const auto directory = std::filesystem::temp_directory_path() / ("ista-read-parasitics-" + std::to_string(getpid()));
  std::filesystem::create_directories(directory);
  try {
    checkImport(directory / "net.spef", "1 FF", "1 OHM");
    checkImport(directory / "net.spef", "0.001 PF", "0.001 KOHM");
    checkImport(directory / "net.spef", "1 FF", "1 KOHM", 1000.);
    checkConnectivity(directory / "stale.spef");
  } catch (...) {
    std::filesystem::remove_all(directory);
    throw;
  }
  std::filesystem::remove_all(directory);
}

// SPDX-License-Identifier: MulanPSL-2.0
#include <unistd.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "ReadParasitics.hpp"
#include "idm.h"
#include "parasitics/Parasitics.hpp"

namespace {
void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}

void checkImport(const std::filesystem::path& file, const char* unit)
{
  // Both spellings use the same physical fF scale. Repeated CAP records must
  // sum before the 1 aF node floor; RES-only nodes need the same treatment.
  std::ofstream(file) << R"(*SPEF "IEEE 1481-1999"
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
*C_UNIT )" << unit << R"(
*R_UNIT 1 OHM
*L_UNIT 1 HENRY
*D_NET n 2.001
*CONN
*P in I
*I load:A I
*CAP
1 in 0
2 n:1 0.0005
3 n:2 -0.001
4 n:3 0.00075
5 n:3 0.00075
6 n:4 2
7 load:A 0
*RES
1 in n:1 10
2 n:1 n:2 10
3 n:2 n:3 10
4 n:3 n:4 10
5 n:4 n:5 10
6 n:5 load:A 10
*END
)";
  require(dmInst->readSpef(file.string()), "SPEF fixture failed to parse");
  ista::Parasitics parasitics;
  ista::ReadParasitics reader(parasitics);
  for (int pass = 0; pass < 2; ++pass) {
    reader.read();
    auto& net = parasitics.nets().at("n");
    auto& nodes = net.get_node_map();
    require(nodes.size() == 7 && net.get_resistor_list().size() == 6, "import changed RC topology");
    for (const auto* name : {"in", "n:1", "n:2", "n:5", "load:A"})
      require(std::abs(nodes.at(name).get_capacitance() - 1e-6) < 1e-15,
              "missing, zero, negative or sub-aF node capacitance was not normalized");
    require(std::abs(nodes.at("n:3").get_capacitance() - 1.5e-6) < 1e-15, "node floor applied before summing duplicate CAP records");
    require(std::abs(nodes.at("n:4").get_capacitance() - .002) < 1e-15, "positive extracted capacitance changed");
    double wire_cap = 0.;
    for (const auto& [name, node] : nodes)
      wire_cap += node.get_capacitance();
    require(std::abs(wire_cap - .0020065) < 1e-15, "incorrect imported wire capacitance or repeated-read accumulation");
  }
}
}  // namespace

int main()
{
  const auto directory = std::filesystem::temp_directory_path() / ("ista-read-parasitics-" + std::to_string(getpid()));
  std::filesystem::create_directories(directory);
  try {
    checkImport(directory / "net.spef", "1 FF");
    checkImport(directory / "net.spef", "0.001 PF");
  } catch (...) {
    std::filesystem::remove_all(directory);
    throw;
  }
  std::filesystem::remove_all(directory);
}

// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2. This file is a regression test.
// ***************************************************************************************

#include "def_write.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "def_service.h"

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void testBusNameParsing()
{
  const idb::IdbBusBitChars bit_chars;
  const auto valid = idb::IdbBus::parseBusName("data[12]", bit_chars);
  require(valid.has_value() && valid->first == "data" && valid->second == 12, "valid bus bit must be parsed");

  require(!idb::IdbBus::parseBusName(R"(hierarchy\[0\].wdata_i\[0\])", bit_chars).has_value(),
          "escaped delimiters must remain a literal identifier");
  require(!idb::IdbBus::parseBusName("data[]", bit_chars).has_value(), "empty bus index must be rejected");
  require(!idb::IdbBus::parseBusName("data[ctrl]", bit_chars).has_value(), "non-numeric bus index must be rejected");
  require(!idb::IdbBus::parseBusName("data[-1]", bit_chars).has_value(), "negative bus index must be rejected");
  require(!idb::IdbBus::parseBusName("data[4294967296]", bit_chars).has_value(), "out-of-range bus index must be rejected");

  idb::IdbBus bus;
  bus.updateRange(12);
  require(bus.get_left() == 12 && bus.get_right() == 12, "first bus index must initialize both range bounds");
}

std::string readFile(const std::filesystem::path& path)
{
  std::ifstream stream(path);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void testEscapedNamesAreSerializedAsLiteralDefIdentifiers()
{
  const auto output_path = std::filesystem::temp_directory_path() / "def_writer_escaped_name_test.def";
  std::filesystem::remove(output_path);

  idb::IdbLayout layout;
  idb::IdbDefService service(&layout);
  auto* design = service.get_design();
  design->set_design_name("escaped_name_test");
  design->get_units()->set_microns_dbu(1000);

  auto* master = layout.get_cell_master_list()->set_cell_master("TEST_CELL");
  require(master != nullptr, "failed to create test cell master");
  require(master->add_term("D[0]") != nullptr, "failed to create test cell pin");
  auto* instance = design->createInstance("hierarchy[0].instance", "TEST_CELL");
  require(instance != nullptr, "failed to create test instance");

  auto* literal_net = design->createOrFindNet("hierarchy[0].wdata_i[0]");
  require(literal_net != nullptr, "failed to create literal bracket net");
  require(design->connectPinToNet(instance->get_pin("D[0]"), literal_net), "failed to connect test cell pin");
  auto* bus_net = design->createOrFindNet("data[0]");
  require(bus_net != nullptr, "failed to create bus net");

  idb::IdbBus bus("data", 0, 0);
  bus.set_type(idb::IdbBus::kBusType::kBusNet);
  bus.addNet(bus_net);
  design->get_bus_list()->addBusObject(std::move(bus));

  idb::DefWrite writer(&service, idb::DefWriteType::kSynthesis);
  require(writer.writeDb(output_path.c_str()), "failed to write test DEF");

  const std::string output = readFile(output_path);
  std::filesystem::remove(output_path);

  require(output.find(R"(- hierarchy\[0\].instance TEST_CELL)") != std::string::npos,
          "instance literal brackets must be escaped");
  require(output.find(R"(- hierarchy\[0\].wdata_i\[0\])") != std::string::npos,
          "net literal brackets must be escaped");
  require(output.find("- hierarchy[0].wdata_i[0]") == std::string::npos, "raw literal bracket net must not be written");
  require(output.find(R"(( hierarchy\[0\].instance D[0] ))") != std::string::npos,
          "instance name must be escaped while the library pin name remains a bus bit");
  require(output.find(R"(D\[0\])") == std::string::npos, "library pin bus delimiter must not be escaped");
  require(output.find("- data[0]") != std::string::npos, "real bus bit must retain its bus delimiters");
}

}  // namespace

int main()
{
  try {
    testBusNameParsing();
    testEscapedNamesAreSerializedAsLiteralDefIdentifiers();
  } catch (const std::exception& error) {
    std::cout << error.what() << '\n';
    return 1;
  }
  return 0;
}

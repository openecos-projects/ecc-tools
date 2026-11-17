#include <cstdlib>
#include <iostream>
#include <iterator>

#include "GeometrySpefDisplay.h"

using ecc::geometry::writeSpefDisplay;

int main()
{
  const auto root = std::filesystem::temp_directory_path() / "ecos_spef_display_test";
  std::filesystem::create_directories(root);
  const auto source = root / "corner.spef";
  const auto destination = root / "corner.gdisplay";
  std::ofstream(source) << "*C_UNIT 1 PF\n*NAME_MAP\n*1 bus[3]\n*2 n2\n*D_NET *1 0.125\n*END\n*D_NET *2 0.25\n*END\n";
  writeSpefDisplay(source);
  std::ifstream file(destination, std::ios::binary);
  auto integer = [&](unsigned size) {
    uint64_t value = 0;
    for (unsigned i = 0; i < size; ++i)
      value |= uint64_t(uint8_t(file.get())) << (8 * i);
    return value;
  };
  file.seekg(16);
  const auto size = integer(4);
  file.seekg(24);
  if (integer(8) != 2)
    return EXIT_FAILURE;
  const auto index = integer(8);
  file.seekg(index);
  const auto start = integer(8), end = integer(8);
  file.seekg(40 + size + start);
  std::vector<uint8_t> bytes(end - start);
  file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
  const auto item = nlohmann::json::from_msgpack(bytes);
  if (item != nlohmann::json({{"label", "bus[3]"}, {"nets", {"bus[3]"}}, {"value", 125.0}}))
    return EXIT_FAILURE;
  file.close();
  const auto length = std::filesystem::file_size(destination);
  std::ofstream(source) << "*C_UNIT 1 PF\n*D_NET *99 1\n";
  try {
    writeSpefDisplay(source);
    return EXIT_FAILURE;
  } catch (const std::runtime_error&) {
    if (std::filesystem::file_size(destination) != length || std::filesystem::exists(destination.string() + ".tmp"))
      return EXIT_FAILURE;
  }
  std::filesystem::remove_all(root);
  std::cout << "SPEF geometry units, name map, records and failed publication passed\n";
  return EXIT_SUCCESS;
}

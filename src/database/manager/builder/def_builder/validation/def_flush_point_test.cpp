// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// This file is a regression test.
// ***************************************************************************************

#include "def_read.h"
#include "def_write.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

/// MET2 WIDTH 0.1 in dbu 1000 -> routing width 100, half width 50
const std::string kFlushPointDef = "VERSION 5.8 ;\n"
                                   "DESIGN def_flush_point_test ;\n"
                                   "UNITS DISTANCE MICRONS 1000 ;\n"
                                   "DIEAREA ( 0 0 ) ( 12000 14000 ) ;\n"
                                   "COMPONENTS 0 ;\n"
                                   "END COMPONENTS\n"
                                   "NETS 5 ;\n"
                                   "    - n_default\n"
                                   "      + ROUTED MET1 ( 1000 1000 ) ( 2000 * ) ;\n"
                                   "    - n_ext_zero\n"
                                   "      + ROUTED MET1 ( 1000 3000 0 ) ( 2000 * ) ;\n"
                                   "    - n_ext_pos\n"
                                   "      + ROUTED MET1 ( 1000 5000 250 ) ( 2000 * ) ;\n"
                                   "    - n_ext_neg\n"
                                   "      + ROUTED MET1 ( 1000 7000 -50 ) ( 2000 * ) ;\n"
                                   "    - n_ext_second\n"
                                   "      + ROUTED MET1 ( 1000 9000 ) ( 2000 * 250 ) ;\n"
                                   "END NETS\n"
                                   "SPECIALNETS 3 ;\n"
                                   "    - vdd_default + USE POWER\n"
                                   "      + ROUTED MET1 100 ( 1000 11000 ) ( 2000 * ) ;\n"
                                   "    - vdd_ext_zero + USE POWER\n"
                                   "      + ROUTED MET1 100 ( 1000 12000 0 ) ( 2000 * ) ;\n"
                                   "    - vdd_ext_pos + USE POWER\n"
                                   "      + ROUTED MET1 100 ( 1000 13000 250 ) ( 2000 * ) ;\n"
                                   "END SPECIALNETS\n"
                                   "END DESIGN\n";

idb::IdbRegularWireSegment* findRegularSegment(idb::IdbDefService& service, const std::string& net_name)
{
  auto* net = service.get_design()->get_net_list()->find_net(net_name);
  require(net != nullptr, "regular net " + net_name + " must be parsed");
  return net->get_wire_list()->get_wire_list().at(0)->get_segment_list().at(0);
}

idb::IdbSpecialWireSegment* findSpecialSegment(idb::IdbDefService& service, const std::string& net_name)
{
  auto* net = service.get_design()->get_special_net_list()->find_net(net_name);
  require(net != nullptr, "special net " + net_name + " must be parsed");
  return net->get_wire_list()->get_wire_list().at(0)->get_segment_list().at(0);
}

void requireRect(const idb::IdbRect& rect, int32_t ll_x, int32_t ll_y, int32_t ur_x, int32_t ur_y, const std::string& message)
{
  require(rect.get_low_x() == ll_x && rect.get_low_y() == ll_y && rect.get_high_x() == ur_x && rect.get_high_y() == ur_y, message);
}

void testReadGeometry()
{
  idb::IdbLayout layout;
  idb::IdbDefService service(&layout);
  auto* layer = dynamic_cast<idb::IdbLayerRouting*>(layout.get_layers()->set_layer("MET1", "ROUTING"));
  require(layer != nullptr, "failed to create routing layer");
  layer->set_width(100);

  const auto def_path = std::filesystem::temp_directory_path() / "def_flush_point_test.def";
  {
    std::ofstream def_file(def_path);
    def_file << kFlushPointDef;
  }

  idb::DefRead reader(&service);
  require(reader.createDb(def_path.c_str()), "DEF reader must parse the flush point fixture");

  /// a plain point carries no ext, a flush point keeps its ext value
  require(findRegularSegment(service, "n_default")->get_point_ext(findRegularSegment(service, "n_default")->get_point_start())
              == std::nullopt,
          "a plain point must report no ext");
  require(findRegularSegment(service, "n_ext_zero")->get_point_ext(findRegularSegment(service, "n_ext_zero")->get_point_start())
              == std::optional<int32_t>(0),
          "a flush point must keep ext = 0");

  /// regular wire rect: no ext extends half the width past the end, ext ends the metal exactly ext beyond it
  requireRect(findRegularSegment(service, "n_default")->get_segment_rect(), 950, 950, 2050, 1050, "n_default rect mismatch");
  requireRect(findRegularSegment(service, "n_ext_zero")->get_segment_rect(), 1000, 2950, 2050, 3050, "n_ext_zero rect mismatch");
  requireRect(findRegularSegment(service, "n_ext_pos")->get_segment_rect(), 750, 4950, 2050, 5050, "n_ext_pos rect mismatch");
  requireRect(findRegularSegment(service, "n_ext_neg")->get_segment_rect(), 1050, 6950, 2050, 7050, "n_ext_neg rect mismatch");
  requireRect(findRegularSegment(service, "n_ext_second")->get_segment_rect(), 950, 8950, 2250, 9050, "n_ext_second rect mismatch");

  /// special wire bbox: an absent ext keeps the legacy flush end, an ext ends the metal exactly ext beyond it
  requireRect(*findSpecialSegment(service, "vdd_default")->get_bounding_box(), 1000, 10950, 2000, 11050, "vdd_default bbox mismatch");
  requireRect(*findSpecialSegment(service, "vdd_ext_zero")->get_bounding_box(), 1000, 11950, 2000, 12050,
              "vdd_ext_zero bbox mismatch");
  requireRect(*findSpecialSegment(service, "vdd_ext_pos")->get_bounding_box(), 750, 12950, 2000, 13050, "vdd_ext_pos bbox mismatch");

  /// the ext survives a DEF write round trip, and plain points stay plain
  const auto round_trip_path = std::filesystem::temp_directory_path() / "def_flush_point_test_round_trip.def";
  idb::DefWrite writer(&service, idb::DefWriteType::kChip);
  require(writer.writeDb(round_trip_path.c_str()), "DEF writer must write the round trip file");

  std::ifstream round_trip_file(round_trip_path);
  std::string round_trip((std::istreambuf_iterator<char>(round_trip_file)), std::istreambuf_iterator<char>());
  for (const std::string expected : {"( 1000 1000 ) ( 2000 * )", "( 1000 3000 0 ) ( 2000 * )", "( 1000 5000 250 ) ( 2000 * )",
                                     "( 1000 7000 -50 ) ( 2000 * )", "( 1000 9000 ) ( 2000 * 250 )", "( 1000 11000 ) ( 2000 * )",
                                     "( 1000 12000 0 ) ( 2000 * )", "( 1000 13000 250 ) ( 2000 * )"}) {
    require(round_trip.find(expected) != std::string::npos, "round trip DEF must contain " + expected);
  }

  std::filesystem::remove(def_path);
  std::filesystem::remove(round_trip_path);
}

}  // namespace

int main()
{
  try {
    testReadGeometry();
  } catch (const std::exception& error) {
    std::cout << error.what() << '\n';
    return 1;
  }
  return 0;
}

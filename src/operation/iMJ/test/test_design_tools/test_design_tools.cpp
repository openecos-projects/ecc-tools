// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "DAModel.hpp"
#include "DCModel.hpp"
#include "DEFCutter.hpp"
#include "DriverAnalyzer.hpp"
#include "IdbDesign.h"
#include "IdbLayout.h"
#include "Logger.hpp"
#include "MJInterface.hpp"
#include "idm.h"

class DesignToolsTest
{
 public:
  void run()
  {
    setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
    std::filesystem::create_directories(_output);
    loadDEF();
    MJI.initMJ({{"-temp_directory_path", _output.string()}});
    testDriverAnalysis();
    testDEFCut();
    testCutSpecialNets();
    testCutExternalConnections();
    testCutIOSpatialIndex();
    testCutRoutingFanout();
    testCutIOCapacity();
    testCutOriginalLayers();
    testCutMeanLayer();
    testCutMissingLayers();
    testCutWithoutTracks();
    testNetlist();
    testPGNetExclusion();
    testPGNetlist();
    testIOBoundaryNetExclusion();
    testDriverInference();
    dmInst->reset();
  }

 private:
  std::filesystem::path _data = std::filesystem::path(__FILE__).parent_path() / "data";
  std::filesystem::path _output = std::filesystem::temp_directory_path() / ("test_imj_design_tools_" + std::to_string(getpid()));

  void loadDEF()
  {
    dmInst->reset();
    require(dmInst->readLef(std::vector<std::string>{(_data / "tech.lef").string()}, true), "read tech");
    require(dmInst->readLef(std::vector<std::string>{(_data / "cell.lef").string()}), "read cell");
    require(dmInst->readDef((_data / "design.def.in").string()), "read DEF");
    // IDB currently reads group records but not their member callbacks.
    idb::IdbDesign* design = dmInst->get_idb_design();
    design->get_group_list()->get_group_list()[0]->add_instance(design->get_instance_list()->find_instance("u0"));
    design->get_group_list()->get_group_list()[0]->add_instance(design->get_instance_list()->find_instance("u1"));
    design->get_group_list()->get_group_list()[1]->add_instance(design->get_instance_list()->find_instance("outside"));
    design->get_group_list()->get_group_list()[1]->add_instance(design->get_instance_list()->find_instance("u0"));
  }

  void require(bool condition, const std::string& message)
  {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

  std::string readText(const std::filesystem::path& path)
  {
    std::ifstream stream(path);
    require(stream.good(), "missing " + path.string());
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  }

  void testDriverAnalysis()
  {
    MJI.analyzeDriver();
    std::string report = readText(_output / "driver_analyzer/driver_report.rpt");
    require(report.find("MultiDriverNets : 1") != std::string::npos, "multiple outputs");
    require(report.find("UnresolvedNets : 0") != std::string::npos, "sole inout inferred as driver");
    require(report.find("PORT bus driver (inferred from inout)") != std::string::npos, "inferred driver provenance");
    require(report.find("UndrivenNets : 2") != std::string::npos, "undriven net");
    require(report.find("ExcludedPGNets : 1") != std::string::npos, "PG exclusion");
    require(report.find("INSTANCE u0/Y driver") != std::string::npos, "driver identity");
    idb::IdbNet* net = dmInst->get_idb_design()->get_net_list()->find_net("multi");
    idb::IdbPin* pin = dmInst->get_idb_design()->get_instance_list()->find_instance("u0")->get_pin("Y");
    net->get_instance_pin_list()->get_pin_list().push_back(pin);
    MJI.analyzeDriver();
    report = readText(_output / "driver_analyzer/driver_report.rpt");
    require(report.find("Drivers : 2") != std::string::npos && report.find("Drivers : 3") == std::string::npos, "deduplicate pins");
    net->get_instance_pin_list()->get_pin_list().pop_back();
  }

  void testDEFCut()
  {
    idb::IdbDesign* source = dmInst->get_idb_design();
    idb::IdbPin* bus_pin = source->get_io_pin_list()->find_pin("bus");
    idb::IdbPlacementStatus status = bus_pin->get_term()->get_placement_status();
    bus_pin->get_term()->set_placement_status_unplace();
    imj::DCModel unplaced_model;
    MJI.wrapCutDesign(unplaced_model);
    for (imj::DCObject& object : unplaced_model.get_io_pin_list()) {
      if (object.get_name() == "bus") {
        require(object.get_rect_list().empty(), "unplaced IO treated as physical geometry");
      }
    }
    for (imj::DCNet& net : unplaced_model.get_net_list()) {
      if (net.get_name() == "bus") {
        require(net.get_connection_list().front().get_shape_list().empty(), "unplaced IO has placed shapes");
        require(net.get_connection_list().front().get_layer_name_set() == std::set<std::string>{"M2"}, "unplaced IO lost its DEF layer");
      }
    }
    bus_pin->get_term()->set_placement_status(status);
    idb::IdbLayout* source_layout = dmInst->get_idb_layout();
    idb::IdbCellMaster* cell_master = source_layout->get_cell_master_list()->find_cell_master("BUF");
    idb::IdbTerm* spare_term = source->get_io_pin_list()->find_pin("spare[0]")->get_term();
    spare_term->set_antenna_gate_area(0.25);
    spare_term->set_antenna_diff_area(0.5);
    spare_term->add_antenna_partial_metal_area("M1", 0.125);
    idb::IdbVia* pin_via = source->get_via_list()->find_via("VDEF")->clone();
    pin_via->set_coordinate(0, 0);
    spare_term->get_port_list().front()->add_via(pin_via);
    require(MJI.wrapCutPinLayerNameSet(source->get_io_pin_list()->find_pin("spare[0]")) == std::set<std::string>{"M1", "M2"},
            "pin-via routing layers were not wrapped");
    source->get_io_pin_list()->find_pin("spare[0]")->set_bounding_box();
    require(dmInst->saveDef((_output / "before.def").string()), "save before");
    MJI.cutDEF({{"-rect", std::string("2.5 2.5 12 12")}});
    require(dmInst->get_idb_design() == source, "active design address changed");
    require(source->get_design_name() == "crop_test_cut", "cropped design not published");
    require(dmInst->get_idb_builder()->get_def_service()->get_design() == source, "builder design is stale");
    require(dmInst->get_idb_layout() == source_layout && source->get_layout() == source_layout, "LEF layout changed");
    require(source_layout->get_cell_master_list()->find_cell_master("BUF") == cell_master, "LEF master changed");
    require(!std::filesystem::exists(_output / "def_cutter/cut.def"), "unexpected intermediate DEF");
    require(dmInst->saveDef((_output / "after.def").string()), "save after");
    require(readText(_output / "before.def") != readText(_output / "after.def"), "active design was not cropped");
    idb::IdbPin* spare_pin = source->get_io_pin_list()->find_pin("spare[0]");
    require(spare_pin->get_term() != spare_term, "retained IO borrows the old term");
    require(spare_pin->get_term()->get_antenna_gate_area() == 0.25 && spare_pin->get_term()->get_antenna_diff_area() == 0.5,
            "retained IO antenna data changed");
    require(spare_pin->get_term()->get_antenna_partial_metal_area().find("M1")->second == 0.125, "retained IO layer antenna data changed");
    require(spare_pin->get_term()->get_port_list().front()->get_via_list().size() == 1, "retained IO via lost");
    require(spare_pin->get_via_list().size() == 1, "retained IO via geometry not rebuilt");
    require(source->get_bus_list()->findBus("spare").has_value(), "IO bus not rebuilt");
    require(source->get_bus_list()->findBus("spare")->get().getPin(0) == spare_pin, "IO bus references stale pin");
    require(source->get_via_list()->get_num_via() == 1, "DEF via definitions lost");
    require(source->get_fill_list()->get_fill_list()[1]->get_via()->get_via()->get_instance() == source->get_via_list()->find_via("VDEF")->get_instance(),
            "fill via references stale master");
    require(spare_pin->get_term()->get_port_list().front()->get_via_list().front()->get_instance()
                == source->get_via_list()->find_via("VDEF")->get_instance(),
            "IO via references stale master");
    std::string report = readText(_output / "def_cutter/cut_report.rpt");
    require(report.find("RetainedInstances : 2/5") != std::string::npos, "partial/outside/unplaced instance");
    require(report.find("CutNet : a removed_pins=1") != std::string::npos, "boundary connectivity report");
    bool rejected = false;
    try {
      MJI.cutDEF({{"-rect", std::string("12 12 2 2")}});
    } catch (const std::runtime_error&) {
      rejected = true;
      imj::DEFCutter::destroyInst();
    }
    require(rejected, "invalid rectangle accepted");
    rejected = false;
    try {
      MJI.cutDEF({{"-rect", std::string("3.1 3.1 3.2 3.2")}, {"-boundary", std::string("core")}});
    } catch (const std::runtime_error&) {
      rejected = true;
      imj::DEFCutter::destroyInst();
    }
    require(rejected, "removed option accepted");
    require(dmInst->saveDef((_output / "rejected.def").string()), "save after rejected crop");
    require(readText(_output / "after.def") == readText(_output / "rejected.def"), "rejected crop mutated IDB");
    checkCut(2500, 2500, 12000, 12000);
    idb::IdbLayout* layout = dmInst->get_idb_layout();
    require(layout->get_rows()->get_row_num() == 9, "row count");
    for (idb::IdbRow* row : layout->get_rows()->get_row_list()) {
      require(row->get_original_coordinate()->get_x() == 3000 && row->get_row_num_x() == 9 && row->get_step_x() == 1000, "site alignment");
    }
    for (idb::IdbLayer* layer : layout->get_layers()->get_routing_layers()) {
      idb::IdbLayerRouting* routing_layer = dynamic_cast<idb::IdbLayerRouting*>(layer);
      require(routing_layer->get_track_grid_list().size() == 1, "routing layer track count");
      idb::IdbTrackGrid* track = routing_layer->get_track_grid_list().front();
      require(std::find(layout->get_track_grid_list()->get_track_grid_list().begin(), layout->get_track_grid_list()->get_track_grid_list().end(), track)
                  != layout->get_track_grid_list()->get_track_grid_list().end(),
              "routing layer references stale track");
      require(track->get_track()->get_start() == 2600 && track->get_track_num() == 48, "cropped track range");
    }
    imj::DCModel model;
    MJI.wrapCutDesign(model);
    for (idb::IdbPin* pin : source->get_io_pin_list()->get_pin_list()) {
      imj::DCRect bounding_box = MJI.wrapCutRect(pin->get_bounding_box());
      for (idb::IdbLayerShape* shape : pin->get_port_box_list()) {
        for (idb::IdbRect* rect : shape->get_rect_list()) {
          require(bounding_box.contains(MJI.wrapCutRect(rect)), "IO bounding box does not enclose its ports");
        }
      }
    }
    int32_t via_num = 0;
    bool has_flush_wire = false;
    bool has_clipped_via_top = false;
    for (imj::DCNet& net : model.get_net_list()) {
      for (imj::DCRoute& route : net.get_route_list()) {
        require(model.get_input_die().contains(route.get_rect()), "route outside crop");
        if (net.get_name() == "a" && route.get_rect().get_ll_x() == 5100 && route.get_rect().get_ll_y() == 6950 && route.get_rect().get_ur_x() == 6000
            && route.get_rect().get_ur_y() == 7050) {
          has_flush_wire = true;
        }
        if (net.get_name() == "multi" && route.get_layer_name() == "M2" && route.get_via_idx() < 0 && route.get_rect().get_ll_x() == 11915
            && route.get_rect().get_ur_x() == 12000) {
          has_clipped_via_top = true;
        }
        if (route.get_via_idx() >= 0) {
          via_num++;
        }
      }
    }
    require(has_flush_wire, "negative/zero wire end extensions changed");
    require(has_clipped_via_top, "clipped via must retain its upper routing enclosure on M2");
    require(via_num == 2, "retain complete signal/PG vias, exclude boundary via");
    require(dmInst->get_idb_design()->get_fill_list()->get_num_fill() == 2, "fill clipping");
    require(dmInst->get_idb_design()->get_instance_list()->find_instance("u1")->get_orient() == idb::IdbOrient::kFS_MX, "orientation changed");
    checkIOPins(model);
    require(dmInst->saveDef((_output / "roundtrip.def").string()), "save standalone DEF");
    MJI.analyzeDriver();
    report = readText(_output / "driver_analyzer/driver_report.rpt");
    require(report.find("MultiDriverNets : 1") != std::string::npos, "cropping must not introduce multiple drivers");
    require(report.find("UndrivenNets : 1") != std::string::npos, "external driver must be replaced with an input IO");
    dmInst->saveVerilog((_output / "cut.v").string());
    require(dmInst->addVerilog((_output / "cut.v").string(), "crop_test_cut"), "read standalone netlist");
    idb::IdbDesign* netlist = dmInst->get_netlist_idb_design();
    require(netlist->get_instance_list()->get_num() == 2, "standalone netlist instance count");
    require(netlist->get_io_pin_list()->find_pin("out")->get_term()->get_direction() == idb::IdbConnectDirection::kInput, "standalone input pin");
    require(netlist->get_io_pin_list()->find_pin("transit")->get_term()->get_direction() == idb::IdbConnectDirection::kInOut, "standalone inout pin");
    MJI.cutDEF({{"-rect", std::string("2.5 2.5 12 12")}});
    checkCut(2500, 2500, 12000, 12000);
    require(netlist == dmInst->get_netlist_idb_design() && netlist->get_design_name() == "crop_test_cut", "reference netlist changed");
    MJI.destroyMJ();
    checkCut(2500, 2500, 12000, 12000);
    require(dmInst->saveDef((_output / "destroyed.def").string()), "save after destroy_mj");
    dmInst->saveVerilog((_output / "destroyed.v").string());
    require(readText(_output / "destroyed.def").find("DESIGN crop_test_cut_cut ;") != std::string::npos, "DEF output after destroy_mj");
    require(readText(_output / "destroyed.v").find("module crop_test_cut_cut") != std::string::npos, "netlist output after destroy_mj");
    require(dmInst->readDef((_output / "roundtrip.def").string()), "read standalone DEF");
    imj::DCModel roundtrip_model;
    MJI.wrapCutDesign(roundtrip_model);
    checkIOPins(roundtrip_model);
  }

  void checkCut(int32_t ll_x, int32_t ll_y, int32_t ur_x, int32_t ur_y)
  {
    idb::IdbRect* die = dmInst->get_idb_layout()->get_die()->get_bounding_box();
    require(die->get_low_x() == ll_x && die->get_low_y() == ll_y && die->get_high_x() == ur_x && die->get_high_y() == ur_y, "die boundary");
    idb::IdbDesign* design = dmInst->get_idb_design();
    idb::IdbConnectivityCheckResult connectivity = design->validateConnectivity();
    require(connectivity.ok, connectivity.messages.empty() ? "cropped design connectivity is inconsistent" : connectivity.messages.front());
    require(design->get_instance_list()->get_num() == 2, "component count");
    require(design->get_io_pin_list()->get_pin_num() == 6, "preserved and generated IO pin count");
    require(design->get_net_list()->find_net("a")->get_instance_pin_list()->get_pin_num() == 1, "deleted instance reference");
    require(design->get_special_net_list()->find_net("VDD")->get_instance_pin_list()->get_pin_num() == 2, "PG connectivity");
    require(design->get_group_list()->get_num() == 2, "group references");
  }

  void checkIOPins(imj::DCModel& model)
  {
    idb::IdbDesign* design = dmInst->get_idb_design();
    for (idb::IdbPin* pin : design->get_io_pin_list()->get_pin_list()) {
      require(pin->get_net() != nullptr || pin->get_special_net() != nullptr, "unconnected IO pin");
      std::vector<imj::DCLayerRect> shapes = MJI.wrapCutPinShapeList(pin);
      require(!shapes.empty(), "IO without physical access");
      for (imj::DCLayerRect& shape : shapes) {
        require(model.get_input_die().contains(shape.get_rect()), "IO shape outside die");
      }
    }
    require(design->get_io_pin_list()->find_pin("multi")->get_term()->get_direction() == idb::IdbConnectDirection::kOutput, "internal driver produces OUTPUT");
    require(design->get_io_pin_list()->find_pin("out")->get_term()->get_direction() == idb::IdbConnectDirection::kInput, "external driver produces INPUT");
    require(design->get_io_pin_list()->find_pin("transit")->get_term()->get_direction() == idb::IdbConnectDirection::kInOut, "unknown transit direction");
    require(design->get_io_pin_list()->find_pin("VDD") == nullptr, "special net must not generate an IO");
    require(design->get_special_net_list()->find_net("VDD")->get_io_pin_list()->get_pin_num() == 0, "special net must not gain IO references");
    require(design->get_net_list()->find_net("transit")->get_io_pins()->get_pin_num() == 1, "one logical pin for two crossings");
    require(design->get_io_pin_list()->find_pin("transit")->get_term()->get_port_number() == 2, "both transit crossings have PORT shapes");
    std::set<std::tuple<std::string, int32_t, int32_t, int32_t, int32_t>> transit_shapes;
    for (imj::DCLayerRect& shape : MJI.wrapCutPinShapeList(design->get_io_pin_list()->find_pin("transit"))) {
      imj::DCRect& rect = shape.get_rect();
      transit_shapes.emplace(shape.get_layer_name(), rect.get_ll_x(), rect.get_ll_y(), rect.get_ur_x(), rect.get_ur_y());
    }
    require(transit_shapes
                == std::set<std::tuple<std::string, int32_t, int32_t, int32_t, int32_t>>{{"M2", 8950, 2500, 9050, 2600}, {"M2", 8950, 11900, 9050, 12000}},
            "transit IO access must match the retained wire at both die edges");
    require(design->get_net_list()->find_net("a")->get_io_pins()->get_pin_num() == 1, "reuse retained IO for removed loads on the same net");
    require(design->get_io_pin_list()->find_pin("a")->get_term()->get_direction() == idb::IdbConnectDirection::kInput, "retained input direction");
    require(design->get_io_pin_list()->find_pin("a")->get_term()->get_port_number() == 2, "retained input keeps its geometry and gains boundary access");
    require(design->get_net_list()->find_net("bus")->get_io_pins()->get_pin_num() == 1, "reuse retained INOUT rather than creating an alias");
    require(design->get_io_pin_list()->find_pin("bus")->get_term()->get_port_number() > 1, "retained inout gets boundary access");
    std::vector<imj::DCLayerRect> input_shapes = MJI.wrapCutPinShapeList(design->get_io_pin_list()->find_pin("out"));
    for (imj::DCLayerRect& shape : input_shapes) {
      checkBoundaryShape(shape, model.get_input_die());
    }
    require(design->get_net_list()->find_net("out")->get_io_pins()->get_pin_num() == 1, "all external connections share one IO");
    require(design->get_io_pin_list()->find_pin("o") == nullptr, "removed external top-level load must not add another IO");
  }

  void checkBoundaryShape(imj::DCLayerRect& shape, const imj::DCRect& die)
  {
    imj::DCRect& rect = shape.get_rect();
    require(die.contains(rect), "generated IO outside die");
    require(rect.get_ll_x() == die.get_ll_x() || rect.get_ur_x() == die.get_ur_x()
                || rect.get_ll_y() == die.get_ll_y() || rect.get_ur_y() == die.get_ur_y(),
            "generated IO is floating inside die");
  }

  void testCutSpecialNets()
  {
    loadDEF();
    idb::IdbDesign* design = dmInst->get_idb_design();
    idb::IdbPin* bus_pin = design->get_io_pin_list()->find_pin("bus");
    imj::DCRect bus_rect = MJI.wrapCutPinShapeList(bus_pin).front().get_rect();
    int32_t bus_port_num = bus_pin->get_term()->get_port_number();
    idb::IdbSpecialNet* bus = design->get_special_net_list()->add_net("bus");
    bus->set_connect_type(idb::IdbConnectType::kSignal);
    require(design->connectPinToSpecialNet(bus_pin, bus), "existing special IO connection");
    // This special record will not survive the crop, but still excludes its ordinary record.
    design->get_special_net_list()->add_net("out")->set_connect_type(idb::IdbConnectType::kSignal);
    idb::IdbSpecialNet* signal = design->get_special_net_list()->add_net("special_signal");
    signal->set_connect_type(idb::IdbConnectType::kSignal);
    idb::IdbSpecialWire* wire = signal->get_wire_list()->add_wire();
    wire->set_wire_state(idb::IdbWiringStatement::kRouted);
    idb::IdbSpecialWireSegment* segment = wire->add_segment();
    segment->set_layer(dmInst->get_idb_layout()->get_layers()->find_layer("M1"));
    segment->set_route_width(100);
    segment->add_flush_point(1000, 8000, 0);
    segment->add_flush_point(18000, 8000, 0);
    MJI.initMJ({{"-temp_directory_path", (_output / "special_nets").string()}});
    MJI.cutDEF({{"-rect", std::string("2.5 2.5 12 12")}});
    require(design->validateConnectivity().ok, "special net cut connectivity");
    require(design->get_io_pin_list()->get_pin_num() == 5, "only ordinary nets may gain IOs");
    for (std::string name : {"VDD", "out", "special_signal"}) {
      require(design->get_io_pin_list()->find_pin(name) == nullptr, "special net generated IO: " + name);
    }
    require(design->get_net_list()->find_net("out")->get_io_pins()->get_pin_num() == 0, "ordinary record bypassed special exclusion");
    require(design->get_special_net_list()->find_net("out") == nullptr, "empty special record should not survive crop");
    bus_pin = design->get_io_pin_list()->find_pin("bus");
    require(bus_pin != nullptr && bus_pin->get_term()->get_port_number() == bus_port_num, "retained special IO must not be extended");
    std::vector<imj::DCLayerRect> bus_shapes = MJI.wrapCutPinShapeList(bus_pin);
    require(bus_shapes.size() == 1 && bus_shapes.front().get_layer_name() == "M2", "retained special IO layer changed");
    imj::DCRect& rect = bus_shapes.front().get_rect();
    require(rect.get_ll_x() == bus_rect.get_ll_x() && rect.get_ll_y() == bus_rect.get_ll_y()
                && rect.get_ur_x() == bus_rect.get_ur_x() && rect.get_ur_y() == bus_rect.get_ur_y(),
            "retained special IO geometry changed");
    require(bus_pin->get_special_net() == design->get_special_net_list()->find_net("bus")
                && bus_pin->get_net() == design->get_net_list()->find_net("bus"),
            "retained IO lost ordinary/special references");
    require(design->get_special_net_list()->find_net("VDD")->get_instance_pin_list()->get_pin_num() == 2, "special net instance pins lost");
    require(design->get_special_net_list()->find_net("VDD")->get_wire_list()->get_num() > 0, "special net routing lost");
    imj::DCModel model;
    MJI.wrapCutDesign(model);
    bool has_clipped_signal_route = false;
    for (imj::DCNet& net : model.get_net_list()) {
      if (net.get_name() == "special_signal") {
        require(net.get_is_special() && !net.get_is_pg(), "signal SPECIALNETS classification changed");
        require(net.get_route_list().size() == 1, "special signal routing lost");
        imj::DCRect& route = net.get_route_list().front().get_rect();
        has_clipped_signal_route = route.get_ll_x() == 2500 && route.get_ur_x() == 12000
                                   && route.get_ll_y() == 7950 && route.get_ur_y() == 8050;
      }
    }
    require(has_clipped_signal_route, "special signal route must remain clipped without creating IOs");
    require(design->get_net_list()->find_net("multi")->get_io_pins()->get_pin_num() == 1, "ordinary net must still gain its IO");
    std::string report = readText(_output / "special_nets/def_cutter/cut_report.rpt");
    for (std::string name : {"VDD", "bus", "out", "special_signal"}) {
      require(report.find("CutNet : " + name + " ") == std::string::npos, "excluded special net reported as generating IO");
    }
    MJI.destroyMJ();
  }

  void testCutExternalConnections()
  {
    dmInst->reset();
    require(dmInst->readLef(std::vector<std::string>{(_data / "tech.lef").string()}, true), "fanout tech");
    require(dmInst->readLef(std::vector<std::string>{(_data / "cell.lef").string()}), "fanout cells");
    require(dmInst->readDef((_data / "fanout.def.in").string()), "fanout design");
    MJI.initMJ({{"-temp_directory_path", (_output / "fanout").string()}});
    MJI.cutDEF({{"-rect", std::string("2.5 2.5 12 12")}});
    idb::IdbDesign* design = dmInst->get_idb_design();
    require(design->validateConnectivity().ok, "fanout connectivity");
    require(design->get_instance_list()->get_num() == 2, "retain only A and E");
    idb::IdbNet* fanout = design->get_net_list()->find_net("fanout");
    require(fanout->get_instance_pin_list()->get_pin_num() == 1, "retain A/Y");
    require(fanout->get_io_pins()->get_pin_num() == 1, "A to external B/C/D must have one IO");
    for (idb::IdbPin* pin : fanout->get_io_pins()->get_pin_list()) {
      require(pin->get_net() == fanout && pin->get_term()->get_direction() == idb::IdbConnectDirection::kOutput, "external loads share one OUTPUT");
    }
    idb::IdbNet* mixed = design->get_net_list()->find_net("mixed");
    require(mixed->get_io_pins()->get_pin_num() == 1, "mixed external driver/load count");
    int32_t input_num = 0;
    int32_t output_num = 0;
    for (idb::IdbPin* pin : mixed->get_io_pins()->get_pin_list()) {
      input_num += pin->get_term()->get_direction() == idb::IdbConnectDirection::kInput;
      output_num += pin->get_term()->get_direction() == idb::IdbConnectDirection::kOutput;
    }
    require(input_num == 1 && output_num == 0, "external driver determines the consolidated IO direction");
    require(design->get_net_list()->find_net("internal")->get_io_pins()->get_pin_num() == 0, "internal connection must not become IO");
    require(design->get_special_net_list()->find_net("VDD")->get_io_pin_list()->get_pin_num() == 0, "special net must not generate IOs");
    std::string report = readText(_output / "fanout/def_cutter/cut_report.rpt");
    require(report.find("CutNet : fanout removed_pins=3 io_pins=1\n") != std::string::npos, "fanout consolidation report");
    imj::DCRect die(2500, 2500, 12000, 12000);
    std::vector<imj::DCLayerRect> shape_list;
    for (idb::IdbPin* pin : design->get_io_pin_list()->get_pin_list()) {
      std::vector<imj::DCLayerRect> shapes = MJI.wrapCutPinShapeList(pin);
      require(shapes.size() == 1, "one edge shape for each unrouted IO");
      for (imj::DCLayerRect& shape : shapes) {
        require(shape.get_layer_name() == "M1", "IO changed the source terminal layer");
        checkBoundaryShape(shape, die);
        imj::DCRect& rect = shape.get_rect();
        idb::IdbLayerRouting* layer = dynamic_cast<idb::IdbLayerRouting*>(dmInst->get_idb_layout()->get_layers()->find_layer(shape.get_layer_name()));
        int32_t center = layer->is_horizontal() ? (rect.get_ll_y() + rect.get_ur_y()) / 2 : (rect.get_ll_x() + rect.get_ur_x()) / 2;
        require(center % 200 == 0, "IO not aligned to DEF track");
        require(layer->is_horizontal() ? (rect.get_ll_x() == die.get_ll_x() || rect.get_ur_x() == die.get_ur_x())
                                       : (rect.get_ll_y() == die.get_ll_y() || rect.get_ur_y() == die.get_ur_y()),
                "IO edge does not match routing direction");
        double area = static_cast<double>(rect.get_ur_x() - rect.get_ll_x()) * (rect.get_ur_y() - rect.get_ll_y());
        require(area >= layer->get_area(), "IO violates layer minimum area");
        for (imj::DCLayerRect& previous : shape_list) {
          if (previous.get_layer_name() != shape.get_layer_name()) {
            continue;
          }
          imj::DCRect& other = previous.get_rect();
          int32_t x_gap = std::max(rect.get_ll_x() - other.get_ur_x(), other.get_ll_x() - rect.get_ur_x());
          int32_t y_gap = std::max(rect.get_ll_y() - other.get_ur_y(), other.get_ll_y() - rect.get_ur_y());
          require(x_gap >= 100 || y_gap >= 100, "IOs overlap or violate spacing");
        }
        shape_list.push_back(shape);
      }
    }
    MJI.analyzeDriver();
    report = readText(_output / "fanout/driver_analyzer/driver_report.rpt");
    require(report.find("MultiDriverNets : 0\n") != std::string::npos && report.find("UndrivenNets : 0\n") != std::string::npos,
            "IO splitting changed driver roles");
    dmInst->saveVerilog((_output / "fanout.v").string());
    require(dmInst->addVerilog((_output / "fanout.v").string(), "fanout_test_cut"), "read multi-port netlist");
    idb::IdbDesign* netlist = dmInst->get_netlist_idb_design();
    idb::IdbNet* output_net = netlist->get_instance_list()->find_instance("A")->get_pin("Y")->get_net();
    require(output_net->get_io_pins()->get_pin_num() == 1, "netlist changed the consolidated IO");
    MJI.cutDEF({{"-rect", std::string("2.5 2.5 12 12")}});
    require(design->get_io_pin_list()->get_pin_num() == 2, "repeated cut changed boundary IO count");
    require(design->validateConnectivity().ok, "repeated fanout cut connectivity");
    require(dmInst->saveDef((_output / "fanout.def").string()), "save multi-port DEF");
    require(dmInst->readDef((_output / "fanout.def").string()), "read multi-port DEF");
    require(dmInst->get_idb_design()->get_net_list()->find_net("fanout")->get_io_pins()->get_pin_num() == 1, "DEF roundtrip changed the consolidated IO");
    MJI.destroyMJ();
  }

  void testCutIOSpatialIndex()
  {
    for (int32_t mode = 0; mode < 3; mode++) {
      loadDEF();
      require(dmInst->readDef((_data / "fanout.def.in").string()), "spatial index design");
      idb::IdbDesign* design = dmInst->get_idb_design();
      idb::IdbLayerRouting* layer = dynamic_cast<idb::IdbLayerRouting*>(dmInst->get_idb_layout()->get_layers()->find_layer("M1"));
      layer->get_spacing_table()->set_parallel(nullptr);
      if (mode == 1) {
        layer->get_spacing_list()->get_spacing_list().front()->set_min_spacing(400);
      } else if (mode == 2) {
        std::shared_ptr<idb::IdbParallelSpacingTable> table = std::make_shared<idb::IdbParallelSpacingTable>(3, 3);
        std::array<int32_t, 3> threshold_list = {0, 100, 800};
        for (int32_t idx = 0; idx < 3; idx++) {
          table->set_width(idx, threshold_list[idx]);
          table->set_parallel_length(idx, threshold_list[idx]);
          for (int32_t length_idx = 0; length_idx < 3; length_idx++) {
            table->set_spacing(idx, length_idx, 100);
          }
        }
        // The largest spacing is not at the end of either table dimension.
        table->set_spacing(1, 1, 400);
        layer->set_parallel_spacing_table(table);
      }
      design->get_instance_list()->find_instance("E")->set_coodinate(6000, 4100);
      std::vector<imj::DCRect> obstacle_list = {{2800, 4800, 3350, 5100}, {2900, 6700, 3700, 7100}, {9000, 3500, 9300, 7500}};
      idb::IdbRoutingBlockage* blockage = design->get_blockage_list()->add_blockage_routing("M1");
      blockage->set_layer(layer);
      for (imj::DCRect& rect : obstacle_list) {
        blockage->add_rect(rect.get_ll_x(), rect.get_ll_y(), rect.get_ur_x(), rect.get_ur_y());
      }
      MJI.initMJ({{"-temp_directory_path", (_output / ("spatial_index_" + std::to_string(mode))).string()}});
      MJI.cutDEF({{"-rect", std::string("2.5 2.5 12 12")}});
      require(design->validateConnectivity().ok, "spatial index connectivity");
      for (idb::IdbInstance* instance : design->get_instance_list()->get_instance_list()) {
        for (idb::IdbPin* pin : instance->get_pin_list()->get_pin_list()) {
          for (imj::DCLayerRect& shape : MJI.wrapCutPinShapeList(pin)) {
            require(shape.get_layer_name() == "M1", "unexpected fixture layer");
            obstacle_list.push_back(shape.get_rect());
          }
        }
      }
      // Exact edge slots for this fixture's M1/M2 tracks, width and minimum area.
      std::vector<imj::DCRect> slot_list;
      for (int32_t side = 0; side < 2; side++) {
        for (int32_t y = 2600; y <= 11800; y += 200) {
          slot_list.emplace_back(side == 0 ? 2500 : 11550, y - 50, side == 0 ? 3050 : 12000, y + 50);
        }
      }
      for (const std::string& net_name : {std::string("fanout"), std::string("mixed")}) {
        idb::IdbNet* net = design->get_net_list()->find_net(net_name);
        require(net->get_io_pins()->get_pin_num() == 1, "spatial index IO count");
        imj::DCRect target = MJI.wrapCutPinShapeList(net->get_instance_pin_list()->get_pin_list().front()).front().get_rect();
        int32_t best_idx = -1;
        double best_distance = DBL_MAX;
        for (int32_t idx = 0; idx < static_cast<int32_t>(slot_list.size()); idx++) {
          imj::DCRect& slot = slot_list[idx];
          bool available = true;
          for (imj::DCRect& obstacle : obstacle_list) {
            int32_t width = std::max(100, std::min(obstacle.get_ur_x() - obstacle.get_ll_x(), obstacle.get_ur_y() - obstacle.get_ll_y()));
            int32_t length = std::max(0, std::min(slot.get_ur_x(), obstacle.get_ur_x()) - std::max(slot.get_ll_x(), obstacle.get_ll_x()));
            int32_t spacing = layer->get_spacing(width, length);
            int32_t x_gap = std::max(slot.get_ll_x() - obstacle.get_ur_x(), obstacle.get_ll_x() - slot.get_ur_x());
            int32_t y_gap = std::max(slot.get_ll_y() - obstacle.get_ur_y(), obstacle.get_ll_y() - slot.get_ur_y());
            if (slot.intersect(obstacle).isValid() || (x_gap < spacing && y_gap < spacing)) {
              available = false;
              break;
            }
          }
          double distance = std::abs((slot.get_ll_x() + slot.get_ur_x()) / 2.0 - (target.get_ll_x() + target.get_ur_x()) / 2.0)
                            + std::abs((slot.get_ll_y() + slot.get_ur_y()) / 2.0 - (target.get_ll_y() + target.get_ur_y()) / 2.0);
          if (available && distance < best_distance) {
            best_idx = idx;
            best_distance = distance;
          }
        }
        require(best_idx >= 0, "brute-force fixture has no legal IO slot");
        imj::DCRect actual = MJI.wrapCutPinShapeList(net->get_io_pins()->get_pin_list().front()).front().get_rect();
        imj::DCRect& expected = slot_list[best_idx];
        require(actual.get_ll_x() == expected.get_ll_x() && actual.get_ll_y() == expected.get_ll_y()
                    && actual.get_ur_x() == expected.get_ur_x() && actual.get_ur_y() == expected.get_ur_y(),
                "spatial query changed exhaustive placement result for mode " + std::to_string(mode) + ", net " + net_name);
        obstacle_list.push_back(expected);
      }
      MJI.destroyMJ();
    }
    loadDEF();
  }

  void testCutRoutingFanout()
  {
    require(dmInst->readDef((_data / "fanout.def.in").string()), "routed fanout design");
    idb::IdbDesign* design = dmInst->get_idb_design();
    idb::IdbRegularWire* wire = design->get_net_list()->find_net("fanout")->get_wire_list()->add_wire();
    wire->set_wire_state(idb::IdbWiringStatement::kRouted);
    for (imj::DCRect rect : {imj::DCRect(11000, 4000, 13000, 4100), imj::DCRect(11000, 4200, 13000, 4300), imj::DCRect(11000, 4000, 13000, 4300)}) {
      idb::IdbRegularWireSegment* segment = wire->add_segment();
      segment->set_layer(dmInst->get_idb_layout()->get_layers()->find_layer("M1"));
      segment->set_is_rect(true);
      segment->add_point(rect.get_ll_x(), rect.get_ll_y());
      segment->set_delta_rect(0, 0, rect.get_ur_x() - rect.get_ll_x(), rect.get_ur_y() - rect.get_ll_y());
    }
    idb::IdbRegularWireSegment* upper_segment = wire->add_segment();
    upper_segment->set_layer(dmInst->get_idb_layout()->get_layers()->find_layer("M2"));
    upper_segment->set_is_rect(true);
    upper_segment->add_point(8000, 11000);
    upper_segment->set_delta_rect(0, 0, 100, 2000);
    MJI.initMJ({{"-temp_directory_path", (_output / "routed_fanout").string()}});
    MJI.cutDEF({{"-rect", std::string("2.5 2.5 12 12")}});
    idb::IdbNet* fanout = design->get_net_list()->find_net("fanout");
    std::vector<idb::IdbPin*>& pins = fanout->get_io_pins()->get_pin_list();
    require(pins.size() == 1, "routed fanout must use one IO with multiple PORT shapes");
    bool has_cut_shape = false;
    for (int32_t idx = 0; idx < static_cast<int32_t>(pins.size()); idx++) {
      for (imj::DCLayerRect& shape : MJI.wrapCutPinShapeList(pins[idx])) {
        require(shape.get_layer_name() == "M1", "routing cut changed an M1 terminal to an M2 IO");
        checkBoundaryShape(shape, imj::DCRect(2500, 2500, 12000, 12000));
        has_cut_shape = has_cut_shape || (shape.get_layer_name() == "M1" && shape.get_rect().get_ll_x() == 11700
                                          && shape.get_rect().get_ll_y() == 4000 && shape.get_rect().get_ur_y() == 4300);
        for (int32_t other_idx = idx + 1; other_idx < static_cast<int32_t>(pins.size()); other_idx++) {
          for (imj::DCLayerRect& other : MJI.wrapCutPinShapeList(pins[other_idx])) {
            require(shape.get_layer_name() != other.get_layer_name() || !shape.get_rect().intersect(other.get_rect()).isValid(),
                    "overlapping route cuts must not produce overlapping logical IO pins");
          }
        }
      }
    }
    require(has_cut_shape, "existing routing cut lost");
    imj::DCModel result;
    MJI.wrapCutDesign(result);
    bool has_upper_route = false;
    for (imj::DCNet& net : result.get_net_list()) {
      if (net.get_name() == "fanout") {
        for (imj::DCRoute& route : net.get_route_list()) {
          has_upper_route = has_upper_route || (route.get_layer_name() == "M2" && route.get_rect().get_ll_x() == 8000
                                               && route.get_rect().get_ur_y() == 12000);
        }
      }
    }
    require(has_upper_route, "incompatible IO layer must not remove retained routing metal");
    require(design->validateConnectivity().ok, "routed fanout connectivity");
    MJI.destroyMJ();
  }

  void testCutIOCapacity()
  {
    require(dmInst->readDef((_data / "fanout.def.in").string()), "capacity design");
    idb::IdbDesign* design = dmInst->get_idb_design();
    idb::IdbLayer* layer = dmInst->get_idb_layout()->get_layers()->find_layer("M1");
    idb::IdbRoutingBlockage* blockage = design->get_blockage_list()->add_blockage_routing(layer->get_name());
    blockage->set_layer(layer);
    blockage->add_rect(2500, 2500, 12000, 12000);
    require(dmInst->saveDef((_output / "capacity_before.def").string()), "save blocked design");
    MJI.initMJ({{"-temp_directory_path", (_output / "capacity").string()}});
    bool rejected = false;
    try {
      MJI.cutDEF({{"-rect", std::string("2.5 2.5 12 12")}});
    } catch (const std::runtime_error& error) {
      require(std::string(error.what()).find("selected routing layer: M1") != std::string::npos, "capacity error must name the selected layer");
      rejected = true;
      imj::DEFCutter::destroyInst();
    }
    require(rejected, "blocked M1 must not fall back to available M2 slots");
    require(dmInst->get_idb_design() == design && design->get_design_name() == "fanout_test", "capacity failure replaced active IDB");
    require(dmInst->saveDef((_output / "capacity_after.def").string()), "save after capacity failure");
    require(readText(_output / "capacity_before.def") == readText(_output / "capacity_after.def"), "capacity failure changed IDB");
    MJI.destroyMJ();
  }

  void testCutOriginalLayers()
  {
    require(dmInst->readDef((_data / "fanout.def.in").string()), "source-layer design");
    idb::IdbDesign* design = dmInst->get_idb_design();
    idb::IdbLayer* upper = dmInst->get_idb_layout()->get_layers()->find_layer("M2");
    idb::IdbInstance* external = design->get_instance_list()->find_instance("B");
    idb::IdbTerm* upper_term = external->get_pin("A")->set_term();
    upper_term->set_name("A");
    upper_term->set_direction(idb::IdbConnectDirection::kInput);
    upper_term->set_type(idb::IdbConnectType::kSignal);
    idb::IdbLayerShape* shape = upper_term->add_port()->add_layer_shape();
    shape->set_layer(upper);
    shape->set_type_rect();
    shape->add_rect(0, 400, 100, 600);
    external->set_status(idb::IdbPlacementStatus::kUnplaced);
    require(MJI.wrapCutPinShapeList(external->get_pin("A")).empty(), "source must be unplaced");
    require(MJI.wrapCutPinLayerNameSet(external->get_pin("A")) == std::set<std::string>{"M2"}, "unplaced instance lost its terminal layer");
    idb::IdbPin* via_pin = design->get_instance_list()->find_instance("C")->get_pin("A");
    idb::IdbTerm* via_term = via_pin->set_term();
    via_term->set_name("A");
    via_term->set_direction(idb::IdbConnectDirection::kInput);
    via_term->set_type(idb::IdbConnectType::kSignal);
    via_term->add_port()->add_via(dmInst->get_idb_layout()->get_via_list()->find_via("V12")->clone());
    require(MJI.wrapCutPinLayerNameSet(via_pin) == std::set<std::string>{"M1", "M2"}, "via-only terminal lost its routing layers");
    via_pin->set_bounding_box();
    MJI.initMJ({{"-temp_directory_path", (_output / "source_layers").string()}});
    MJI.cutDEF({{"-rect", std::string("2.5 2.5 12 12")}});
    require(design->get_net_list()->find_net("fanout")->get_io_pins()->get_pin_num() == 1, "layer averaging changed IO count");
    std::vector<imj::DCLayerRect> shapes = MJI.wrapCutPinShapeList(design->get_io_pin_list()->find_pin("fanout"));
    require(!shapes.empty(), "missing mean-layer IO geometry");
    for (imj::DCLayerRect& io_shape : shapes) {
      // A/Y=M1, B/A=M2, C/A=(M1+M2)/2, D/A=M1: mean=1.375 -> M1.
      require(io_shape.get_layer_name() == "M1", "IO layer must average all pins, including retained and unplaced terminals");
      checkBoundaryShape(io_shape, imj::DCRect(2500, 2500, 12000, 12000));
    }
    require(design->validateConnectivity().ok, "source-layer connectivity");
    MJI.destroyMJ();
  }

  void testCutMeanLayer()
  {
    idb::IdbLayers* layers = dmInst->get_idb_layout()->get_layers();
    layers->add_cut_layer(layers->set_layer("UPPER_CUT", "CUT"));
    layers->add_routing_layer(layers->set_layer("TOP_METAL", "ROUTING"));
    std::vector<std::string> instance_names = {"B", "C", "D"};
    for (auto [upper_pin_num, layer_name, expected] : std::vector<std::tuple<int32_t, std::string, std::string>>{
             {1, "M2", "M1"}, {2, "M2", "M2"}, {3, "M2", "M2"}, {2, "TOP_METAL", "M2"}}) {
      require(dmInst->readDef((_data / "fanout.def.in").string()), "mean-layer design");
      idb::IdbDesign* design = dmInst->get_idb_design();
      idb::IdbNet* net = design->get_net_list()->find_net("fanout");
      for (int32_t idx = 0; idx < upper_pin_num; idx++) {
        idb::IdbPin* pin = design->get_instance_list()->find_instance(instance_names[idx])->get_pin("A");
        idb::IdbTerm* term = pin->set_term();
        term->set_name("A");
        term->set_direction(idb::IdbConnectDirection::kInput);
        term->set_type(idb::IdbConnectType::kSignal);
        idb::IdbLayerShape* shape = term->add_port()->add_layer_shape();
        shape->set_layer(layers->find_layer(layer_name));
        shape->set_type_rect();
        for (int32_t rect_idx = 0; rect_idx < 10; rect_idx++) {
          shape->add_rect(0, 400, 100, 600);
        }
        pin->set_bounding_box();
        // Duplicate records and extra rectangles must not give a pin more weight.
        net->get_instance_pin_list()->get_pin_list().push_back(pin);
        net->get_instance_pin_list()->get_pin_list().push_back(pin);
      }
      MJI.initMJ({{"-temp_directory_path", (_output / "mean_layer").string()}});
      MJI.cutDEF({{"-rect", std::string("2.5 2.5 12 12")}});
      idb::IdbPin* io = design->get_io_pin_list()->find_pin("fanout");
      require(design->get_net_list()->find_net("fanout")->get_io_pins()->get_pin_num() == 1, "mean layer produced extra IOs");
      std::vector<imj::DCLayerRect> shapes = MJI.wrapCutPinShapeList(io);
      require(!shapes.empty(), "mean-layer IO has no geometry");
      for (imj::DCLayerRect& shape : shapes) {
        require(shape.get_layer_name() == expected, "incorrect mean metal layer or half-up rounding");
        require(layers->find_layer(shape.get_layer_name())->is_routing(), "IO must not use a CUT layer");
      }
      require(design->validateConnectivity().ok, "mean-layer connectivity");
      MJI.destroyMJ();
    }
  }

  void testCutMissingLayers()
  {
    require(dmInst->readDef((_data / "fanout.def.in").string()), "missing-layer design");
    idb::IdbDesign* design = dmInst->get_idb_design();
    idb::IdbTerm* term = design->get_instance_list()->find_instance("B")->get_pin("A")->set_term();
    term->set_name("A");
    term->set_direction(idb::IdbConnectDirection::kInput);
    MJI.initMJ({{"-temp_directory_path", (_output / "missing_layers").string()}});
    bool rejected = false;
    try {
      MJI.cutDEF({{"-rect", std::string("2.5 2.5 12 12")}});
    } catch (const std::runtime_error& error) {
      require(std::string(error.what()).find("Missing original routing layers") != std::string::npos, "missing-layer diagnostic");
      rejected = true;
      imj::DEFCutter::destroyInst();
    }
    require(rejected && design->get_design_name() == "fanout_test", "missing layers must not fabricate an IO layer or publish IDB");
    MJI.destroyMJ();
  }

  void testCutWithoutTracks()
  {
    require(dmInst->readDef((_data / "fanout.def.in").string()), "no-track design");
    idb::IdbLayout* layout = dmInst->get_idb_layout();
    for (idb::IdbLayer* layer : layout->get_layers()->get_routing_layers()) {
      dynamic_cast<idb::IdbLayerRouting*>(layer)->get_track_grid_list().clear();
    }
    layout->get_track_grid_list()->reset();
    MJI.initMJ({{"-temp_directory_path", (_output / "no_tracks").string()}});
    MJI.cutDEF({{"-rect", std::string("2.5 2.5 12 12")}});
    idb::IdbDesign* design = dmInst->get_idb_design();
    require(design->get_io_pin_list()->get_pin_num() == 2, "LEF grid fallback IO count");
    require(design->validateConnectivity().ok, "LEF grid fallback connectivity");
    for (idb::IdbPin* pin : design->get_io_pin_list()->get_pin_list()) {
      for (imj::DCLayerRect& shape : MJI.wrapCutPinShapeList(pin)) {
        checkBoundaryShape(shape, imj::DCRect(2500, 2500, 12000, 12000));
        imj::DCRect& rect = shape.get_rect();
        idb::IdbLayerRouting* layer = dynamic_cast<idb::IdbLayerRouting*>(layout->get_layers()->find_layer(shape.get_layer_name()));
        int32_t coord = layer->is_horizontal() ? (rect.get_ll_y() + rect.get_ur_y()) / 2 : (rect.get_ll_x() + rect.get_ur_x()) / 2;
        require((coord - std::max(layer->get_offset_prefer(), 0)) % layer->get_pitch_prefer() == 0, "IO not aligned to LEF fallback grid");
      }
    }
    MJI.destroyMJ();
  }

  void testNetlist()
  {
    dmInst->reset();
    require(dmInst->readLef(std::vector<std::string>{(_data / "tech.lef").string()}, true), "read tech netlist");
    require(dmInst->readLef(std::vector<std::string>{(_data / "cell.lef").string()}), "read cell netlist");
    require(dmInst->readVerilog((_data / "design.v").string(), "driver_test"), "read netlist");
    MJI.initMJ({{"-temp_directory_path", (_output / "netlist").string()}});
    MJI.analyzeDriver();
    std::string report = readText(_output / "netlist/driver_analyzer/driver_report.rpt");
    require(report.find("MultiDriverNets : 1") != std::string::npos, "netlist multi driver");
    MJI.destroyMJ();
    require(dmInst->readDef((_data / "design.def.in").string()), "read active DEF");
    require(dmInst->addVerilog((_data / "design.v").string(), "driver_test"), "read reference netlist");
    MJI.initMJ({{"-temp_directory_path", (_output / "active_design").string()}});
    MJI.analyzeDriver();
    report = readText(_output / "active_design/driver_analyzer/driver_report.rpt");
    require(report.find("Design : crop_test\n") != std::string::npos && report.find("Design : driver_test\n") == std::string::npos,
            "driver analysis must use the active design, not the LVS reference");
    require(report.find("MultiDriverNets : 1") != std::string::npos && report.find("UnresolvedNets : 0") != std::string::npos,
            "active DEF driver classification");
    MJI.destroyMJ();
  }

  void testPGNetExclusion()
  {
    loadDEF();
    MJI.initMJ({{"-temp_directory_path", (_output / "pg_exclusion").string()}});
    idb::IdbDesign* design = dmInst->get_idb_design();
    idb::IdbPin* first = addInferencePin("first_supply", idb::IdbConnectDirection::kInput);
    idb::IdbPin* second = addInferencePin("second_supply", idb::IdbConnectDirection::kInput);
    // The explicit PG record is visited after its ordinary same-name record.
    addInferenceNet("VDD", {first, second});
    addInferenceNet("typed_power", {first, second});
    design->get_net_list()->find_net("typed_power")->set_connect_type(idb::IdbConnectType::kPower);
    addInferenceNet("typed_ground", {first, second});
    design->get_net_list()->find_net("typed_ground")->set_connect_type(idb::IdbConnectType::kGround);
    idb::IdbPin* supply_port = addInferencePin("supply_port", idb::IdbConnectDirection::kInOut);
    addInferenceNet("lef_supply", {supply_port},
                    {design->get_instance_list()->find_instance("u0")->get_pin("VPWR"), design->get_instance_list()->find_instance("u1")->get_pin("VPWR")});
    idb::IdbPin* ground_port = addInferencePin("ground_port", idb::IdbConnectDirection::kInOut);
    ground_port->get_term()->set_type(idb::IdbConnectType::kGround);
    addInferenceNet("io_supply", {ground_port, first, second});
    idb::IdbSpecialNet* wildcard = design->get_special_net_list()->add_net("wildcard_supply");
    wildcard->set_connect_type(idb::IdbConnectType::kNone);
    wildcard->add_pin_string("VPWR");
    // Also cover a PG ordinary record followed by a signal special record.
    idb::IdbSpecialNet* duplicate = design->get_special_net_list()->add_net("typed_power");
    duplicate->set_connect_type(idb::IdbConnectType::kSignal);
    duplicate->get_io_pins()->add_pin_ref_unique(first);
    duplicate->get_io_pins()->add_pin_ref_unique(second);
    addInferenceNet("VSS", {addInferencePin("VSS", idb::IdbConnectDirection::kInOut)});
    idb::IdbSpecialNet* signal = design->get_special_net_list()->add_net("special_signal");
    signal->set_connect_type(idb::IdbConnectType::kSignal);
    signal->get_io_pins()->add_pin_ref_unique(addInferencePin("special_signal", idb::IdbConnectDirection::kInOut));

    imj::DAModel model;
    MJI.wrapDriverDesign(model);
    std::set<std::string> net_name_set;
    for (imj::DANet& net : model.get_net_list()) {
      net_name_set.insert(net.get_name());
    }
    require(net_name_set == std::set<std::string>{"multi", "a", "out", "bus", "spare[0]", "transit", "VSS", "special_signal"},
            "PG nets must not reach the graph, while signal special nets and signal VSS remain");
    require(model.get_excluded_pg_net_num() == 6, "count each logical PG net once across ordinary/special records");
    MJI.analyzeDriver();
    std::string report = readText(_output / "pg_exclusion/driver_analyzer/driver_report.rpt");
    require(report.find("SignalNets : 8\n") != std::string::npos && report.find("ExcludedPGNets : 6\n") != std::string::npos, "PG report counts");
    require(report.find("MultiDriverNets : 1\n") != std::string::npos && report.find("UnresolvedNets : 0\n") != std::string::npos
                && report.find("UndrivenNets : 2\n") != std::string::npos && report.find("InferredPins : 3\n") != std::string::npos,
            "PG pins must not contribute inferred roles or driver findings");
    for (std::string name : {"VDD", "typed_power", "typed_ground", "lef_supply", "io_supply", "wildcard_supply"}) {
      require(report.find("Net : " + name + "\n") == std::string::npos, "excluded PG net in report: " + name);
    }
    require(design->get_net_list()->find_net("lef_supply")->get_connect_type() == idb::IdbConnectType::kSignal
                && supply_port->get_term()->get_direction() == idb::IdbConnectDirection::kInOut && wildcard->get_instance_pin_list()->get_pin_list().empty(),
            "PG classification must not mutate IDB types, directions or wildcard connectivity");
    MJI.destroyMJ();
  }

  void testPGNetlist()
  {
    dmInst->reset();
    require(dmInst->readLef(std::vector<std::string>{(_data / "tech.lef").string()}, true), "read PG netlist tech");
    require(dmInst->readLef(std::vector<std::string>{(_data / "cell.lef").string()}), "read PG netlist cell");
    require(dmInst->readVerilog((_data / "power.v").string(), "power_test"), "read PG netlist");
    MJI.initMJ({{"-temp_directory_path", (_output / "pg_netlist").string()}});
    idb::IdbDesign* design = dmInst->get_idb_design();
    require(design->get_net_list()->find_net("rail")->get_connect_type() == idb::IdbConnectType::kSignal
                && design->get_instance_list()->find_instance("u0")->get_pin("VPWR")->get_term()->get_type() == idb::IdbConnectType::kPower,
            "ordinary Verilog supply wire derives PG evidence from LEF terminals");
    imj::DAModel model;
    MJI.wrapDriverDesign(model);
    std::set<std::string> net_name_set;
    for (imj::DANet& net : model.get_net_list()) {
      net_name_set.insert(net.get_name());
    }
    require(net_name_set == std::set<std::string>{"a", "VDD", "VSS", "y0", "y1", "y2"}, "exclude supply0/supply1/LEF PG, not signal names");
    require(model.get_excluded_pg_net_num() == 3, "Verilog PG net count");
    MJI.analyzeDriver();
    std::string report = readText(_output / "pg_netlist/driver_analyzer/driver_report.rpt");
    require(report.find("MultiDriverNets : 2\n") != std::string::npos && report.find("UnresolvedNets : 0\n") != std::string::npos
                && report.find("UndrivenNets : 0\n") != std::string::npos && report.find("InferredPins : 1\n") != std::string::npos,
            "only signal connectivity participates in Verilog driver inference");
    require(report.find("PORT VSS driver (inferred from inout)") != std::string::npos && report.find("Net : rail\n") == std::string::npos,
            "signal VSS is inferred but LEF supply rail is not");
    MJI.destroyMJ();
  }

  void testIOBoundaryNetExclusion()
  {
    dmInst->reset();
    require(dmInst->readLef(std::vector<std::string>{(_data / "tech.lef").string()}, true), "read IO boundary tech");
    require(dmInst->readLef(std::vector<std::string>{(_data / "cell.lef").string(), (_data / "pad.lef").string()}), "read IO boundary cells");
    require(dmInst->readVerilog((_data / "io_boundary.v").string(), "io_boundary_test"), "read IO boundary netlist");
    MJI.initMJ({{"-temp_directory_path", (_output / "io_boundary").string()}});
    idb::IdbDesign* design = dmInst->get_idb_design();
    idb::IdbNet* clock_net = design->get_net_list()->find_net("clock_pin");
    std::vector<idb::IdbPin*> clock_pin_list = clock_net->get_instance_pin_list()->get_pin_list();
    require(design->get_instance_list()->find_instance("pad0")->get_cell_master()->is_pad(), "PAD classification from LEF");
    MJI.analyzeDriver();
    std::string report = readText(_output / "io_boundary/driver_analyzer/driver_report.rpt");
    require(report.find("SignalNets : 5\n") != std::string::npos && report.find("ExcludedPGNets : 1\n") != std::string::npos
                && report.find("ExcludedIOBoundaryNets : 3\n") != std::string::npos,
            "exclude input/output/inout package boundaries independently of PG nets");
    require(report.find("MultiDriverNets : 2\n") != std::string::npos && report.find("InferredPins : 1\n") != std::string::npos
                && report.find("UnresolvedNets : 0\n") != std::string::npos,
            "only direct core IO is inferred; core-side multiple drivers remain");
    for (std::string name : {"clock_pin", "data_pin", "gpio_pin"}) {
      require(report.find("Net : " + name + "\n") == std::string::npos, "package boundary in graph/report: " + name);
    }
    require(report.find("Net : driven\nStatus : MULTI_DRIVER\n") != std::string::npos
                && report.find("Net : from_gpio\nStatus : MULTI_DRIVER\n") != std::string::npos
                && report.find("PORT direct_pin driver (inferred from inout)") != std::string::npos,
            "keep core-to-pad, pad-to-core and direct core IO connections");
    require(clock_net->get_instance_pin_list()->get_pin_list() == clock_pin_list
                && design->get_instance_list()->find_instance("pad2")->get_pin("BOND")->get_term()->get_direction() == idb::IdbConnectDirection::kInOut,
            "boundary exclusion must not alter IDB connectivity or pin directions");
    MJI.analyzeDriver();
    require(readText(_output / "io_boundary/driver_analyzer/driver_report.rpt") == report, "stable boundary exclusion");

    // Classify the merged, deduplicated logical connection, not each DEF record separately.
    idb::IdbSpecialNet* split = design->get_special_net_list()->add_net("clock_pin");
    split->set_connect_type(idb::IdbConnectType::kSignal);
    split->get_instance_pin_list()->get_pin_list() = {clock_pin_list[0], clock_pin_list[0]};
    clock_net->get_instance_pin_list()->get_pin_list().clear();
    clock_net->get_io_pins()->get_pin_list().push_back(clock_net->get_io_pins()->get_pin_list()[0]);
    MJI.analyzeDriver();
    require(readText(_output / "io_boundary/driver_analyzer/driver_report.rpt") == report, "split/duplicate boundary records");

    idb::IdbSpecialNet* mixed = design->get_special_net_list()->add_net("gpio_pin");
    mixed->set_connect_type(idb::IdbConnectType::kSignal);
    mixed->get_instance_pin_list()->add_pin_ref_unique(design->get_instance_list()->find_instance("u0")->get_pin("Y"));
    design->get_net_list()
        ->find_net("data_pin")
        ->get_instance_pin_list()
        ->add_pin_ref_unique(design->get_instance_list()->find_instance("pad1")->get_pin("DRIVE"));
    clock_net->get_io_pins()->add_pin_ref_unique(addInferencePin("second_clock_port", idb::IdbConnectDirection::kInput));
    MJI.analyzeDriver();
    report = readText(_output / "io_boundary/driver_analyzer/driver_report.rpt");
    require(report.find("SignalNets : 8\n") != std::string::npos && report.find("ExcludedIOBoundaryNets : 0\n") != std::string::npos,
            "additional core pin, second PAD terminal or second IO prevents boundary exclusion");
    require(report.find("Net : gpio_pin\nStatus : SINGLE_DRIVER\n") != std::string::npos
                && report.find("Net : data_pin\nStatus : SINGLE_DRIVER\n") != std::string::npos
                && report.find("Net : clock_pin\nStatus : MULTI_DRIVER\n") != std::string::npos,
            "analyze retained mixed connections including known multiple drivers");
    MJI.destroyMJ();
  }

  void testDriverInference()
  {
    loadDEF();
    MJI.initMJ({{"-temp_directory_path", (_output / "inference").string()}});
    idb::IdbDesign* design = dmInst->get_idb_design();
    idb::IdbPin* driver = addInferencePin("known_driver", idb::IdbConnectDirection::kInput);
    idb::IdbPin* other_driver = addInferencePin("other_driver", idb::IdbConnectDirection::kInput);
    idb::IdbPin* load = addInferencePin("known_load", idb::IdbConnectDirection::kOutput);
    idb::IdbPin* inout_load = addInferencePin("inout_load", idb::IdbConnectDirection::kInOut);
    idb::IdbPin* unknown_load = addInferencePin("unknown_load", idb::IdbConnectDirection::kNone);
    addInferenceNet("infer_load", {driver, inout_load, unknown_load, inout_load});
    addInferenceNet("infer_driver", {addInferencePin("inout_driver", idb::IdbConnectDirection::kInOut), load});
    addInferenceNet("infer_unknown", {addInferencePin("unknown_driver", idb::IdbConnectDirection::kNone), load});
    addInferenceNet("ambiguous",
                    {addInferencePin("ambiguous_a", idb::IdbConnectDirection::kInOut), addInferencePin("ambiguous_b", idb::IdbConnectDirection::kInOut), load});
    addInferenceNet("known_multi", {driver, other_driver, addInferencePin("multi_inout", idb::IdbConnectDirection::kInOut)});

    // Shared pin nodes exercise propagation back to nets already visited in the work queue.
    idb::IdbPin* chain_a = addInferencePin("chain_a", idb::IdbConnectDirection::kInOut);
    idb::IdbPin* chain_b = addInferencePin("chain_b", idb::IdbConnectDirection::kInOut);
    idb::IdbPin* chain_c = addInferencePin("chain_c", idb::IdbConnectDirection::kInOut);
    addInferenceNet("chain_first", {chain_a, chain_b});
    addInferenceNet("chain_second", {chain_b, chain_c});
    addInferenceNet("chain_seed", {driver, chain_c});
    idb::IdbPin* cycle_a = addInferencePin("cycle_a", idb::IdbConnectDirection::kInOut);
    idb::IdbPin* cycle_b = addInferencePin("cycle_b", idb::IdbConnectDirection::kInOut);
    idb::IdbPin* cycle_c = addInferencePin("cycle_c", idb::IdbConnectDirection::kInOut);
    idb::IdbPin* cycle_d = addInferencePin("cycle_d", idb::IdbConnectDirection::kInOut);
    addInferenceNet("cycle_first", {cycle_a, cycle_b});
    addInferenceNet("cycle_second", {cycle_b, cycle_c});
    addInferenceNet("cycle_third", {cycle_c, cycle_d});
    addInferenceNet("cycle_fourth", {cycle_d, cycle_a});

    idb::IdbPin* instance_driver = design->get_instance_list()->find_instance("partial")->get_pin("A");
    idb::IdbPin* instance_load = design->get_instance_list()->find_instance("unplaced")->get_pin("A");
    for (idb::IdbPin* pin : {instance_driver, instance_load}) {
      pin->set_term()->set_direction(idb::IdbConnectDirection::kInOut);
      pin->get_term()->set_type(idb::IdbConnectType::kSignal);
    }
    addInferenceNet("instance_driver", {load}, {instance_driver});
    addInferenceNet("instance_load", {driver}, {instance_load});

    MJI.analyzeDriver();
    std::string report = readText(_output / "inference/driver_analyzer/driver_report.rpt");
    require(report.find("InferredPins : 10\n") != std::string::npos, "count distinct inferred graph nodes");
    require(report.find("MultiDriverNets : 2\n") != std::string::npos, "known multiple drivers remain visible");
    require(report.find("UnresolvedNets : 5\n") != std::string::npos, "ambiguous nets and cycle remain unresolved");
    require(report.find("UndrivenNets : 2\n") != std::string::npos, "known undriven nets remain visible");
    require(report.find("PORT inout_load load (inferred from inout)") != std::string::npos, "inout becomes load beside known driver");
    require(report.find("PORT unknown_load load (inferred from unknown)") != std::string::npos, "missing direction becomes load");
    require(report.find("PORT inout_driver driver (inferred from inout)") != std::string::npos, "sole inout becomes driver");
    require(report.find("PORT unknown_driver driver (inferred from unknown)") != std::string::npos, "sole unknown becomes driver");
    require(report.find("Net : known_multi\nStatus : MULTI_DRIVER\nDrivers : 2\nUnresolved : 1\n") != std::string::npos,
            "do not infer from an already violated single-driver constraint");
    require(report.find("Net : ambiguous\nStatus : UNRESOLVED\nDrivers : 0\nUnresolved : 2\n") != std::string::npos, "do not select an arbitrary driver");
    require(report.find("PORT chain_a load (inferred from inout)") != std::string::npos
                && report.find("PORT chain_b driver (inferred from inout)") != std::string::npos
                && report.find("PORT chain_c load (inferred from inout)") != std::string::npos,
            "iterate graph constraints to a fixed point");
    require(report.find("INSTANCE partial/A driver (inferred from inout)") != std::string::npos
                && report.find("INSTANCE unplaced/A load (inferred from inout)") != std::string::npos,
            "keep separate instance pins with the same port name");
    require(inout_load->get_term()->get_direction() == idb::IdbConnectDirection::kInOut
                && unknown_load->get_term()->get_direction() == idb::IdbConnectDirection::kNone
                && instance_driver->get_term()->get_direction() == idb::IdbConnectDirection::kInOut
                && instance_load->get_term()->get_direction() == idb::IdbConnectDirection::kInOut,
            "inference must not change IDB directions");
    MJI.analyzeDriver();
    require(readText(_output / "inference/driver_analyzer/driver_report.rpt") == report, "repeated analysis must be stable");

    std::reverse(design->get_net_list()->get_net_list().begin(), design->get_net_list()->get_net_list().end());
    for (idb::IdbNet* net : design->get_net_list()->get_net_list()) {
      std::reverse(net->get_io_pins()->get_pin_list().begin(), net->get_io_pins()->get_pin_list().end());
    }
    MJI.analyzeDriver();
    std::string reordered_report = readText(_output / "inference/driver_analyzer/driver_report.rpt");
    require(getReportNetMap(reordered_report) == getReportNetMap(report), "inference must not depend on net/pin traversal order");
    MJI.destroyMJ();
  }

  idb::IdbPin* addInferencePin(std::string name, idb::IdbConnectDirection direction)
  {
    idb::IdbPin* pin = dmInst->get_idb_design()->get_io_pin_list()->add_pin_list(name);
    pin->set_as_io();
    pin->set_term()->set_direction(direction);
    pin->get_term()->set_type(idb::IdbConnectType::kSignal);
    return pin;
  }

  void addInferenceNet(std::string name, const std::vector<idb::IdbPin*>& io_pin_list, const std::vector<idb::IdbPin*>& instance_pin_list = {})
  {
    idb::IdbNet* net = dmInst->get_idb_design()->get_net_list()->add_net(name, idb::IdbConnectType::kSignal);
    net->get_io_pins()->get_pin_list() = io_pin_list;
    net->get_instance_pin_list()->get_pin_list() = instance_pin_list;
  }

  std::map<std::string, std::multiset<std::string>> getReportNetMap(const std::string& report)
  {
    std::istringstream stream(report);
    std::map<std::string, std::multiset<std::string>> net_map;
    std::string net_name;
    for (std::string line; std::getline(stream, line);) {
      if (line.starts_with("Net : ")) {
        net_name = line.substr(6);
      }
      if (!line.empty()) {
        net_map[net_name].insert(line);
      }
    }
    return net_map;
  }
};

int main()
{
  try {
    DesignToolsTest().run();
  } catch (const std::exception& error) {
    MJLOG.warn(imj::Loc::current(), "Design tools regression failed: ", error.what());
    return 1;
  }
  return 0;
}

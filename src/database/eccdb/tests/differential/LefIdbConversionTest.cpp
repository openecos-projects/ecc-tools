// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "IdbObs.h"
#include "idb/IdbLibraryImporter.h"
#include "idb/IdbTechImporter.h"
#include "idb/LegacyLefReader.h"

namespace eccdb {
namespace {

struct Sky130Conversion
{
  LegacyLefReader builder;
  TechStore tech;
  IdbTechImporter tech_importer{tech};
  LibraryStore library{tech.techRegistry()};
  IdbLibraryImporter library_importer{library, tech_importer};
  ::idb::IdbLayout* source = nullptr;

  Sky130Conversion()
  {
    const auto source_root = std::filesystem::path{ECC_TOOLS_SOURCE_DIR};
    const auto tech_lef = source_root / "scripts/foundry/sky130/lef/sky130_fd_sc_hd.tlef";
    const auto cell_lef = source_root / "scripts/foundry/sky130/lef/sky130_fd_sc_hd_merged.lef";
    if (!std::filesystem::exists(tech_lef) || !std::filesystem::exists(cell_lef)) {
      throw std::runtime_error("Sky130 LEF test data is missing");
    }

    std::vector<std::string> tech_files{tech_lef.string()};
    if (builder.buildLef(tech_files, true) == nullptr) {
      throw std::runtime_error("failed to parse the Sky130 technology LEF");
    }
    std::vector<std::string> cell_files{cell_lef.string()};
    auto* service = builder.buildLef(cell_files, false);
    if (service == nullptr || service->get_layout() == nullptr) {
      throw std::runtime_error("failed to parse the Sky130 cell LEF");
    }

    source = service->get_layout();
    tech_importer.import(*source);
    library_importer.import(*source);
  }
};

Sky130Conversion& sky130()
{
  static Sky130Conversion conversion;
  return conversion;
}

bool hasFlag(uint64_t flags, uint64_t flag)
{
  return (flags & flag) != 0u;
}

void expectFlag(const std::string& field, bool source, uint64_t flags, uint64_t flag)
{
  EXPECT_EQ(hasFlag(flags, flag), source) << field;
}

void expectIntField(const std::string& field, int32_t source, int32_t target, uint64_t flags, uint64_t flag)
{
  EXPECT_EQ(hasFlag(flags, flag), source >= 0) << field;
  if (source >= 0) {
    EXPECT_EQ(target, source) << field;
  }
}

void expectDoubleField(const std::string& field, double source, double target, uint64_t flags, uint64_t flag)
{
  EXPECT_EQ(hasFlag(flags, flag), source >= 0.0) << field;
  if (source >= 0.0) {
    EXPECT_DOUBLE_EQ(target, source) << field;
  }
}

TechRoutingDirection expectedDirection(::idb::IdbLayerDirection source)
{
  switch (source) {
    case ::idb::IdbLayerDirection::kHorizontal:
      return TechRoutingDirection::kHorizontal;
    case ::idb::IdbLayerDirection::kVertical:
      return TechRoutingDirection::kVertical;
    case ::idb::IdbLayerDirection::kDiag45:
      return TechRoutingDirection::kDiag45;
    case ::idb::IdbLayerDirection::kDiag135:
      return TechRoutingDirection::kDiag135;
    case ::idb::IdbLayerDirection::kNone:
    case ::idb::IdbLayerDirection::kMax:
      return TechRoutingDirection::kUnknown;
  }
  return TechRoutingDirection::kUnknown;
}

TechRoutingAxisValueForm expectedAxisForm(::idb::IdbLayerOrientType source)
{
  switch (source) {
    case ::idb::IdbLayerOrientType::kBothXY:
      return TechRoutingAxisValueForm::kBothXY;
    case ::idb::IdbLayerOrientType::kSeperateXY:
      return TechRoutingAxisValueForm::kSeparateXY;
    case ::idb::IdbLayerOrientType::kNone:
    case ::idb::IdbLayerOrientType::kMax:
      return TechRoutingAxisValueForm::kNone;
  }
  return TechRoutingAxisValueForm::kNone;
}

TechMastersliceType expectedMastersliceType(const std::string& source)
{
  if (source == "NWELL") {
    return TechMastersliceType::kNWell;
  }
  if (source == "PWELL") {
    return TechMastersliceType::kPWell;
  }
  if (source == "ABOVEDIEEDGE") {
    return TechMastersliceType::kAboveDieEdge;
  }
  if (source == "BELOWDIEEDGE") {
    return TechMastersliceType::kBelowDieEdge;
  }
  if (source == "DIFFUSION") {
    return TechMastersliceType::kDiffusion;
  }
  if (source == "TRIMPOLY") {
    return TechMastersliceType::kTrimPoly;
  }
  if (source == "TRIMMETAL") {
    return TechMastersliceType::kTrimMetal;
  }
  if (source == "REGION") {
    return TechMastersliceType::kRegion;
  }
  return TechMastersliceType::kNone;
}

Rect expectedRect(::idb::IdbRect& source)
{
  return Rect{.ll_x = source.get_low_x(), .ll_y = source.get_low_y(), .ur_x = source.get_high_x(), .ur_y = source.get_high_y()};
}

TEST(IdbImporterTest, RejectsASecondImportEvenWhenTheLegacyLayoutIsEmpty)
{
  ::idb::IdbLayout source;
  TechStore tech;
  IdbTechImporter tech_importer{tech};
  tech_importer.import(source);
  EXPECT_THROW(tech_importer.import(source), std::logic_error);

  LibraryStore library{tech.techRegistry()};
  IdbLibraryImporter library_importer{library, tech_importer};
  library_importer.import(source);
  EXPECT_THROW(library_importer.import(source), std::logic_error);
}

TEST(IdbTechImporterTest, CreatesEveryBasicLayerTypeInOneTechRegistry)
{
  ::idb::IdbLayout source;
  auto* routing_base = source.get_layers()->set_layer("M1", "ROUTING");
  auto* cut_base = source.get_layers()->set_layer("V1", "CUT");
  auto* routing2_base = source.get_layers()->set_layer("M2", "ROUTING");
  auto* masterslice = source.get_layers()->set_layer("NWELL", "MASTERSLICE");
  auto* implant = source.get_layers()->set_layer("NIMP", "IMPLANT");
  auto* implant2 = source.get_layers()->set_layer("PIMP", "IMPLANT");
  auto* overlap = source.get_layers()->set_layer("OVERLAP", "OVERLAP");
  ASSERT_NE(routing_base, nullptr);
  ASSERT_NE(cut_base, nullptr);
  ASSERT_NE(routing2_base, nullptr);
  ASSERT_NE(masterslice, nullptr);
  ASSERT_NE(implant, nullptr);
  ASSERT_NE(implant2, nullptr);
  ASSERT_NE(overlap, nullptr);

  auto* routing = dynamic_cast<::idb::IdbLayerRouting*>(routing_base);
  auto* routing2 = dynamic_cast<::idb::IdbLayerRouting*>(routing2_base);
  auto* cut = dynamic_cast<::idb::IdbLayerCut*>(cut_base);
  auto* n_implant = dynamic_cast<::idb::IdbLayerImplant*>(implant);
  auto* p_implant = dynamic_cast<::idb::IdbLayerImplant*>(implant2);
  ASSERT_NE(routing, nullptr);
  ASSERT_NE(routing2, nullptr);
  ASSERT_NE(cut, nullptr);
  ASSERT_NE(n_implant, nullptr);
  ASSERT_NE(p_implant, nullptr);
  routing->set_direction(::idb::IdbLayerDirection::kHorizontal);
  routing->set_width(100);
  routing2->set_direction(::idb::IdbLayerDirection::kVertical);
  routing2->set_width(120);
  cut->set_width(50);
  cut->set_lef58_type("specialcut");
  cut->set_lef58_backside();
  masterslice->set_lef58_type("NWELL");
  n_implant->set_min_width(40);
  p_implant->set_min_width(50);
  auto* implant_spacing = n_implant->get_min_spacing_list()->add_min_spacing();
  implant_spacing->set_min_spacing(30);
  implant_spacing->set_layer_2nd(p_implant);

  auto* max_via_stack = new ::idb::IdbMaxViaStack();
  max_via_stack->set_no_single(true);
  max_via_stack->set_stacked_via_num(2);
  max_via_stack->set_layer_bottom("M1");
  max_via_stack->set_layer_top("M2");
  source.set_max_via_stack(max_via_stack);

  TechStore tech;
  IdbTechImporter importer{tech};
  importer.import(source);
  const auto& registry = tech.techRegistry().registry();
  EXPECT_TRUE(registry.all_of<TechRoutingLayer>(importer.layerId(routing_base).entity()));
  EXPECT_TRUE(registry.all_of<TechCutLayer>(importer.layerId(cut_base).entity()));
  EXPECT_TRUE(registry.all_of<TechRoutingLayer>(importer.layerId(routing2_base).entity()));
  EXPECT_TRUE(registry.all_of<TechMastersliceLayer>(importer.layerId(masterslice).entity()));
  EXPECT_TRUE(registry.all_of<TechImplantLayer>(importer.layerId(implant).entity()));
  EXPECT_TRUE(registry.all_of<TechImplantLayer>(importer.layerId(implant2).entity()));
  EXPECT_TRUE(registry.all_of<TechOverlapLayer>(importer.layerId(overlap).entity()));
  EXPECT_EQ(tech.layerInfo(importer.layerId(cut_base)).lef58_type, TechLef58LayerType::kSpecialCut);
  EXPECT_NE(tech.layerInfo(importer.layerId(cut_base)).flags & TechLayerInfoFlag::kLef58Backside, 0u);
  EXPECT_EQ(tech.layerInfo(importer.layerId(masterslice)).lef58_type, TechLef58LayerType::kNWell);
  EXPECT_EQ(tech.layerSequence().size(), 6u);
  EXPECT_FALSE(tech.layerPosition(importer.layerId(overlap)).has_value());

  const auto n_implant_id = TechImplantLayerId{importer.layerId(implant).entity()};
  const auto& imported_implant = tech.implantLayerStorage().implantLayer(n_implant_id);
  EXPECT_NE(imported_implant.flags & TechImplantLayerFlag::kHasMinWidth, 0u);
  EXPECT_EQ(imported_implant.min_width, 40);
  const auto spacing_rules = tech.implantLayerStorage().spacingRules(n_implant_id);
  ASSERT_EQ(spacing_rules.size(), 1u);
  const auto& imported_spacing = spacing_rules.front();
  EXPECT_EQ(imported_spacing.min_spacing, 30);
  EXPECT_EQ(imported_spacing.other_layer, TechImplantLayerId{importer.layerId(implant2).entity()});

  ASSERT_TRUE(tech.globalStorage().hasMaxViaStack());
  const auto& imported_stack = tech.globalStorage().getMaxViaStack();
  EXPECT_EQ(imported_stack.max_stack_count, 2u);
  EXPECT_NE(imported_stack.flags & TechMaxViaStackFlag::kNoSingle, 0u);
  EXPECT_NE(imported_stack.flags & TechMaxViaStackFlag::kHasRange, 0u);
  EXPECT_EQ(imported_stack.bottom_layer, TechRoutingLayerId{importer.layerId(routing_base).entity()});
  EXPECT_EQ(imported_stack.top_layer, TechRoutingLayerId{importer.layerId(routing2_base).entity()});
}

TEST(IdbTechImporterTest, ConvertsEverySupportedGlobalAndLayerScalar)
{
  ::idb::IdbLayout source;
  source.set_units(new ::idb::IdbUnits());
  auto* units = source.get_units();
  ASSERT_NE(units, nullptr);
  units->set_nanoseconds(1);
  units->set_picofarads(2);
  units->set_ohms(3);
  units->set_milliwatts(4);
  units->set_milliamps(5);
  units->set_volts(6);
  units->set_microns_dbu(2000);
  units->set_megahertz(8);
  source.set_manufacture_grid(10);

  auto* routing_base = source.get_layers()->set_layer("M1", "ROUTING");
  auto* cut_base = source.get_layers()->set_layer("V1", "CUT");
  auto* routing = dynamic_cast<::idb::IdbLayerRouting*>(routing_base);
  auto* cut = dynamic_cast<::idb::IdbLayerCut*>(cut_base);
  ASSERT_NE(routing, nullptr);
  ASSERT_NE(cut, nullptr);

  routing->set_direction(::idb::IdbLayerDirection::kDiag45);
  routing->set_pitch(::idb::IdbLayerOrientValue{.type = ::idb::IdbLayerOrientType::kSeperateXY, .orient_x = 101, .orient_y = 102});
  routing->set_offset(::idb::IdbLayerOrientValue{.type = ::idb::IdbLayerOrientType::kSeperateXY, .orient_x = 11, .orient_y = 12});
  routing->set_width(100);
  routing->set_min_width(90);
  routing->set_max_width(200);
  routing->set_diag_width(91);
  routing->set_diag_spacing(92);
  routing->set_wire_extension(51);
  routing->set_thickness(21);
  routing->set_height(22);
  routing->set_shrinkage(23);
  routing->set_cap_multiplier(1.5);
  routing->set_fill_active_spacing(24);
  routing->set_area(2500);
  routing->set_resistance(2.5);
  routing->set_capacitance(3.5);
  routing->set_edge_capacitance(4.5);
  routing->set_min_density(5.5);
  routing->set_max_density(6.5);
  routing->set_density_check_length(31);
  routing->set_density_check_width(32);
  routing->set_density_check_step(33);
  routing->set_min_cut_num(2);
  routing->set_min_cut_width(34);
  routing->set_protrusion_width(41, 42, 43);
  routing->set_lef58_rect_only();
  routing->set_lef58_rect_only_except_non_core_pins();
  routing->set_lef58_right_way_on_grid_only();
  routing->set_lef58_right_way_on_grid_only_check_mask();
  routing->set_lef58_type("polyrouting");
  routing->set_lef58_backside();
  cut->set_width(50);
  cut->set_lef58_type("specialcut");
  cut->set_lef58_backside();
  cut->set_resistance_per_cut(7.5);

  TechStore tech;
  IdbTechImporter importer{tech};
  importer.import(source);

  const auto& imported_units = tech.globalStorage().getUnits();
  EXPECT_EQ(imported_units.flags, 0xffu);
  EXPECT_EQ(imported_units.nanoseconds, 1);
  EXPECT_EQ(imported_units.picofarads, 2);
  EXPECT_EQ(imported_units.ohms, 3);
  EXPECT_EQ(imported_units.milliwatts, 4);
  EXPECT_EQ(imported_units.milliamps, 5);
  EXPECT_EQ(imported_units.volts, 6);
  EXPECT_EQ(imported_units.database_units_per_micron, 2000);
  EXPECT_EQ(imported_units.megahertz, 8);
  EXPECT_EQ(tech.globalStorage().getManufacturingGrid().value, 10);

  const auto& imported = tech.routingLayerStorage().routingLayer(TechRoutingLayerId{importer.layerId(routing_base).entity()});
  const uint64_t expected_flags
      = TechRoutingLayerFlag::kHasMinWidth | TechRoutingLayerFlag::kHasMaxWidth | TechRoutingLayerFlag::kHasPitchY
        | TechRoutingLayerFlag::kHasOffsetY | TechRoutingLayerFlag::kHasWireExtension | TechRoutingLayerFlag::kHasThickness
        | TechRoutingLayerFlag::kHasHeight | TechRoutingLayerFlag::kHasArea | TechRoutingLayerFlag::kHasResistance
        | TechRoutingLayerFlag::kHasCapacitance | TechRoutingLayerFlag::kHasEdgeCapacitance | TechRoutingLayerFlag::kHasMinCut
        | TechRoutingLayerFlag::kLef58RectOnly | TechRoutingLayerFlag::kLef58RectOnlyExceptNonCorePins
        | TechRoutingLayerFlag::kLef58RightWayOnGridOnly | TechRoutingLayerFlag::kLef58RightWayOnGridOnlyCheckMask
        | TechRoutingLayerFlag::kHasWidth | TechRoutingLayerFlag::kHasMinDensity | TechRoutingLayerFlag::kHasMaxDensity
        | TechRoutingLayerFlag::kHasDensityCheckWindow | TechRoutingLayerFlag::kHasDensityCheckStep | TechRoutingLayerFlag::kHasDiagWidth
        | TechRoutingLayerFlag::kHasDiagSpacing | TechRoutingLayerFlag::kHasProtrusion | TechRoutingLayerFlag::kHasShrinkage
        | TechRoutingLayerFlag::kHasCapMultiplier | TechRoutingLayerFlag::kHasFillActiveSpacing | TechRoutingLayerFlag::kPolyRouting;
  EXPECT_EQ(imported.flags, expected_flags);
  EXPECT_EQ(imported.direction, TechRoutingDirection::kDiag45);
  EXPECT_EQ(imported.pitch_form, TechRoutingAxisValueForm::kSeparateXY);
  EXPECT_EQ(imported.offset_form, TechRoutingAxisValueForm::kSeparateXY);
  EXPECT_EQ(imported.width, 100);
  EXPECT_EQ(imported.min_width, 90);
  EXPECT_EQ(imported.max_width, 200);
  EXPECT_EQ(imported.diag_width, 91);
  EXPECT_EQ(imported.diag_spacing, 92);
  EXPECT_EQ(imported.pitch_x, 101);
  EXPECT_EQ(imported.pitch_y, 102);
  EXPECT_EQ(imported.offset_x, 11);
  EXPECT_EQ(imported.offset_y, 12);
  EXPECT_EQ(imported.wire_extension, 51);
  EXPECT_EQ(imported.thickness, 21);
  EXPECT_EQ(imported.height, 22);
  EXPECT_EQ(imported.shrinkage, 23);
  EXPECT_DOUBLE_EQ(imported.cap_multiplier, 1.5);
  EXPECT_EQ(imported.fill_active_spacing, 24);
  EXPECT_EQ(imported.area, 2500);
  EXPECT_DOUBLE_EQ(imported.resistance, 2.5);
  EXPECT_DOUBLE_EQ(imported.capacitance, 3.5);
  EXPECT_DOUBLE_EQ(imported.edge_capacitance, 4.5);
  EXPECT_DOUBLE_EQ(imported.min_density, 5.5);
  EXPECT_DOUBLE_EQ(imported.max_density, 6.5);
  EXPECT_EQ(imported.density_check_length, 31);
  EXPECT_EQ(imported.density_check_width, 32);
  EXPECT_EQ(imported.density_check_step, 33);
  EXPECT_EQ(imported.min_cut_num, 2);
  EXPECT_EQ(imported.min_cut_width, 34);
  EXPECT_EQ(imported.protrusion_width1, 41);
  EXPECT_EQ(imported.protrusion_length, 42);
  EXPECT_EQ(imported.protrusion_width2, 43);
  const auto& imported_routing_info = tech.layerInfo(importer.layerId(routing_base));
  EXPECT_EQ(imported_routing_info.lef58_type, TechLef58LayerType::kPolyRouting);
  EXPECT_NE(imported_routing_info.flags & TechLayerInfoFlag::kLef58Backside, 0u);

  const auto& imported_cut = tech.cutLayerStorage().cutLayer(TechCutLayerId{importer.layerId(cut_base).entity()});
  EXPECT_EQ(imported_cut.flags, TechCutLayerFlag::kHasWidth | TechCutLayerFlag::kHasResistance);
  EXPECT_EQ(imported_cut.width, 50);
  EXPECT_DOUBLE_EQ(imported_cut.resistance_per_cut, 7.5);
  const auto& imported_cut_info = tech.layerInfo(importer.layerId(cut_base));
  EXPECT_EQ(imported_cut_info.lef58_type, TechLef58LayerType::kSpecialCut);
  EXPECT_NE(imported_cut_info.flags & TechLayerInfoFlag::kLef58Backside, 0u);
}

TEST(IdbTechImporterTest, ConvertsEverySupportedMastersliceSubtypeCaseInsensitively)
{
  const std::vector<std::pair<std::string, TechMastersliceType>> cases{{"nwell", TechMastersliceType::kNWell},
                                                                       {"pwell", TechMastersliceType::kPWell},
                                                                       {"abovedieedge", TechMastersliceType::kAboveDieEdge},
                                                                       {"belowdieedge", TechMastersliceType::kBelowDieEdge},
                                                                       {"diffusion", TechMastersliceType::kDiffusion},
                                                                       {"trimpoly", TechMastersliceType::kTrimPoly},
                                                                       {"trimmetal", TechMastersliceType::kTrimMetal},
                                                                       {"region", TechMastersliceType::kRegion}};
  ::idb::IdbLayout source;
  std::vector<::idb::IdbLayer*> source_layers;
  source_layers.reserve(cases.size());
  for (std::size_t index = 0; index < cases.size(); ++index) {
    auto* layer = source.get_layers()->set_layer("MS" + std::to_string(index), "MASTERSLICE");
    ASSERT_NE(layer, nullptr);
    layer->set_lef58_type(cases[index].first);
    source_layers.push_back(layer);
  }

  TechStore tech;
  IdbTechImporter importer{tech};
  importer.import(source);

  for (std::size_t index = 0; index < cases.size(); ++index) {
    const auto id = TechMastersliceLayerId{importer.layerId(source_layers[index]).entity()};
    EXPECT_EQ(tech.mastersliceLayerStorage().mastersliceLayer(id).subtype, cases[index].second);
  }
}

TEST(LefIdbConversionTest, ImportsRealSky130TechAndCellLibrary)
{
  auto& conversion = sky130();
  auto& tech = conversion.tech;
  auto& library = conversion.library;

  const auto& globals = tech.globalStorage();
  ASSERT_TRUE(globals.hasUnits());
  EXPECT_EQ(globals.getUnits().database_units_per_micron, 1000);
  EXPECT_NE(globals.getUnits().flags & TechGlobalUnitsFlag::kHasDatabaseUnitsPerMicron, 0u);
  ASSERT_TRUE(globals.hasManufacturingGrid());
  EXPECT_EQ(globals.getManufacturingGrid().value, 5);

  ASSERT_EQ(tech.layerSequence().size(), 13u);
  const auto li1 = tech.findLayer("li1");
  const auto mcon = tech.findLayer("mcon");
  const auto nwell = tech.findLayer("nwell");
  ASSERT_TRUE(static_cast<bool>(li1));
  ASSERT_TRUE(static_cast<bool>(mcon));
  ASSERT_TRUE(static_cast<bool>(nwell));

  const auto& tech_registry = tech.techRegistry().registry();
  EXPECT_TRUE(tech_registry.all_of<TechRoutingLayer>(li1.entity()));
  EXPECT_TRUE(tech_registry.all_of<TechCutLayer>(mcon.entity()));
  EXPECT_TRUE(tech_registry.all_of<TechMastersliceLayer>(nwell.entity()));

  const auto& li1_data = tech.routingLayerStorage().routingLayer(TechRoutingLayerId{li1.entity()});
  EXPECT_EQ(li1_data.direction, TechRoutingDirection::kVertical);
  EXPECT_EQ(li1_data.width, 170);
  EXPECT_EQ(li1_data.pitch_x, 460);
  EXPECT_EQ(li1_data.pitch_y, 340);
  EXPECT_EQ(tech.cutLayerStorage().cutLayer(TechCutLayerId{mcon.entity()}).width, 170);
  EXPECT_EQ(tech.mastersliceLayerStorage().mastersliceLayer(TechMastersliceLayerId{nwell.entity()}).subtype, TechMastersliceType::kNWell);

  const auto unithd_id = library.siteStorage().findSite("unithd");
  ASSERT_TRUE(static_cast<bool>(unithd_id));
  const auto& unithd = library.siteStorage().site(unithd_id);
  EXPECT_EQ(unithd.site_class, LibrarySiteClass::kCore);
  EXPECT_EQ(unithd.width, 460);
  EXPECT_EQ(unithd.height, 2720);
  EXPECT_TRUE(unithd.symmetry_y);

  EXPECT_EQ(library.cellMasterStorage().cellMasterCount(), 437u);
  const auto master_id = library.cellMasterStorage().findCellMaster("sky130_fd_sc_hd__a2111o_1");
  ASSERT_TRUE(static_cast<bool>(master_id));
  const auto& master = library.cellMasterStorage().cellMaster(master_id);
  EXPECT_EQ(master.type, LibraryCellMasterType::kCore);
  EXPECT_EQ(master.width, 4140u);
  EXPECT_EQ(master.height, 2720u);
  ASSERT_TRUE(master.site.has_value());
  EXPECT_EQ(*master.site, unithd_id);
  ASSERT_EQ(master.terms.size(), 8u);

  const auto a1_id = library.masterTermStorage().findMasterTerm(master_id, "A1");
  ASSERT_TRUE(static_cast<bool>(a1_id));
  const auto& a1 = library.masterTermStorage().masterTerm(a1_id);
  EXPECT_EQ(a1.direction, LibraryMasterTermDirection::kInput);
  EXPECT_EQ(a1.use, LibraryMasterTermUse::kSignal);
  ASSERT_EQ(a1.ports.size(), 1u);
  const auto& a1_port = library.masterPortStorage().masterPort(a1.ports.front());
  ASSERT_EQ(a1_port.layer_clauses.size(), 1u);
  EXPECT_EQ(a1_port.layer_clauses.front().layer, li1);
  const auto a1_rects = library.geometryPool().rectangles(a1_port.layer_clauses.front().geometry);
  ASSERT_EQ(a1_rects.size(), 3u);
  EXPECT_EQ(a1_rects.front(), (Rect{.ll_x = 2905, .ll_y = 995, .ur_x = 3290, .ur_y = 1325}));

  const auto vgnd_id = library.masterTermStorage().findMasterTerm(master_id, "VGND");
  ASSERT_TRUE(static_cast<bool>(vgnd_id));
  const auto& vgnd = library.masterTermStorage().masterTerm(vgnd_id);
  EXPECT_EQ(vgnd.direction, LibraryMasterTermDirection::kInOut);
  EXPECT_EQ(vgnd.use, LibraryMasterTermUse::kGround);
  EXPECT_EQ(vgnd.shape, LibraryMasterTermShape::kAbutment);
  EXPECT_EQ(vgnd.ports.size(), 2u);

  ASSERT_TRUE(library.cellMasterStorage().hasObs(master_id));
  const auto& obs = library.cellMasterStorage().obs(master_id);
  ASSERT_EQ(obs.layer_clauses.size(), 1u);
  EXPECT_EQ(obs.layer_clauses.front().layer, li1);
  EXPECT_EQ(library.geometryPool().rectangles(obs.layer_clauses.front().geometry).size(), 10u);
}

TEST(LefIdbConversionTest, MatchesEverySupportedTechFieldAgainstLegacyIdb)
{
  auto& conversion = sky130();
  auto& source = *conversion.source;
  auto& tech = conversion.tech;
  const auto& registry = tech.techRegistry().registry();

  const auto* source_units = source.get_units();
  ASSERT_NE(source_units, nullptr);
  const auto& units = tech.globalStorage().getUnits();
  expectIntField("UNITS TIME", source_units->get_nanoseconds(), units.nanoseconds, units.flags, TechGlobalUnitsFlag::kHasNanoseconds);
  expectIntField("UNITS CAPACITANCE", source_units->get_picofarads(), units.picofarads, units.flags, TechGlobalUnitsFlag::kHasPicofarads);
  expectIntField("UNITS RESISTANCE", source_units->get_ohms(), units.ohms, units.flags, TechGlobalUnitsFlag::kHasOhms);
  expectIntField("UNITS POWER", source_units->get_milliwatts(), units.milliwatts, units.flags, TechGlobalUnitsFlag::kHasMilliwatts);
  expectIntField("UNITS CURRENT", source_units->get_milliamps(), units.milliamps, units.flags, TechGlobalUnitsFlag::kHasMilliamps);
  expectIntField("UNITS VOLTAGE", source_units->get_volts(), units.volts, units.flags, TechGlobalUnitsFlag::kHasVolts);
  expectIntField("UNITS DATABASE", source_units->get_micron_dbu(), units.database_units_per_micron, units.flags,
                 TechGlobalUnitsFlag::kHasDatabaseUnitsPerMicron);
  expectIntField("UNITS FREQUENCY", source_units->get_megahertz(), units.megahertz, units.flags, TechGlobalUnitsFlag::kHasMegahertz);
  EXPECT_EQ(tech.globalStorage().getManufacturingGrid().value, source.get_munufacture_grid());

  const auto* source_stack = source.get_max_via_stack();
  EXPECT_EQ(tech.globalStorage().hasMaxViaStack(), source_stack != nullptr);
  if (source_stack != nullptr) {
    const auto& stack = tech.globalStorage().getMaxViaStack();
    EXPECT_EQ(stack.max_stack_count, source_stack->get_stacked_via_num());
    expectFlag("MAXVIASTACK NOSINGLE", source_stack->is_no_single(), stack.flags, TechMaxViaStackFlag::kNoSingle);
    expectFlag("MAXVIASTACK RANGE", source_stack->is_range(), stack.flags, TechMaxViaStackFlag::kHasRange);
    if (source_stack->is_range()) {
      EXPECT_EQ(stack.bottom_layer, TechRoutingLayerId{tech.findLayer(source_stack->get_layer_bottom()).entity()});
      EXPECT_EQ(stack.top_layer, TechRoutingLayerId{tech.findLayer(source_stack->get_layer_top()).entity()});
    }
  }

  const auto& source_layers = source.get_layers()->get_layers();
  std::size_t imported_layer_count = 0;
  for (const auto entity : registry.view<const TechLayerInfo>()) {
    static_cast<void>(entity);
    ++imported_layer_count;
  }
  ASSERT_EQ(imported_layer_count, source_layers.size());

  std::size_t sequence_index = 0;
  for (auto* source_layer : source_layers) {
    ASSERT_NE(source_layer, nullptr);
    SCOPED_TRACE(source_layer->get_name());
    const auto layer = conversion.tech_importer.layerId(source_layer);
    EXPECT_EQ(tech.findLayer(source_layer->get_name()), layer);
    EXPECT_EQ(tech.layerInfo(layer).name, source_layer->get_name());

    if (source_layer->get_type() != ::idb::IdbLayerType::kLayerOverlap) {
      ASSERT_LT(sequence_index, tech.layerSequence().size());
      EXPECT_EQ(tech.layerSequence()[sequence_index], layer);
      ++sequence_index;
    }

    switch (source_layer->get_type()) {
      case ::idb::IdbLayerType::kLayerRouting: {
        auto* source_routing = dynamic_cast<::idb::IdbLayerRouting*>(source_layer);
        ASSERT_NE(source_routing, nullptr);
        ASSERT_TRUE(registry.all_of<TechRoutingLayer>(layer.entity()));
        const auto& routing = registry.get<const TechRoutingLayer>(layer.entity());
        EXPECT_EQ(routing.direction, expectedDirection(source_routing->get_direction()));
        EXPECT_EQ(routing.pitch_form, expectedAxisForm(source_routing->get_pitch().type));
        EXPECT_EQ(routing.offset_form, expectedAxisForm(source_routing->get_offset().type));
        expectFlag("PITCH Y", source_routing->get_pitch().type == ::idb::IdbLayerOrientType::kSeperateXY, routing.flags,
                   TechRoutingLayerFlag::kHasPitchY);
        expectFlag("OFFSET Y", source_routing->get_offset().type == ::idb::IdbLayerOrientType::kSeperateXY, routing.flags,
                   TechRoutingLayerFlag::kHasOffsetY);
        if (routing.pitch_form != TechRoutingAxisValueForm::kNone) {
          EXPECT_EQ(routing.pitch_x, source_routing->get_pitch_x());
          EXPECT_EQ(routing.pitch_y, source_routing->get_pitch_y());
        }
        if (routing.offset_form != TechRoutingAxisValueForm::kNone) {
          EXPECT_EQ(routing.offset_x, source_routing->get_offset_x());
          EXPECT_EQ(routing.offset_y, source_routing->get_offset_y());
        }
        expectIntField("WIDTH", source_routing->get_width(), routing.width, routing.flags, TechRoutingLayerFlag::kHasWidth);
        expectIntField("MINWIDTH", source_routing->get_min_width(), routing.min_width, routing.flags, TechRoutingLayerFlag::kHasMinWidth);
        expectIntField("MAXWIDTH", source_routing->get_max_width(), routing.max_width, routing.flags, TechRoutingLayerFlag::kHasMaxWidth);
        expectIntField("DIAGWIDTH", source_routing->get_diag_width(), routing.diag_width, routing.flags,
                       TechRoutingLayerFlag::kHasDiagWidth);
        expectIntField("DIAGSPACING", source_routing->get_diag_spacing(), routing.diag_spacing, routing.flags,
                       TechRoutingLayerFlag::kHasDiagSpacing);
        expectIntField("WIREEXTENSION", source_routing->get_wire_extension(), routing.wire_extension, routing.flags,
                       TechRoutingLayerFlag::kHasWireExtension);
        expectIntField("THICKNESS", source_routing->get_thickness(), routing.thickness, routing.flags, TechRoutingLayerFlag::kHasThickness);
        expectIntField("HEIGHT", source_routing->get_height(), routing.height, routing.flags, TechRoutingLayerFlag::kHasHeight);
        expectIntField("SHRINKAGE", source_routing->get_shrinkage(), routing.shrinkage, routing.flags, TechRoutingLayerFlag::kHasShrinkage);
        expectIntField("FILLACTIVESPACING", source_routing->get_fill_active_spacing(), routing.fill_active_spacing, routing.flags,
                       TechRoutingLayerFlag::kHasFillActiveSpacing);
        expectIntField("AREA", source_routing->get_area(), routing.area, routing.flags, TechRoutingLayerFlag::kHasArea);
        expectDoubleField("CAPMULTIPLIER", source_routing->get_cap_multiplier(), routing.cap_multiplier, routing.flags,
                          TechRoutingLayerFlag::kHasCapMultiplier);
        expectDoubleField("RESISTANCE", source_routing->get_resistance(), routing.resistance, routing.flags,
                          TechRoutingLayerFlag::kHasResistance);
        expectDoubleField("CAPACITANCE", source_routing->get_capacitance(), routing.capacitance, routing.flags,
                          TechRoutingLayerFlag::kHasCapacitance);
        expectDoubleField("EDGECAPACITANCE", source_routing->get_edge_capacitance(), routing.edge_capacitance, routing.flags,
                          TechRoutingLayerFlag::kHasEdgeCapacitance);
        expectDoubleField("MINIMUMDENSITY", source_routing->get_min_density(), routing.min_density, routing.flags,
                          TechRoutingLayerFlag::kHasMinDensity);
        expectDoubleField("MAXIMUMDENSITY", source_routing->get_max_density(), routing.max_density, routing.flags,
                          TechRoutingLayerFlag::kHasMaxDensity);
        const bool has_density_window = source_routing->get_density_check_length() >= 0 && source_routing->get_density_check_width() >= 0;
        expectFlag("DENSITYCHECKWINDOW", has_density_window, routing.flags, TechRoutingLayerFlag::kHasDensityCheckWindow);
        if (has_density_window) {
          EXPECT_EQ(routing.density_check_length, source_routing->get_density_check_length());
          EXPECT_EQ(routing.density_check_width, source_routing->get_density_check_width());
        }
        expectIntField("DENSITYCHECKSTEP", source_routing->get_density_check_step(), routing.density_check_step, routing.flags,
                       TechRoutingLayerFlag::kHasDensityCheckStep);
        const bool has_min_cut = source_routing->get_min_cut_num() >= 0 && source_routing->get_min_cut_width() >= 0;
        expectFlag("MINIMUMCUT", has_min_cut, routing.flags, TechRoutingLayerFlag::kHasMinCut);
        if (has_min_cut) {
          EXPECT_EQ(routing.min_cut_num, source_routing->get_min_cut_num());
          EXPECT_EQ(routing.min_cut_width, source_routing->get_min_cut_width());
        }
        expectFlag("PROTRUSIONWIDTH", source_routing->has_protrusion_width(), routing.flags, TechRoutingLayerFlag::kHasProtrusion);
        if (source_routing->has_protrusion_width()) {
          EXPECT_EQ(routing.protrusion_width1, source_routing->get_protrusion_width_1());
          EXPECT_EQ(routing.protrusion_length, source_routing->get_protrusion_length());
          EXPECT_EQ(routing.protrusion_width2, source_routing->get_protrusion_width_2());
        }
        expectFlag("LEF58_RECTONLY", source_routing->is_lef58_rect_only(), routing.flags, TechRoutingLayerFlag::kLef58RectOnly);
        expectFlag("LEF58_RECTONLY EXCEPTNONCOREPINS", source_routing->is_lef58_rect_only_except_non_core_pins(), routing.flags,
                   TechRoutingLayerFlag::kLef58RectOnlyExceptNonCorePins);
        expectFlag("LEF58_RIGHTWAYONGRIDONLY", source_routing->is_lef58_right_way_on_grid_only(), routing.flags,
                   TechRoutingLayerFlag::kLef58RightWayOnGridOnly);
        expectFlag("LEF58_RIGHTWAYONGRIDONLY CHECKMASK", source_routing->is_lef58_right_way_on_grid_only_check_mask(), routing.flags,
                   TechRoutingLayerFlag::kLef58RightWayOnGridOnlyCheckMask);
        expectFlag("LEF58_TYPE POLYROUTING", source_routing->get_lef58_type() == "POLYROUTING", routing.flags,
                   TechRoutingLayerFlag::kPolyRouting);
        break;
      }
      case ::idb::IdbLayerType::kLayerCut: {
        auto* source_cut = dynamic_cast<::idb::IdbLayerCut*>(source_layer);
        ASSERT_NE(source_cut, nullptr);
        ASSERT_TRUE(registry.all_of<TechCutLayer>(layer.entity()));
        const auto& cut = registry.get<const TechCutLayer>(layer.entity());
        expectIntField("CUT WIDTH", source_cut->get_width(), cut.width, cut.flags, TechCutLayerFlag::kHasWidth);
        expectDoubleField("CUT RESISTANCE", source_cut->get_resistance_per_cut(), cut.resistance_per_cut, cut.flags,
                          TechCutLayerFlag::kHasResistance);
        break;
      }
      case ::idb::IdbLayerType::kLayerMasterslice: {
        ASSERT_TRUE(registry.all_of<TechMastersliceLayer>(layer.entity()));
        const auto& masterslice = registry.get<const TechMastersliceLayer>(layer.entity());
        EXPECT_EQ(masterslice.subtype, expectedMastersliceType(source_layer->get_lef58_type()));
        break;
      }
      case ::idb::IdbLayerType::kLayerImplant: {
        auto* source_implant = dynamic_cast<::idb::IdbLayerImplant*>(source_layer);
        ASSERT_NE(source_implant, nullptr);
        ASSERT_TRUE(registry.all_of<TechImplantLayer>(layer.entity()));
        const auto implant_id = TechImplantLayerId{layer.entity()};
        const auto& implant = tech.implantLayerStorage().implantLayer(implant_id);
        expectIntField("IMPLANT WIDTH", source_implant->get_min_width(), implant.min_width, implant.flags,
                       TechImplantLayerFlag::kHasMinWidth);

        const auto& source_spacings = source_implant->get_min_spacing_list()->get_min_spacing_list();
        const auto spacings = tech.implantLayerStorage().spacingRules(implant_id);
        ASSERT_EQ(spacings.size(), source_spacings.size());
        for (std::size_t spacing_index = 0; spacing_index < source_spacings.size(); ++spacing_index) {
          ASSERT_NE(source_spacings[spacing_index], nullptr);
          const auto& spacing = spacings[spacing_index];
          EXPECT_EQ(spacing.min_spacing, source_spacings[spacing_index]->get_min_spacing());
          const auto* source_other = source_spacings[spacing_index]->get_layer_2nd();
          expectFlag("IMPLANT SPACING LAYER", source_other != nullptr, spacing.flags, TechImplantSpacingRuleFlag::kHasOtherLayer);
          if (source_other != nullptr) {
            EXPECT_EQ(spacing.other_layer, TechImplantLayerId{conversion.tech_importer.layerId(source_other).entity()});
          }
        }
        break;
      }
      case ::idb::IdbLayerType::kLayerOverlap:
        EXPECT_TRUE(registry.all_of<TechOverlapLayer>(layer.entity()));
        break;
      case ::idb::IdbLayerType::kNone:
      case ::idb::IdbLayerType::kMax:
        FAIL() << "unsupported source layer type";
        break;
    }
  }
  EXPECT_EQ(sequence_index, tech.layerSequence().size());
}

TEST(LefIdbConversionTest, MatchesEveryImportedLibraryObjectAndRectangleAgainstLegacyIdb)
{
  auto& conversion = sky130();
  auto& source = *conversion.source;
  auto& library = conversion.library;

  const auto& source_sites = source.get_sites()->get_site_list();
  ASSERT_EQ(library.siteStorage().siteCount(), source_sites.size());
  for (auto* source_site : source_sites) {
    ASSERT_NE(source_site, nullptr);
    SCOPED_TRACE(source_site->get_name());
    const auto site_id = library.siteStorage().findSite(source_site->get_name());
    ASSERT_TRUE(static_cast<bool>(site_id));
    const auto& site = library.siteStorage().site(site_id);
    EXPECT_EQ(site.width, source_site->get_width());
    EXPECT_EQ(site.height, source_site->get_height());
    EXPECT_EQ(site.overlap, source_site->is_overlap());
    EXPECT_EQ(static_cast<uint8_t>(site.site_class), static_cast<uint8_t>(source_site->get_site_class()));
    EXPECT_EQ(site.symmetry_x, source_site->get_symmetry() == ::idb::IdbSymmetry::kX);
    EXPECT_EQ(site.symmetry_y, source_site->get_symmetry() == ::idb::IdbSymmetry::kY);
    EXPECT_EQ(site.symmetry_r90, source_site->get_symmetry() == ::idb::IdbSymmetry::kR90);
  }

  const auto& source_masters = source.get_cell_master_list()->get_cell_master();
  ASSERT_EQ(library.cellMasterStorage().cellMasterCount(), source_masters.size());
  for (auto* source_master : source_masters) {
    ASSERT_NE(source_master, nullptr);
    SCOPED_TRACE(source_master->get_name());
    const auto master_id = library.cellMasterStorage().findCellMaster(source_master->get_name());
    ASSERT_TRUE(static_cast<bool>(master_id));
    const auto& master = library.cellMasterStorage().cellMaster(master_id);
    EXPECT_EQ(static_cast<uint8_t>(master.type), static_cast<uint8_t>(source_master->get_type()));
    EXPECT_EQ(master.symmetry_x, source_master->is_symmetry_x());
    EXPECT_EQ(master.symmetry_y, source_master->is_symmetry_y());
    EXPECT_EQ(master.symmetry_r90, source_master->is_symmetry_R90());
    EXPECT_EQ(master.origin_x, source_master->get_origin_x());
    EXPECT_EQ(master.origin_y, source_master->get_origin_y());
    EXPECT_EQ(master.width, source_master->get_width());
    EXPECT_EQ(master.height, source_master->get_height());
    if (source_master->get_site() == nullptr) {
      EXPECT_FALSE(master.site.has_value());
    } else {
      ASSERT_TRUE(master.site.has_value());
      EXPECT_EQ(*master.site, library.siteStorage().findSite(source_master->get_site()->get_name()));
    }

    const auto source_terms = source_master->get_term_list();
    ASSERT_EQ(master.terms.size(), source_terms.size());
    for (std::size_t term_index = 0; term_index < source_terms.size(); ++term_index) {
      auto* source_term = source_terms[term_index];
      ASSERT_NE(source_term, nullptr);
      const auto term_id = master.terms[term_index];
      const auto& term = library.masterTermStorage().masterTerm(term_id);
      EXPECT_EQ(library.masterTermStorage().owner(term_id), master_id);
      EXPECT_EQ(term.name, source_term->get_name());
      EXPECT_EQ(static_cast<uint8_t>(term.direction), static_cast<uint8_t>(source_term->get_direction()));
      EXPECT_EQ(static_cast<uint8_t>(term.use), static_cast<uint8_t>(source_term->get_type()));
      EXPECT_EQ(static_cast<uint8_t>(term.shape), static_cast<uint8_t>(source_term->get_shape()));

      const auto& source_ports = source_term->get_port_list();
      ASSERT_EQ(term.ports.size(), source_ports.size());
      for (std::size_t port_index = 0; port_index < source_ports.size(); ++port_index) {
        auto* source_port = source_ports[port_index];
        ASSERT_NE(source_port, nullptr);
        const auto port_id = term.ports[port_index];
        const auto& port = library.masterPortStorage().masterPort(port_id);
        EXPECT_EQ(library.masterPortStorage().owner(port_id), term_id);
        EXPECT_EQ(static_cast<uint8_t>(port.port_class), static_cast<uint8_t>(source_port->get_port_class()));
        EXPECT_TRUE(source_port->get_via_list().empty());
        EXPECT_TRUE(port.vias.empty());

        std::size_t clause_index = 0;
        for (auto* source_shape : source_port->get_layer_shape()) {
          if (source_shape == nullptr || source_shape->get_layer() == nullptr) {
            continue;
          }
          const auto& source_rects = source_shape->get_rect_list();
          if (source_rects.empty()) {
            continue;
          }
          ASSERT_LT(clause_index, port.layer_clauses.size());
          const auto& clause = port.layer_clauses[clause_index++];
          EXPECT_EQ(clause.layer, conversion.tech_importer.layerId(source_shape->get_layer()));
          const auto rects = library.geometryPool().rectangles(clause.geometry);
          ASSERT_EQ(rects.size(), source_rects.size());
          for (std::size_t rect_index = 0; rect_index < source_rects.size(); ++rect_index) {
            ASSERT_NE(source_rects[rect_index], nullptr);
            EXPECT_EQ(rects[rect_index], expectedRect(*source_rects[rect_index]));
          }
        }
        EXPECT_EQ(clause_index, port.layer_clauses.size());
      }
    }

    std::vector<::idb::IdbLayerShape*> source_obs_clauses;
    for (auto* source_obs : source_master->get_obs_list()) {
      if (source_obs == nullptr) {
        continue;
      }
      for (auto* source_obs_layer : source_obs->get_obs_layer_list()) {
        if (source_obs_layer != nullptr && source_obs_layer->get_shape() != nullptr && source_obs_layer->get_shape()->get_layer() != nullptr
            && !source_obs_layer->get_shape()->get_rect_list().empty()) {
          source_obs_clauses.push_back(source_obs_layer->get_shape());
        }
      }
    }
    EXPECT_EQ(library.cellMasterStorage().hasObs(master_id), !source_obs_clauses.empty());
    if (!source_obs_clauses.empty()) {
      const auto& obs = library.cellMasterStorage().obs(master_id);
      ASSERT_EQ(obs.layer_clauses.size(), source_obs_clauses.size());
      for (std::size_t clause_index = 0; clause_index < source_obs_clauses.size(); ++clause_index) {
        auto* source_shape = source_obs_clauses[clause_index];
        const auto& clause = obs.layer_clauses[clause_index];
        EXPECT_EQ(clause.layer, conversion.tech_importer.layerId(source_shape->get_layer()));
        const auto& source_rects = source_shape->get_rect_list();
        const auto rects = library.geometryPool().rectangles(clause.geometry);
        ASSERT_EQ(rects.size(), source_rects.size());
        for (std::size_t rect_index = 0; rect_index < source_rects.size(); ++rect_index) {
          ASSERT_NE(source_rects[rect_index], nullptr);
          EXPECT_EQ(rects[rect_index], expectedRect(*source_rects[rect_index]));
        }
      }
      EXPECT_TRUE(obs.vias.empty());
    }
  }
}

}  // namespace
}  // namespace eccdb

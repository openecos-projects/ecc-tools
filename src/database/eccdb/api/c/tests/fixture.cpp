#include "tech/TechStore.h"
#include "library/LibraryStore.h"
#include "design/DesignStore.h"
#include "io/binary/BinaryDatabaseExporter.h"
#include <fstream>

int main() {
  using namespace eccdb;
  TechStore tech;
  auto bottom=tech.createRoutingLayer({.name="M1"});
  auto cut=tech.createCutLayer({.name="V1"});
  auto top=tech.createRoutingLayer({.name="M2"});
  auto& reg=tech.techRegistry().registry();
  auto rule=reg.create();
  reg.emplace<TechViaRuleGenerate>(rule,TechViaRuleGenerate{.name="VR"});
  reg.emplace<TechViaRuleGenerateBottomLayer>(rule,TechViaRuleGenerateBottomLayer{.layer=bottom});
  reg.emplace<TechViaRuleGenerateCutLayer>(rule,TechViaRuleGenerateCutLayer{.layer=cut});
  reg.emplace<TechViaRuleGenerateTopLayer>(rule,TechViaRuleGenerateTopLayer{.layer=top});
  LibraryStore library{tech.techRegistry()};
  auto master=library.cellMasterStorage().createCellMaster({.name="INV",.width=20,.height=40});
  (void)library.masterTermStorage().createMasterTerm(master,{.name="A"});
  DesignStore design{tech.techRegistry(),library.libraryRegistry()};
  design.globalStorage().setInfo({.name="c_abi_test",.database_units_per_micron=1000});
  design.globalStorage().setDieArea({.boundary={{0,0},{1000,1000}}});
  BinaryDatabaseExporter::saveTech("tech.bin",tech);
  BinaryDatabaseExporter::saveLibrary("library.bin",library);
  BinaryDatabaseExporter::saveDesign("design.bin",design);
  std::ofstream ids("ids.txt");
  ids << bottom.packed() << ' ' << cut.packed() << ' ' << top.packed() << ' '
      << TechViaRuleGenerateId{rule}.packed() << ' ' << master.packed() << '\n';
  return ids ? 0 : 1;
}

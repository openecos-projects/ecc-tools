#include "lef/LefLibraryExporter.h"

#include <algorithm>
#include <fstream>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "lef/LefExportFormat.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

std::string_view siteClassName(LibrarySiteClass value)
{
  switch (value) {
    case LibrarySiteClass::kCore:
      return "CORE";
    case LibrarySiteClass::kPad:
      return "PAD";
    case LibrarySiteClass::kCorner:
      return "CORNER";
    case LibrarySiteClass::kVirtual:
      return "VIRTUAL";
    case LibrarySiteClass::kNone:
      break;
  }
  throw std::invalid_argument("SITE has no class");
}

std::string_view masterClassName(LibraryCellMasterType value)
{
  switch (value) {
    case LibraryCellMasterType::kCover:
      return "COVER";
    case LibraryCellMasterType::kCoverBump:
      return "COVER BUMP";
    case LibraryCellMasterType::kRing:
      return "RING";
    case LibraryCellMasterType::kBlock:
      return "BLOCK";
    case LibraryCellMasterType::kBlockBlackbox:
      return "BLOCK BLACKBOX";
    case LibraryCellMasterType::kBlockSoft:
      return "BLOCK SOFT";
    case LibraryCellMasterType::kPad:
      return "PAD";
    case LibraryCellMasterType::kPadInput:
      return "PAD INPUT";
    case LibraryCellMasterType::kPadOutput:
      return "PAD OUTPUT";
    case LibraryCellMasterType::kPadInOut:
      return "PAD INOUT";
    case LibraryCellMasterType::kPadPower:
      return "PAD POWER";
    case LibraryCellMasterType::kPadSpacer:
      return "PAD SPACER";
    case LibraryCellMasterType::kPadAreaIo:
      return "PAD AREAIO";
    case LibraryCellMasterType::kCore:
      return "CORE";
    case LibraryCellMasterType::kCoreFeedThru:
      return "CORE FEEDTHRU";
    case LibraryCellMasterType::kCoreTieHigh:
      return "CORE TIEHIGH";
    case LibraryCellMasterType::kCoreTieLow:
      return "CORE TIELOW";
    case LibraryCellMasterType::kCoreSpacer:
      return "CORE SPACER";
    case LibraryCellMasterType::kCoreAntennaCell:
      return "CORE ANTENNACELL";
    case LibraryCellMasterType::kCoreWelltap:
      return "CORE WELLTAP";
    case LibraryCellMasterType::kEndcap:
      return "ENDCAP";
    case LibraryCellMasterType::kEndcapPre:
      return "ENDCAP PRE";
    case LibraryCellMasterType::kEndcapPost:
      return "ENDCAP POST";
    case LibraryCellMasterType::kEndcapTopLeft:
      return "ENDCAP TOPLEFT";
    case LibraryCellMasterType::kEndcapTopRight:
      return "ENDCAP TOPRIGHT";
    case LibraryCellMasterType::kEndcapBottomLeft:
      return "ENDCAP BOTTOMLEFT";
    case LibraryCellMasterType::kEndcapBottomRight:
      return "ENDCAP BOTTOMRIGHT";
    case LibraryCellMasterType::kNone:
      break;
  }
  throw std::invalid_argument("MACRO has no class");
}

std::string_view termDirectionName(LibraryMasterTermDirection value)
{
  switch (value) {
    case LibraryMasterTermDirection::kInput:
      return "INPUT";
    case LibraryMasterTermDirection::kOutput:
      return "OUTPUT";
    case LibraryMasterTermDirection::kOutputTriState:
      return "OUTPUT TRISTATE";
    case LibraryMasterTermDirection::kInOut:
      return "INOUT";
    case LibraryMasterTermDirection::kFeedThru:
      return "FEEDTHRU";
    case LibraryMasterTermDirection::kNone:
      break;
  }
  throw std::invalid_argument("PIN has no direction");
}

std::string_view termUseName(LibraryMasterTermUse value)
{
  switch (value) {
    case LibraryMasterTermUse::kSignal:
      return "SIGNAL";
    case LibraryMasterTermUse::kAnalog:
      return "ANALOG";
    case LibraryMasterTermUse::kPower:
      return "POWER";
    case LibraryMasterTermUse::kGround:
      return "GROUND";
    case LibraryMasterTermUse::kClock:
      return "CLOCK";
    case LibraryMasterTermUse::kTieOff:
      return "TIEOFF";
    case LibraryMasterTermUse::kScan:
      return "SCAN";
    case LibraryMasterTermUse::kReset:
      return "RESET";
    case LibraryMasterTermUse::kNone:
      break;
  }
  throw std::invalid_argument("PIN has no use");
}

std::string_view termShapeName(LibraryMasterTermShape value)
{
  switch (value) {
    case LibraryMasterTermShape::kAbutment:
      return "ABUTMENT";
    case LibraryMasterTermShape::kRing:
      return "RING";
    case LibraryMasterTermShape::kFeedThru:
      return "FEEDTHRU";
    case LibraryMasterTermShape::kNone:
      break;
  }
  throw std::invalid_argument("PIN has no shape");
}

std::string_view portClassName(LibraryMasterPortClass value)
{
  switch (value) {
    case LibraryMasterPortClass::kCore:
      return "CORE";
    case LibraryMasterPortClass::kBump:
      return "BUMP";
    case LibraryMasterPortClass::kNone:
      break;
  }
  throw std::invalid_argument("PORT has no class");
}

class LibraryWriter
{
 public:
  LibraryWriter(std::ostream& output, const TechStore& technology, const LibraryStore& library)
      : _output(output), _technology(technology), _library(library)
  {
    if (!technology.globalStorage().hasUnits()) {
      throw std::logic_error("Library LEF export requires technology UNITS DATABASE MICRONS");
    }
    const auto& units = technology.globalStorage().getUnits();
    if ((units.flags & TechGlobalUnitsFlag::kHasDatabaseUnitsPerMicron) == 0u || units.database_units_per_micron <= 0) {
      throw std::logic_error("Library LEF export requires technology UNITS DATABASE MICRONS");
    }
    _database_units_per_micron = units.database_units_per_micron;
  }

  void write()
  {
    _output << "VERSION 5.8 ;\n"
               "BUSBITCHARS \"[]\" ;\n"
               "DIVIDERCHAR \"/\" ;\n"
               "UNITS DATABASE MICRONS "
            << _database_units_per_micron << " ; END UNITS\n\n";
    writeSites();
    writeMasters();
    _output << "END LIBRARY\n";
    if (!_output) {
      throw std::runtime_error("failed to write canonical library LEF");
    }
  }

 private:
  [[nodiscard]] std::string distance(int64_t value) const { return lef_export_detail::distance(value, _database_units_per_micron); }

  [[nodiscard]] std::string_view layerName(TechLayerId id) const { return _technology.layerInfo(id).name; }

  [[nodiscard]] std::string_view viaName(TechViaMasterId id) const { return _technology.viaMasterStorage().viaMaster(id).name; }

  void writeSymmetry(bool x, bool y, bool r90, std::string_view indent)
  {
    if (!x && !y && !r90) {
      return;
    }
    _output << indent << "SYMMETRY";
    if (x) {
      _output << " X";
    }
    if (y) {
      _output << " Y";
    }
    if (r90) {
      _output << " R90";
    }
    _output << " ;\n";
  }

  void writeSites()
  {
    auto ids = _library.siteStorage().sites();
    std::sort(ids.begin(), ids.end(), [&](LibrarySiteId lhs, LibrarySiteId rhs) {
      return _library.siteStorage().site(lhs).name < _library.siteStorage().site(rhs).name;
    });
    for (const auto id : ids) {
      const auto& site = _library.siteStorage().site(id);
      if (site.overlap) {
        throw std::logic_error("Library SITE overlap extension has no canonical LEF representation");
      }
      if ((site.width < 0) != (site.height < 0)) {
        throw std::logic_error("Library SITE has an incomplete SIZE");
      }
      _output << "SITE " << site.name << "\n";
      if (site.site_class != LibrarySiteClass::kNone) {
        _output << "  CLASS " << siteClassName(site.site_class) << " ;\n";
      }
      writeSymmetry(site.symmetry_x, site.symmetry_y, site.symmetry_r90, "  ");
      if (site.width >= 0) {
        _output << "  SIZE " << distance(site.width) << " BY " << distance(site.height) << " ;\n";
      }
      _output << "END " << site.name << "\n\n";
    }
  }

  void writeMasters()
  {
    auto ids = _library.cellMasterStorage().cellMasters();
    std::sort(ids.begin(), ids.end(), [&](LibraryCellMasterId lhs, LibraryCellMasterId rhs) {
      return _library.cellMasterStorage().cellMaster(lhs).name < _library.cellMasterStorage().cellMaster(rhs).name;
    });
    for (const auto id : ids) {
      writeMaster(id);
    }
  }

  void writeMaster(LibraryCellMasterId id)
  {
    const auto& master = _library.cellMasterStorage().cellMaster(id);
    if (master.core_filler != (master.type == LibraryCellMasterType::kCoreSpacer)
        || master.pad_filler != (master.type == LibraryCellMasterType::kPadSpacer)) {
      throw std::logic_error("Library MACRO filler flags are inconsistent with CLASS");
    }

    _output << "MACRO " << master.name << "\n";
    if (master.type != LibraryCellMasterType::kNone) {
      _output << "  CLASS " << masterClassName(master.type) << " ;\n";
    }
    if (master.origin_x != 0 || master.origin_y != 0) {
      _output << "  ORIGIN " << distance(master.origin_x) << ' ' << distance(master.origin_y) << " ;\n";
    }
    if (master.width != 0 || master.height != 0) {
      _output << "  SIZE " << distance(master.width) << " BY " << distance(master.height) << " ;\n";
    }
    writeSymmetry(master.symmetry_x, master.symmetry_y, master.symmetry_r90, "  ");
    if (master.site.has_value()) {
      _output << "  SITE " << _library.siteStorage().site(*master.site).name << " ;\n";
    }
    for (const auto term : _library.masterTermStorage().masterTerms(id)) {
      writeTerm(term);
    }
    if (_library.cellMasterStorage().hasObs(id)) {
      writeObs(_library.cellMasterStorage().obs(id));
    }
    _output << "END " << master.name << "\n\n";
  }

  void writeTerm(LibraryMasterTermId id)
  {
    const auto& term = _library.masterTermStorage().masterTerm(id);
    _output << "  PIN " << term.name << "\n";
    if (term.direction != LibraryMasterTermDirection::kNone) {
      _output << "    DIRECTION " << termDirectionName(term.direction) << " ;\n";
    }
    if (term.use != LibraryMasterTermUse::kNone) {
      _output << "    USE " << termUseName(term.use) << " ;\n";
    }
    if (term.shape != LibraryMasterTermShape::kNone) {
      _output << "    SHAPE " << termShapeName(term.shape) << " ;\n";
    }
    for (const auto port : _library.masterPortStorage().masterPorts(id)) {
      writePort(_library.masterPortStorage().masterPort(port));
    }
    _output << "  END " << term.name << "\n";
  }

  void writePort(const LibraryMasterPort& port)
  {
    if (port.layer_clauses.empty() && port.vias.empty()) {
      throw std::logic_error("Library PORT has no geometry");
    }
    _output << "    PORT\n";
    if (port.port_class != LibraryMasterPortClass::kNone) {
      _output << "      CLASS " << portClassName(port.port_class) << " ;\n";
    }
    for (const auto& clause : port.layer_clauses) {
      _output << "      LAYER " << layerName(clause.layer) << " ;\n";
      writeGeometry(clause.geometry, "        ");
    }
    for (const auto& via : port.vias) {
      writeVia(via, "      ");
    }
    _output << "    END\n";
  }

  void writeObs(const LibraryMasterObs& obs)
  {
    _output << "  OBS\n";
    for (const auto& clause : obs.layer_clauses) {
      _output << "    LAYER " << layerName(clause.layer);
      if ((clause.flags & LibraryObsLayerFlag::kExceptPgNet) != 0u) {
        _output << " EXCEPTPGNET";
      }
      if ((clause.flags & LibraryObsLayerFlag::kHasSpacing) != 0u) {
        _output << " SPACING " << distance(clause.spacing);
      }
      if ((clause.flags & LibraryObsLayerFlag::kHasDesignRuleWidth) != 0u) {
        _output << " DESIGNRULEWIDTH " << distance(clause.design_rule_width);
      }
      _output << " ;\n";
      if ((clause.flags & LibraryObsLayerFlag::kHasPathWidth) != 0u) {
        _output << "      WIDTH " << distance(clause.path_width) << " ;\n";
      }
      writeGeometry(clause.geometry, "      ");
    }
    for (const auto& via : obs.vias) {
      writeVia(via, "    ");
    }
    _output << "  END\n";
  }

  void writeGeometry(GeometryHandle geometry, std::string_view indent)
  {
    for (const auto rect : _library.geometryPool().rectangles(geometry)) {
      _output << indent << "RECT " << distance(rect.ll_x) << ' ' << distance(rect.ll_y) << ' ' << distance(rect.ur_x) << ' '
              << distance(rect.ur_y) << " ;\n";
    }
    for (uint32_t index = 0; index < _library.geometryPool().polygonCount(geometry); ++index) {
      const auto points = _library.geometryPool().polygonPoints(geometry, index);
      if (points.size() < 3u) {
        throw std::logic_error("Library polygon has fewer than three points");
      }
      _output << indent << "POLYGON";
      for (const auto point : points) {
        _output << ' ' << distance(point.x) << ' ' << distance(point.y);
      }
      _output << " ;\n";
    }
  }

  void writeVia(const LibraryViaPlacement& via, std::string_view indent)
  {
    _output << indent << "VIA ( " << distance(via.origin.x) << ' ' << distance(via.origin.y) << " ) " << viaName(via.via) << " ;\n";
  }

  std::ostream& _output;
  const TechStore& _technology;
  const LibraryStore& _library;
  int32_t _database_units_per_micron = 0;
};

}  // namespace

void LefLibraryExporter::write(std::ostream& output, const TechStore& technology, const LibraryStore& library)
{
  LibraryWriter(output, technology, library).write();
}

void LefLibraryExporter::write(const std::filesystem::path& path, const TechStore& technology, const LibraryStore& library)
{
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("cannot open Library LEF export file: " + path.string());
  }
  write(output, technology, library);
}

}  // namespace eccdb

#include "idb/IdbLibraryImporter.h"

#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "IdbLayout.h"
#include "IdbObs.h"
#include "idb/IdbTechImporter.h"

namespace eccdb {
namespace {

LibrarySiteClass siteClass(::idb::IdbSiteClass source)
{
  switch (source) {
    case ::idb::IdbSiteClass::kCore:
      return LibrarySiteClass::kCore;
    case ::idb::IdbSiteClass::kPad:
      return LibrarySiteClass::kPad;
    case ::idb::IdbSiteClass::kCorner:
      return LibrarySiteClass::kCorner;
    case ::idb::IdbSiteClass::kNone:
    case ::idb::IdbSiteClass::kMax:
      return LibrarySiteClass::kNone;
  }
  return LibrarySiteClass::kNone;
}

LibraryCellMasterType cellMasterType(::idb::CellMasterType source)
{
  switch (source) {
    case ::idb::CellMasterType::kCover:
      return LibraryCellMasterType::kCover;
    case ::idb::CellMasterType::kCoverBump:
      return LibraryCellMasterType::kCoverBump;
    case ::idb::CellMasterType::kRing:
      return LibraryCellMasterType::kRing;
    case ::idb::CellMasterType::kBlock:
      return LibraryCellMasterType::kBlock;
    case ::idb::CellMasterType::kBlockBlackbox:
      return LibraryCellMasterType::kBlockBlackbox;
    case ::idb::CellMasterType::kBLockSoft:
      return LibraryCellMasterType::kBlockSoft;
    case ::idb::CellMasterType::kPad:
      return LibraryCellMasterType::kPad;
    case ::idb::CellMasterType::kPadInput:
      return LibraryCellMasterType::kPadInput;
    case ::idb::CellMasterType::kPadOutput:
      return LibraryCellMasterType::kPadOutput;
    case ::idb::CellMasterType::kPadInOut:
      return LibraryCellMasterType::kPadInOut;
    case ::idb::CellMasterType::kPadPower:
      return LibraryCellMasterType::kPadPower;
    case ::idb::CellMasterType::kPadSpacer:
      return LibraryCellMasterType::kPadSpacer;
    case ::idb::CellMasterType::kPadAreaIO:
      return LibraryCellMasterType::kPadAreaIo;
    case ::idb::CellMasterType::kCore:
      return LibraryCellMasterType::kCore;
    case ::idb::CellMasterType::kCoreFeedThru:
      return LibraryCellMasterType::kCoreFeedThru;
    case ::idb::CellMasterType::kCoreTieHigh:
      return LibraryCellMasterType::kCoreTieHigh;
    case ::idb::CellMasterType::kCoreTieLow:
      return LibraryCellMasterType::kCoreTieLow;
    case ::idb::CellMasterType::kCoreSpacer:
      return LibraryCellMasterType::kCoreSpacer;
    case ::idb::CellMasterType::kCoreAntenaCell:
      return LibraryCellMasterType::kCoreAntennaCell;
    case ::idb::CellMasterType::kCoreWelltap:
      return LibraryCellMasterType::kCoreWelltap;
    case ::idb::CellMasterType::kEndcap:
      return LibraryCellMasterType::kEndcap;
    case ::idb::CellMasterType::kEndcapPre:
      return LibraryCellMasterType::kEndcapPre;
    case ::idb::CellMasterType::kEndcapPost:
      return LibraryCellMasterType::kEndcapPost;
    case ::idb::CellMasterType::kEndcapTopLeft:
      return LibraryCellMasterType::kEndcapTopLeft;
    case ::idb::CellMasterType::kEndcapTopRight:
      return LibraryCellMasterType::kEndcapTopRight;
    case ::idb::CellMasterType::kEndcapBottomLeft:
      return LibraryCellMasterType::kEndcapBottomLeft;
    case ::idb::CellMasterType::kEndcapBottomRight:
      return LibraryCellMasterType::kEndcapBottomRight;
    case ::idb::CellMasterType::kNone:
    case ::idb::CellMasterType::kMax:
      return LibraryCellMasterType::kNone;
  }
  return LibraryCellMasterType::kNone;
}

LibraryMasterTermDirection termDirection(::idb::IdbConnectDirection source)
{
  switch (source) {
    case ::idb::IdbConnectDirection::kInput:
      return LibraryMasterTermDirection::kInput;
    case ::idb::IdbConnectDirection::kOutput:
      return LibraryMasterTermDirection::kOutput;
    case ::idb::IdbConnectDirection::kOutputTriState:
      return LibraryMasterTermDirection::kOutputTriState;
    case ::idb::IdbConnectDirection::kInOut:
      return LibraryMasterTermDirection::kInOut;
    case ::idb::IdbConnectDirection::kFeedThru:
      return LibraryMasterTermDirection::kFeedThru;
    case ::idb::IdbConnectDirection::kNone:
    case ::idb::IdbConnectDirection::kMax:
      return LibraryMasterTermDirection::kNone;
  }
  return LibraryMasterTermDirection::kNone;
}

LibraryMasterTermUse termUse(::idb::IdbConnectType source)
{
  switch (source) {
    case ::idb::IdbConnectType::kSignal:
      return LibraryMasterTermUse::kSignal;
    case ::idb::IdbConnectType::kAnalog:
      return LibraryMasterTermUse::kAnalog;
    case ::idb::IdbConnectType::kPower:
      return LibraryMasterTermUse::kPower;
    case ::idb::IdbConnectType::kGround:
      return LibraryMasterTermUse::kGround;
    case ::idb::IdbConnectType::kClock:
      return LibraryMasterTermUse::kClock;
    case ::idb::IdbConnectType::kTieOff:
      return LibraryMasterTermUse::kTieOff;
    case ::idb::IdbConnectType::kScan:
      return LibraryMasterTermUse::kScan;
    case ::idb::IdbConnectType::kReset:
      return LibraryMasterTermUse::kReset;
    case ::idb::IdbConnectType::kNone:
    case ::idb::IdbConnectType::kMax:
      return LibraryMasterTermUse::kNone;
  }
  return LibraryMasterTermUse::kNone;
}

LibraryMasterTermShape termShape(::idb::IdbTermShape source)
{
  switch (source) {
    case ::idb::IdbTermShape::kAbutment:
      return LibraryMasterTermShape::kAbutment;
    case ::idb::IdbTermShape::kRing:
      return LibraryMasterTermShape::kRing;
    case ::idb::IdbTermShape::kFeedThru:
      return LibraryMasterTermShape::kFeedThru;
    case ::idb::IdbTermShape::kNone:
    case ::idb::IdbTermShape::kMax:
      return LibraryMasterTermShape::kNone;
  }
  return LibraryMasterTermShape::kNone;
}

LibraryMasterPortClass portClass(::idb::IdbPortClass source)
{
  switch (source) {
    case ::idb::IdbPortClass::kCore:
      return LibraryMasterPortClass::kCore;
    case ::idb::IdbPortClass::kBump:
      return LibraryMasterPortClass::kBump;
    case ::idb::IdbPortClass::kNone:
    case ::idb::IdbPortClass::kMax:
      return LibraryMasterPortClass::kNone;
  }
  return LibraryMasterPortClass::kNone;
}

int32_t checkedCoordinate(int64_t value, const char* field)
{
  if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()) {
    throw std::runtime_error(std::string(field) + " is outside int32 DBU range");
  }
  return static_cast<int32_t>(value);
}

Rect rect(::idb::IdbRect& source, int64_t offset_x = 0, int64_t offset_y = 0)
{
  return Rect{.ll_x = checkedCoordinate(static_cast<int64_t>(source.get_low_x()) + offset_x, "legacy library rectangle X"),
              .ll_y = checkedCoordinate(static_cast<int64_t>(source.get_low_y()) + offset_y, "legacy library rectangle Y"),
              .ur_x = checkedCoordinate(static_cast<int64_t>(source.get_high_x()) + offset_x, "legacy library rectangle X"),
              .ur_y = checkedCoordinate(static_cast<int64_t>(source.get_high_y()) + offset_y, "legacy library rectangle Y")};
}

}  // namespace

void IdbLibraryImporter::import(::idb::IdbLayout& source)
{
  if (_imported) {
    throw std::logic_error("legacy iDB library importer is one-shot");
  }
  _imported = true;

  for (auto* source_site : source.get_sites()->get_site_list()) {
    if (source_site == nullptr) {
      continue;
    }

    LibrarySite site{.name = source_site->get_name(),
                     .width = source_site->get_width(),
                     .height = source_site->get_height(),
                     .overlap = source_site->is_overlap(),
                     .site_class = siteClass(source_site->get_site_class())};
    switch (source_site->get_symmetry()) {
      case ::idb::IdbSymmetry::kX:
        site.symmetry_x = true;
        break;
      case ::idb::IdbSymmetry::kY:
        site.symmetry_y = true;
        break;
      case ::idb::IdbSymmetry::kR90:
        site.symmetry_r90 = true;
        break;
      case ::idb::IdbSymmetry::kNone:
      case ::idb::IdbSymmetry::kMax:
        break;
    }

    const auto site_id = _database.siteStorage().createSite(std::move(site));
    _site_ids.emplace(source_site, site_id);
  }

  for (auto* source_master : source.get_cell_master_list()->get_cell_master()) {
    if (source_master == nullptr) {
      continue;
    }

    LibraryCellMaster master{.name = source_master->get_name(),
                             .type = cellMasterType(source_master->get_type()),
                             .symmetry_x = source_master->is_symmetry_x(),
                             .symmetry_y = source_master->is_symmetry_y(),
                             .symmetry_r90 = source_master->is_symmetry_R90(),
                             .core_filler = source_master->is_core_filler(),
                             .pad_filler = source_master->is_pad_filler(),
                             .origin_x = source_master->get_origin_x(),
                             .origin_y = source_master->get_origin_y(),
                             .width = source_master->get_width(),
                             .height = source_master->get_height()};
    if (const auto* source_site = source_master->get_site(); source_site != nullptr) {
      const auto found = _site_ids.find(source_site);
      if (found == _site_ids.end()) {
        throw std::runtime_error("cell master references a legacy SITE that was not imported");
      }
      master.site = found->second;
    }

    const auto master_id = _database.cellMasterStorage().createCellMaster(std::move(master));
    std::unordered_map<std::string, LibraryMasterTermId> term_ids;

    for (auto* source_term : source_master->get_term_list()) {
      if (source_term == nullptr) {
        continue;
      }

      const LibraryMasterTerm imported_term{.name = source_term->get_name(),
                                            .direction = termDirection(source_term->get_direction()),
                                            .use = termUse(source_term->get_type()),
                                            .shape = termShape(source_term->get_shape())};
      LibraryMasterTermId term_id;
      const auto found_term = term_ids.find(imported_term.name);
      if (found_term == term_ids.end()) {
        term_id = _database.masterTermStorage().createMasterTerm(master_id, imported_term);
        term_ids.emplace(imported_term.name, term_id);
      } else {
        term_id = found_term->second;
        const auto& existing = _database.masterTermStorage().masterTerm(term_id);
        if (existing.direction != imported_term.direction || existing.use != imported_term.use || existing.shape != imported_term.shape) {
          throw std::runtime_error("duplicate legacy PIN has conflicting attributes: " + source_master->get_name() + "/"
                                   + imported_term.name);
        }
      }

      for (auto* source_port : source_term->get_port_list()) {
        if (source_port == nullptr) {
          continue;
        }

        LibraryMasterPortInput port{.port_class = portClass(source_port->get_port_class())};
        for (auto* source_shape : source_port->get_layer_shape()) {
          if (source_shape == nullptr || source_shape->get_layer() == nullptr) {
            continue;
          }
          const auto layer = _tech_importer.layerId(source_shape->get_layer());
          LibraryPortLayerGeometryInput clause{.layer = layer};
          for (auto* source_rect : source_shape->get_rect_list()) {
            if (source_rect != nullptr) {
              const auto imported = rect(*source_rect, -source_master->get_origin_x(), -source_master->get_origin_y()).normalized();
              if (imported.hasArea()) {
                clause.geometry.rects.push_back(imported);
              }
            }
          }
          if (!clause.geometry.rects.empty()) {
            port.layer_clauses.push_back(std::move(clause));
          }
        }
        for (auto* source_via : source_port->get_via_list()) {
          if (source_via == nullptr || source_via->get_coordinate() == nullptr) {
            continue;
          }
          port.vias.push_back(LibraryViaPlacement{.via = _tech_importer.viaMasterId(source_via->get_name()),
                                                 .origin = Point{source_via->get_coordinate()->get_x(),
                                                                 source_via->get_coordinate()->get_y()}});
        }
        static_cast<void>(_database.masterPortStorage().createMasterPort(term_id, std::move(port)));
      }
    }

    LibraryMasterObsInput obs;
    for (auto* source_obs : source_master->get_obs_list()) {
      if (source_obs == nullptr) {
        continue;
      }
      for (auto* source_obs_layer : source_obs->get_obs_layer_list()) {
        if (source_obs_layer == nullptr || source_obs_layer->get_shape() == nullptr
            || source_obs_layer->get_shape()->get_layer() == nullptr) {
          continue;
        }

        auto* source_shape = source_obs_layer->get_shape();
        LibraryObsLayerClauseInput clause{.layer = _tech_importer.layerId(source_shape->get_layer())};
        for (auto* source_rect : source_shape->get_rect_list()) {
          if (source_rect != nullptr) {
            const auto imported = rect(*source_rect, -source_master->get_origin_x(), -source_master->get_origin_y()).normalized();
            if (imported.hasArea()) {
              clause.geometry.rects.push_back(imported);
            }
          }
        }
        if (!clause.geometry.rects.empty()) {
          obs.layer_clauses.push_back(std::move(clause));
        }
      }
    }
    if (!obs.layer_clauses.empty() || !obs.vias.empty()) {
      _database.cellMasterStorage().setObs(master_id, std::move(obs));
    }
  }
}

}  // namespace eccdb

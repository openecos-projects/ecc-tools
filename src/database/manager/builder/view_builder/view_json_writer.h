// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************

#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "IdbGeometry.h"
#include "json.hpp"
#include "view_geometry_transform.h"

namespace idb {

class IdbBlockage;
class IdbCellMaster;
class IdbDefService;
class IdbDesign;
class IdbFill;
class IdbInstance;
class IdbLayer;
class IdbLayerShape;
class IdbLayout;
class IdbNet;
class IdbPin;
class IdbRect;
class IdbRegularWireSegment;
class IdbSpecialNet;
class IdbSpecialWireSegment;
class IdbTerm;
class IdbVia;
class IdbViaMaster;

using ViewJson = nlohmann::ordered_json;

class ViewJsonWriter
{
 public:
  explicit ViewJsonWriter(IdbDefService* def_service);

  bool write(const std::string& output_dir);

  ViewJson toPointJson(const ViewPoint& point) const;
  ViewJson toRectJson(const ViewRect& rect) const;
  ViewJson toLayerShapeJson(const ViewLayerShape& shape) const;
  ViewLayerShape toViewLayerShape(const IdbLayerShape* shape) const;
  ViewJson toLayerShapeJson(const IdbLayerShape* shape) const;
  ViewJson toPlacedLayerShapeJson(const IdbLayerShape* local_shape, const PlacedTransform& transform) const;

 private:
  struct PartSummary
  {
    std::string file;
    ViewRect bbox;
    int count = 0;
    std::vector<int> layers;
  };

  struct SpatialEntry
  {
    std::string object_kind;
    std::string file;
    int id = -1;
    ViewRect bbox;
    std::vector<int> layers;
  };

  bool prepareOutputDir(const std::filesystem::path& output_dir) const;
  bool buildDictionaries();

  bool writeManifest();
  bool writeMeta();
  bool writeLayers();
  bool writeVias();
  bool writeCellMasters();
  bool writeSites();
  bool writeDie();
  bool writeRows();
  bool writeTracks();
  bool writeGCellGrids();
  bool writeInstances();
  bool writeIoPins();
  bool writeRegularNets();
  bool writeRegularWires();
  bool writeSpecialNets();
  bool writeSpecialWires();
  bool writeBlockages();
  bool writeFills();
  bool writeRegions();
  bool writeObjectIndexes();
  bool writeLayerIndex();
  bool writeSpatialIndex();
  bool writeEditOverlay();

  bool writeNameIndex(const std::string& relative_path, const std::string& kind,
                      const std::unordered_map<std::string, int>& name_to_id) const;
  bool writeJsonFile(const std::string& relative_path, const ViewJson& json) const;
  bool validateDenseData(const std::string& relative_path, const ViewJson& json) const;

  ViewJson makeFileHeader(const std::string& kind, int count) const;
  ViewJson makeNameToIdJson(const std::unordered_map<std::string, int>& name_to_id) const;
  ViewJson makeObjectIndexJson(const std::string& object_kind, const std::string& file) const;

  ViewJson toRectJson(const IdbRect* rect) const;
  ViewJson toRectJson(const IdbRect& rect) const;
  ViewJson toPointJson(const idb::IdbCoordinate<int32_t>* coord) const;
  ViewRect toViewRect(const IdbRect* rect) const;
  ViewRect toViewRect(const IdbRect& rect) const;
  ViewRect instanceBBox(const IdbInstance* inst) const;
  ViewRect wireBBox(const std::vector<ViewPoint>& points, int32_t width) const;
  ViewRect viaBBox(const IdbVia* via) const;
  ViewRect viaBBox(const IdbViaMaster* via_master, const ViewPoint& origin) const;
  std::vector<int> viaLayerIds(const IdbVia* via) const;
  std::vector<int> viaLayerIds(const IdbViaMaster* via_master) const;
  ViewJson toViaPlacementJson(const IdbVia* via) const;

  ViewJson makeLayerPartJson(const PartSummary& summary) const;
  void addLayerPart(int layer_id, const std::string& object_kind, const PartSummary& summary);
  void registerSpatialEntry(const std::string& object_kind, const std::string& file, int id, const ViewRect& bbox,
                            const std::vector<int>& layers);
  void expandBBox(ViewRect& bbox, const ViewRect& rect) const;
  bool isBBoxValid(const ViewRect& bbox) const;

  std::string orientName(IdbOrient orient) const;
  std::string layerTypeName(IdbLayer* layer) const;
  std::string layerDirectionName(IdbLayer* layer) const;
  std::string cellMasterTypeName(IdbCellMaster* master) const;
  std::string instanceTypeName(IdbInstance* inst) const;
  std::string placementStatusName(IdbPlacementStatus status) const;
  std::string connectDirectionName(IdbTerm* term) const;
  std::string connectTypeName(IdbConnectType type) const;
  std::string wireStateName(IdbWiringStatement state) const;
  std::string wireShapeName(IdbWireShapeType shape) const;
  std::string trackDirectionName(IdbTrackDirection direction) const;

  int layerId(const IdbLayer* layer) const;
  int viaMasterId(const IdbViaMaster* via_master) const;
  int cellMasterId(const IdbCellMaster* master) const;
  int instanceId(const IdbInstance* inst) const;
  int ioPinId(const IdbPin* pin) const;
  int regularNetId(const IdbNet* net) const;
  int specialNetId(const IdbSpecialNet* net) const;

 private:
  IdbDefService* _def_service = nullptr;
  IdbLayout* _layout = nullptr;
  IdbDesign* _design = nullptr;
  std::filesystem::path _output_dir;

  std::unordered_map<const IdbLayer*, int> _layer_id_map;
  std::unordered_map<const IdbViaMaster*, int> _via_master_id_map;
  std::unordered_map<const IdbCellMaster*, int> _cell_master_id_map;
  std::unordered_map<const IdbInstance*, int> _instance_id_map;
  std::unordered_map<const IdbPin*, int> _io_pin_id_map;
  std::unordered_map<const IdbNet*, int> _regular_net_id_map;
  std::unordered_map<const IdbSpecialNet*, int> _special_net_id_map;

  std::unordered_map<std::string, int> _layer_name_to_id;
  std::unordered_map<std::string, int> _via_master_name_to_id;
  std::unordered_map<std::string, int> _cell_master_name_to_id;
  std::unordered_map<std::string, int> _instance_name_to_id;
  std::unordered_map<std::string, int> _io_pin_name_to_id;
  std::unordered_map<std::string, int> _regular_net_name_to_id;
  std::unordered_map<std::string, int> _special_net_name_to_id;

  std::unordered_map<int, std::unordered_map<std::string, std::vector<PartSummary>>> _layer_parts;
  std::vector<SpatialEntry> _spatial_entries;
};

}  // namespace idb

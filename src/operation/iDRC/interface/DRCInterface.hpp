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
#pragma once

#include <any>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../../../database/interaction/RT_DRC/ids.hpp"

#if 1  // 前向声明

namespace idb {
class IdbNet;
class IdbLayerRouting;
class IdbLayerCut;
enum class IdbLayerDirection : uint8_t;
}  // namespace idb

namespace eccdb {
class DesignStore;
class TechStore;
class LibraryStore;
class DatabaseContext;
}  // namespace eccdb

namespace idrc {
class RoutingLayer;
class CutLayer;
class DRCShape;
class Database;
enum class Direction;
enum class ViolationType;
}  // namespace idrc

#endif

namespace idrc {

#define DRCI (idrc::DRCInterface::getInst())

class DRCInterface
{
 public:
  static DRCInterface& getInst();
  static void destroyInst();

#if 1  // 外部调用DRC的API

#if 1  // iDRC
  void initDRC(std::map<std::string, std::any> config_map, bool enable_quiet = true);
  void runDRC();
  void checkDef();
  [[nodiscard]] const std::map<std::string, std::size_t>& lastViolationCounts() const noexcept;
  void destroyDRC();
  // Releases a partially initialized run without publishing DRC output.
  void abortDRC();
  bool saveDRC(std::string path);
  std::vector<ids::Violation> getViolationList(const std::vector<ids::Shape>& ids_env_shape_list, const std::vector<ids::Shape>& ids_result_shape_list,
                                               const std::set<std::string>& ids_check_type_set, const std::vector<ids::Shape>& ids_check_region_list);
#endif

#endif

#if 1  // DRC调用外部的API

#if 1  // TopData

#if 1  // input
  void setDesignSource(eccdb::DesignStore* design, eccdb::TechStore* tech,
                       eccdb::LibraryStore* library);
  // Borrow the caller-owned Context for the lifetime of a native run. Passing
  // nullptr detaches it and clears the native source selection.
  void setDesignSource(eccdb::DatabaseContext* context);
  void setDesignSource(eccdb::DatabaseContext& context);
  void input(std::map<std::string, std::any>& config_map);
  void wrapConfig(std::map<std::string, std::any>& config_map);
  void wrapDatabase();
  void wrapDatabaseFromIdb();
  void wrapDatabaseFromEnTT();
  std::string compareWrappedDatabase(Database& left, Database& right);
  void wrapDBInfo();
  void wrapMicronDBU();
  void wrapManufactureGrid();
  void wrapDie();
  void wrapDesignRule();
  void wrapLayerList();
  void wrapTrackAxis(RoutingLayer& routing_layer, idb::IdbLayerRouting* idb_layer);
  void wrapRoutingDesignRule(RoutingLayer& routing_layer, idb::IdbLayerRouting* idb_layer);
  void wrapCutDesignRule(CutLayer& cut_layer, idb::IdbLayerCut* idb_layer);
  void wrapLayerInfo();
  Direction getDRCDirectionByDB(idb::IdbLayerDirection idb_direction);
#endif

#if 1  // output
  void output();
#endif

#endif

#if 1  // check
  std::vector<ids::Shape> buildEnvShapeList();
  std::vector<ids::Shape> buildEnvShapeListFromEnTT(std::set<size_t>* obs_shape_idx_set = nullptr);
  bool isSkipping(idb::IdbNet* idb_net);
  std::vector<ids::Shape> buildResultShapeList();
  std::vector<ids::Shape> buildResultShapeListFromEnTT();
  std::string getNetName(int32_t net_idx);
  // IDB-only shape conversion path. EccDB keeps the ids::Shape builders above
  // as its public adapter contract; these helpers avoid a return-type-only
  // overload collision with that contract.
  std::vector<DRCShape> buildEnvDrcShapeList();
  std::vector<DRCShape> buildResultDrcShapeList();
  void printSummary(std::map<std::string, std::vector<ids::Violation>>& type_violation_map);
  void outputViolationJson(std::map<std::string, std::vector<ids::Violation>>& type_violation_map);
  void outputViolationFile(std::map<std::string, std::vector<ids::Violation>>& type_violation_map);
  void outputTofeature(std::map<std::string, std::vector<ids::Violation>>& type_violation_map);
  DRCShape convertToDRCShape(const ids::Shape& ids_shape);
#endif

#endif

 private:
  static DRCInterface* _drc_interface_instance;
  eccdb::DesignStore* _design = nullptr;
  eccdb::TechStore* _tech = nullptr;
  eccdb::LibraryStore* _library = nullptr;
  eccdb::DatabaseContext* _database_context = nullptr;
  bool _drc_initialized = false;
  std::map<std::string, std::size_t> _last_violation_counts;
  // iDRC uses regular-then-special net ordinals. Build this snapshot
  // before parallel queries and keep it read-only for the initialized run.
  // A changed source/netlist is picked up by the next initDRC, not per box.
  int32_t _eccdb_regular_net_count = 0;
  std::vector<std::string> _eccdb_net_names;

  DRCInterface() = default;
  DRCInterface(const DRCInterface& other) = delete;
  DRCInterface(DRCInterface&& other) = delete;
  ~DRCInterface() = default;
  DRCInterface& operator=(const DRCInterface& other) = delete;
  DRCInterface& operator=(DRCInterface&& other) = delete;
  // function
  void refreshEccdbNetIndex();
  void clearEccdbNetIndex();
  void finishDRC(bool write_output);
  std::vector<ids::Violation> getViolationList(const std::vector<ids::Shape>& ids_env_shape_list, const std::vector<ids::Shape>& ids_result_shape_list,
                                               const std::set<std::string>& ids_check_type_set, const std::vector<ids::Shape>& ids_check_region_list,
                                               const std::set<size_t>& obs_shape_idx_set);
  std::vector<ids::Violation> getViolationList(std::vector<DRCShape> drc_env_shape_list, std::vector<DRCShape> drc_result_shape_list,
                                               std::set<ViolationType> drc_check_type_set, std::vector<DRCShape> drc_check_region_list);
};

}  // namespace idrc

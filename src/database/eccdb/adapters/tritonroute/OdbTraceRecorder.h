// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/json.hpp>

namespace odb {
class dbBlock;
class dbBPin;
class dbBTerm;
class dbDatabase;
class dbGCellGrid;
class dbITerm;
class dbInst;
class dbMaster;
class dbMPin;
class dbNet;
class dbOrientType;
class dbTech;
class dbVia;
}  // namespace odb

namespace eccdb::tritonroute {

struct OdbTraceInputPaths
{
  std::filesystem::path lef;
  std::filesystem::path def;
  std::filesystem::path guides;
};

// Captures semantic groups at explicit TritonRoute ODB business call sites.
// This is intentionally not an automatic interception of every ODB getter.
class OdbTraceRecorder
{
 public:
  OdbTraceRecorder(const std::filesystem::path& path,
                   odb::dbBlock* block,
                   const OdbTraceInputPaths& input_paths);
  ~OdbTraceRecorder();

  OdbTraceRecorder(const OdbTraceRecorder&) = delete;
  OdbTraceRecorder& operator=(const OdbTraceRecorder&) = delete;

  static OdbTraceRecorder* active() { return active_; }

  void recordReadTechHeader(std::string_view source, odb::dbTech* tech);
  void recordReadTechLayers(std::string_view source, odb::dbTech* tech);
  void recordReadTechVias(std::string_view source, odb::dbTech* tech);
  void recordReadDesignVias(std::string_view source, odb::dbBlock* block);
  void recordReadViaGenerateRules(std::string_view source, odb::dbTech* tech);
  void recordReadNdrs(std::string_view source, odb::dbDatabase* database);
  void recordReadLibrary(std::string_view source, odb::dbDatabase* database);
  void recordReadBlockages(std::string_view source, odb::dbBlock* block);
  void recordReadBTerms(std::string_view source, odb::dbBlock* block);
  void recordReadBTermPinAccess(std::string_view source,
                                odb::dbBTerm* term,
                                odb::dbBPin* pin);
  void recordReadMasterPinAccess(std::string_view source, odb::dbMPin* pin);
  void recordReadPreferredPinAccess(std::string_view source,
                                    odb::dbInst* instance,
                                    odb::dbITerm* term);
  void recordReadSpecialWire(std::string_view source, odb::dbNet* net);
  void recordReadGuides(std::string_view source, odb::dbNet* net);
  void recordReadDie(std::string_view source, odb::dbBlock* block);
  void recordScanTracks(std::string_view source, odb::dbBlock* block);
  void recordScanInstances(std::string_view source, odb::dbBlock* block);
  void recordScanNets(std::string_view source, odb::dbBlock* block);
  void recordReadInstance(std::string_view source,
                          odb::dbInst* instance,
                          std::string_view name,
                          std::string_view master,
                          int x,
                          int y,
                          const odb::dbOrientType& orient);
  void recordReadInstancePins(std::string_view source, odb::dbInst* instance);
  void recordReadNetAttributes(std::string_view source, odb::dbNet* net);
  void recordNetPins(std::string_view source, odb::dbNet* net);
  void recordReadWire(std::string_view source, odb::dbNet* net);
  void recordReadGCellGrid(std::string_view source, odb::dbGCellGrid* grid);
  void recordFindInstance(std::string_view source,
                          std::string_view query,
                          odb::dbInst* instance);
  void recordFindPin(std::string_view source,
                     odb::dbInst* instance,
                     std::string_view query,
                     odb::dbITerm* term);
  void recordDefineDesignVia(std::string_view source, odb::dbVia* via);
  void recordClearWire(std::string_view source, odb::dbNet* net);
  void recordReplaceWire(std::string_view source, odb::dbNet* net);
  void recordAppendTrackAssignment(std::string_view source,
                                   odb::dbNet* net,
                                   boost::json::array appended);
  void recordReplaceMasterPinAccess(std::string_view source,
                                    odb::dbMaster* master,
                                    const std::vector<int>& groups);
  void recordReplaceInstancePinAccess(std::string_view source,
                                      odb::dbInst* instance);
  void recordAppendBTermPinAccess(std::string_view source,
                                  odb::dbBTerm* term,
                                  odb::dbBPin* pin,
                                  std::size_t previous_count);
  void recordReplaceGuides(std::string_view source,
                           odb::dbNet* net,
                           boost::json::array guides);
  void recordAppendBTermStackWire(std::string_view source,
                                  odb::dbNet* net,
                                  std::string_view layer,
                                  int x,
                                  int y,
                                  const std::vector<std::string>& vias);
  void finish();

 private:
  void record(boost::json::object event);

  std::ofstream file_;
  // (true, name) is a tech VIA; (false, name) is a block VIA. The set starts
  // with input definitions and grows only after their trace event is emitted.
  std::set<std::pair<bool, std::string>> known_vias_;
  std::size_t events_ = 0;
  bool finished_ = false;

  static OdbTraceRecorder* active_;
};

}  // namespace eccdb::tritonroute

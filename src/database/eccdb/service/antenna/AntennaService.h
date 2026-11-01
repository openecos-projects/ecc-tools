// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <string_view>

#include "context/DatabaseContext.h"
#include "tech/common/TechLayerAntennaComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "library/common/LibraryTypes.h"
#include "tech/common/TechLayerIds.h"

namespace eccdb {

class DesignStore;
class LibraryStore;
class TechStore;

// Cross-domain antenna metadata facade.  The components remain owned by their
// canonical Library/Tech/Design entity; this service only coordinates lookup,
// replacement and change notification so adaptors do not reach into EnTT.
class AntennaService
{
 public:
  explicit AntennaService(DatabaseContext& context) noexcept;
  explicit AntennaService(DesignStore& design) noexcept;
  explicit AntennaService(const DesignStore& design) noexcept;
  explicit AntennaService(LibraryStore& library) noexcept;
  explicit AntennaService(const LibraryStore& library) noexcept;
  explicit AntennaService(TechStore& technology) noexcept;
  explicit AntennaService(const TechStore& technology) noexcept;

  [[nodiscard]] const LibraryMasterTermAntenna* masterTermAntenna(LibraryMasterTermId term) const noexcept;
  void setMasterTermAntenna(LibraryMasterTermId term, LibraryMasterTermAntenna value);
  void clearMasterTermAntenna(LibraryMasterTermId term) noexcept;

  // OpenDB-style access to one oxide slot.  An empty oxide name and OXIDE1
  // address the same default slot; the supplied spelling is retained when a
  // model is written.
  [[nodiscard]] const AntennaModel* masterTermAntennaModel(LibraryMasterTermId term,
                                                            std::string_view oxide) const noexcept;
  void setMasterTermAntennaModel(LibraryMasterTermId term, AntennaModel value);
  bool clearMasterTermAntennaModel(LibraryMasterTermId term, std::string_view oxide) noexcept;

  [[nodiscard]] const TechLayerAntenna* layerAntenna(TechLayerId layer) const noexcept;
  void setLayerAntenna(TechLayerId layer, TechLayerAntenna value);
  void clearLayerAntenna(TechLayerId layer) noexcept;

  [[nodiscard]] const AntennaModel* layerAntennaModel(TechLayerId layer,
                                                      std::string_view oxide) const noexcept;
  void setLayerAntennaModel(TechLayerId layer, AntennaModel value);
  bool clearLayerAntennaModel(TechLayerId layer, std::string_view oxide) noexcept;

  [[nodiscard]] const DesignBlockTermAntenna* blockTermAntenna(DesignBlockTermId term) const noexcept;
  void setBlockTermAntenna(DesignBlockTermId term, DesignBlockTermAntenna value);
  void clearBlockTermAntenna(DesignBlockTermId term) noexcept;

  [[nodiscard]] const AntennaModel* blockTermAntennaModel(DesignBlockTermId term,
                                                          std::string_view oxide) const noexcept;
  void setBlockTermAntennaModel(DesignBlockTermId term, AntennaModel value);
  bool clearBlockTermAntennaModel(DesignBlockTermId term, std::string_view oxide) noexcept;

 private:
  [[nodiscard]] LibraryMasterTermAntenna& ensureMasterTermAntenna(LibraryMasterTermId term);
  [[nodiscard]] TechLayerAntenna& ensureLayerAntenna(TechLayerId layer);
  [[nodiscard]] DesignBlockTermAntenna& ensureBlockTermAntenna(DesignBlockTermId term);

  void notify() noexcept;

  DatabaseContext* _context = nullptr;
  DesignStore* _design = nullptr;
  const DesignStore* _const_design = nullptr;
  LibraryStore* _library = nullptr;
  const LibraryStore* _const_library = nullptr;
  TechStore* _technology = nullptr;
  const TechStore* _const_technology = nullptr;
};

}  // namespace eccdb

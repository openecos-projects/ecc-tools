// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include "AFComParam.hpp"
#include "ACViolation.hpp"
#include "AntennaResult.hpp"
#include "IdbDesign.h"
#include "IdbInstance.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "RoutingContext.hpp"
#include "WireEnvIndex.hpp"

namespace izh {

enum class AFFixKind
{
  kNone,
  kHopUp,
  kJog,
  kDiode,
  kBoth,
  kSkip
};

class AntennaFixer
{
 public:
  AFFixKind classify(const ACViolation& violation, const RoutingContext& ctx) const;
  bool hopUpPermitted(const ACViolation& violation, const RoutingContext& ctx) const;
  bool applySelected(idb::IdbDesign* design, idb::IdbNet* net, const ACViolation& violation, const RoutingContext& ctx, WireEnvIndex& index,
                     AFIterStat& stat, bool& logged_no_cell);
  bool applyHopUp(idb::IdbDesign* design, idb::IdbNet* net, const ACViolation& violation, const RoutingContext& ctx, WireEnvIndex& index,
                  bool allow_jog, AFIterStat& stat);
  bool applyDiode(idb::IdbDesign* design, idb::IdbNet* net, const ACViolation& violation, const RoutingContext& ctx, WireEnvIndex& index,
                  AFIterStat& stat, bool& logged_no_cell);

 private:
  idb::IdbPin* findVictimPin(idb::IdbNet* net, const ACViolation& violation) const;
  bool pinCoord(idb::IdbPin* pin, int32_t& x, int32_t& y) const;
  const RCRoutingLayer* resolveStubLayer(idb::IdbPin* pin, const RoutingContext& ctx) const;
  bool tryPlaceVia(idb::IdbDesign* design, idb::IdbNet* net, int32_t x, int32_t y, const RCRoutingLayer& lower,
                   const RCRoutingLayer& upper, idb::IdbVia* via, const RoutingContext& ctx, WireEnvIndex& index);
  bool addViaAndStub(idb::IdbNet* net, int32_t x, int32_t y, const RCRoutingLayer& lower, const RCRoutingLayer& upper, idb::IdbVia* via,
                     WireEnvIndex& index);
  idb::IdbTerm* pickDiodeTerm(idb::IdbCellMaster* master) const;
};

}  // namespace izh

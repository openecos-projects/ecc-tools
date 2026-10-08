// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
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

#include "NativeRouteSnapshot.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "db/obj/frInstTerm.h"
#include "db/obj/frAccess.h"
#include "db/obj/frShape.h"
#include "db/tech/frConstraint.h"
#include "frDesign.h"

namespace drt::io {
namespace {
void box(std::ostream& out, const odb::Rect& rectangle)
{
  out << ' ' << rectangle.xMin() << ' ' << rectangle.yMin() << ' ' << rectangle.xMax() << ' ' << rectangle.yMax();
}

std::string name(const std::string& value)
{
  std::ostringstream out;
  out << std::quoted(value);
  return out.str();
}

std::string netName(const frNet* net) { return net == nullptr ? "null" : name(net->getName()); }

void accessPoint(std::vector<std::string>& records, const std::string& owner, const frAccessPoint* point)
{
  if (point == nullptr) {
    records.push_back(owner + " null");
    return;
  }
  std::ostringstream out;
  out << owner << ' ' << point->x() << ' ' << point->y() << ' ' << point->getLayerNum()
      << ' ' << static_cast<int>(point->getType(true)) << ' ' << static_cast<int>(point->getType(false))
      << ' ' << point->isViaAllowed();
  for (bool allowed : point->getAccess()) out << ' ' << allowed;
  const auto& vias = point->getAllViaDefs();
  for (std::size_t cuts = 0; cuts < vias.size(); ++cuts) {
    out << " cuts " << cuts + 1;
    for (const auto* via : vias[cuts]) out << ' ' << name(via->getName());
  }
  records.push_back(out.str());
}

void figure(std::vector<std::string>& records, const std::string& owner, const frPinFig& fig)
{
  std::ostringstream out;
  out << owner << ' ' << static_cast<int>(fig.typeId());
  if (fig.typeId() == frcVia) {
    const auto& via = static_cast<const frVia&>(fig);
    out << ' ' << name(via.getViaDef()->getName()) << ' ' << via.getOrigin().x() << ' ' << via.getOrigin().y();
  } else {
    const auto& shape = static_cast<const frShape&>(fig);
    out << ' ' << shape.getLayerNum();
    box(out, shape.getBBox());
    if (fig.typeId() == frcPathSeg) {
      const auto& segment = static_cast<const frPathSeg&>(fig);
      const auto& style = segment.getStyle();
      out << ' ' << segment.getBeginPoint().x() << ' ' << segment.getBeginPoint().y()
          << ' ' << segment.getEndPoint().x() << ' ' << segment.getEndPoint().y()
          << ' ' << style.getWidth() << ' ' << static_cast<int>(static_cast<frEndStyleEnum>(style.getBeginStyle()))
          << ' ' << style.getBeginExt() << ' ' << static_cast<int>(static_cast<frEndStyleEnum>(style.getEndStyle()))
          << ' ' << style.getEndExt() << " tapered " << segment.isTapered();
    } else if (fig.typeId() == frcPolygon) {
      for (const auto& point : static_cast<const frPolygon&>(fig).getPoints()) out << ' ' << point.x() << ' ' << point.y();
    } else if (fig.typeId() != frcRect && fig.typeId() != frcPatchWire) {
      throw std::runtime_error("unsupported DRT snapshot geometry");
    }
  }
  records.push_back(out.str());
}
}  // namespace

void writeNativeRouteSnapshot(const frDesign& design,
                              const std::filesystem::path& path,
                              bool include_routes,
                              bool include_runtime_details)
{
  std::vector<std::string> records;
  const auto* tech = design.getTech();
  if (include_runtime_details) {
  records.push_back("units " + std::to_string(tech->getDBUPerUU()) + " " + std::to_string(tech->getManufacturingGrid()));
  for (const auto& layer : tech->getLayers()) {
    const auto owner = "layer " + name(layer->getName());
    std::ostringstream row;
    row << owner << ' ' << layer->getLayerNum() << ' ' << layer->getType().getString()
        << ' ' << layer->getDir().getString() << ' ' << layer->getPitch() << ' ' << layer->getWidth()
        << ' ' << layer->getMinWidth() << ' ' << layer->getWrongDirWidth()
        << ' ' << layer->getNumMasks() << ' ' << layer->isBackside();
    records.push_back(row.str());
    if (auto* area = layer->getAreaConstraint()) records.push_back(owner + " area " + std::to_string(area->getMinArea()));
    if (const auto* constraint = layer->getMinSpacing()) {
      if (constraint->typeId() != frConstraintTypeEnum::frcSpacingTablePrlConstraint) {
        throw std::runtime_error("unsupported DRT snapshot spacing rule");
      }
      auto table = static_cast<const frSpacingTablePrlConstraint*>(constraint)->getLookupTbl();
      const auto widths = table.getRows();
      const auto lengths = table.getCols();
      const auto values = table.getValues();
      for (std::size_t i = 0; i < widths.size(); ++i) {
        for (std::size_t j = 0; j < lengths.size(); ++j) {
          records.push_back(owner + " prl " + std::to_string(i) + " " + std::to_string(j) + " "
                            + std::to_string(widths[i]) + " " + std::to_string(lengths[j]) + " " + std::to_string(values.at(i).at(j)));
        }
      }
    }
    for (const auto* eol : layer->getEolSpacing()) {
      std::ostringstream rule;
      rule << owner << " eol " << eol->getMinSpacing() << ' ' << eol->getEolWidth() << ' ' << eol->getEolWithin()
           << ' ' << eol->getParSpace() << ' ' << eol->getParWithin() << ' ' << eol->hasTwoEdges();
      records.push_back(rule.str());
    }
    for (const auto* corner : layer->getLef58CornerSpacingConstraints()) {
      std::ostringstream rule;
      rule << owner << " corner " << static_cast<int>(corner->getCornerType()) << ' ' << corner->getSameMask()
           << ' ' << corner->hasCornerOnly() << ' ' << corner->getWithin()
           << ' ' << corner->hasExceptEol() << ' ' << corner->getEolWidth()
           << ' ' << corner->isCornerToCorner() << ' ' << corner->getSameXY()
           << ' ' << corner->getExceptSameNet() << ' ' << corner->getExceptSameMetal()
           << ' ' << corner->findMin() << ' ' << corner->findMax();
      for (const auto width : {0u, tech->getDBUPerUU() / 10, tech->getDBUPerUU() / 5,
                               tech->getDBUPerUU() / 4, tech->getDBUPerUU() / 2,
                               tech->getDBUPerUU()}) {
        rule << ' ' << width << ':' << corner->find(width, true) << ':' << corner->find(width, false);
      }
      records.push_back(rule.str());
    }
    for (bool same_net : {false, true}) {
      for (const auto* cut : layer->getCutSpacing(same_net)) {
        std::ostringstream rule;
        rule << owner << " cut " << cut->getCutSpacing() << ' ' << cut->getCenterToCenter() << ' ' << cut->getSameNet()
             << ' ' << name(cut->getSecondLayerName()) << ' ' << cut->getStack() << ' ' << cut->getAdjacentCuts()
             << ' ' << cut->getCutWithin() << ' ' << cut->getExceptSamePGNet() << ' ' << cut->getParallelOverlap();
        records.push_back(rule.str());
      }
    }
  }
  for (const auto& via : tech->getVias()) {
    const auto owner = "via " + name(via->getName());
    records.push_back(owner + " default " + std::to_string(via->getDefault()));
    for (const auto& shape : via->getLayer1Figs()) figure(records, owner, *shape);
    for (const auto& shape : via->getCutFigs()) figure(records, owner, *shape);
    for (const auto& shape : via->getLayer2Figs()) figure(records, owner, *shape);
  }
  for (const auto& rule : tech->getViaRuleGenerates()) {
    std::ostringstream row;
    row << "via_rule " << name(rule->getName()) << ' ' << rule->getDefault()
        << ' ' << rule->getLayer1Num() << ' ' << rule->getCutLayerNum() << ' ' << rule->getLayer2Num()
        << ' ' << rule->getLayer1Enc().x() << ' ' << rule->getLayer1Enc().y()
        << ' ' << rule->getLayer2Enc().x() << ' ' << rule->getLayer2Enc().y()
        << ' ' << rule->getCutSpacing().x() << ' ' << rule->getCutSpacing().y();
    box(row, rule->getCutRect());
    records.push_back(row.str());
  }
  for (const auto& rule : tech->getNondefaultRules()) {
    const auto owner = "ndr " + name(rule->getName());
    records.push_back(owner + " hard_spacing " + std::to_string(rule->isHardSpacing()));
    for (const auto& layer : tech->getLayers()) {
      const auto number = layer->getLayerNum();
      if (layer->getType() != odb::dbTechLayerType::ROUTING) continue;
      const auto index = number / 2 - 1;
      std::ostringstream row;
      row << owner << " layer " << name(layer->getName()) << ' ' << rule->getWidth(index)
          << ' ' << rule->getSpacing(index) << ' ' << rule->getWireExtension(index);
      if (const auto* via = rule->getPrefVia(index)) row << " via " << name(via->getName());
      records.push_back(row.str());
    }
  }
  for (const auto& master : design.getMasters()) {
    const auto owner = "master " + name(master->getName());
    std::ostringstream row;
    row << owner << ' ' << master->getMasterType().getString();
    row << " id " << master->getId();
    box(row, master->getDieBox());
    records.push_back(row.str());
    for (std::size_t ordinal = 0; ordinal < master->getTerms().size(); ++ordinal) {
      const auto& term = master->getTerms()[ordinal];
      const auto term_owner = owner + " term " + std::to_string(ordinal) + " " + name(term->getName());
      records.push_back(term_owner + " " + term->getType().getString());
      for (std::size_t port = 0; port < term->getPins().size(); ++port) {
        const auto pin_owner = term_owner + " port " + std::to_string(port);
        records.push_back(pin_owner);
        for (const auto& fig : term->getPins()[port]->getFigs()) figure(records, pin_owner, *fig);
      }
    }
    for (const auto& blockage : master->getBlockages()) {
      for (const auto& fig : blockage->getPin()->getFigs()) figure(records, owner + " obs", *fig);
    }
  }
  }
  const auto* block = design.getTopBlock();
  std::ostringstream die;
  die << "die " << name(block->getName()) << ' ' << block->getDBUPerUU();
  box(die, block->getDieBox());
  records.push_back(die.str());
  for (const auto* track : block->getTrackPatterns()) {
    std::ostringstream row;
    row << "track " << track->getLayerNum() << ' ' << track->isHorizontal() << ' ' << track->getStartCoord()
        << ' ' << track->getNumTracks() << ' ' << track->getTrackSpacing();
    records.push_back(row.str());
  }
  for (const auto& grid : block->getGCellPatterns()) {
    if (!include_runtime_details) break;
    std::ostringstream row;
    row << "gcell " << grid.isHorizontal() << ' ' << grid.getStartCoord() << ' ' << grid.getCount() << ' ' << grid.getSpacing();
    records.push_back(row.str());
  }
  for (const auto& instance : block->getInsts()) {
    const auto owner = "instance " + name(instance->getName());
    std::ostringstream row;
    row << owner << ' ' << name(instance->getMaster()->getName()) << ' ' << instance->getOrigin().x()
        << ' ' << instance->getOrigin().y() << ' ' << instance->getOrient().getString();
    if (include_runtime_details) row << " id " << instance->getId();
    const auto offset = instance->getDBTransform().getOffset();
    row << " geometry_offset " << offset.x() << ' ' << offset.y();
    records.push_back(row.str());
    for (const auto& pin : instance->getInstTerms()) {
      const auto pin_owner = owner + " pin "
                             + (include_runtime_details ? std::to_string(pin->getIndexInOwner()) + " " : "")
                             + name(pin->getTerm()->getName());
      records.push_back(pin_owner + " net " + netName(pin->getNet()));
      if (include_routes && include_runtime_details) {
        for (std::size_t port = 0; port < pin->getAccessPoints().size(); ++port) {
          accessPoint(records, pin_owner + " access " + std::to_string(port), pin->getAccessPoints()[port]);
        }
      }
    }
  }
  for (const auto& term : block->getTerms()) {
    const auto owner = "io " + name(term->getName());
    records.push_back(owner + " " + term->getType().getString() + " net " + netName(term->getNet()));
    for (std::size_t port = 0; port < term->getPins().size(); ++port) {
      const auto pin_owner = owner + " port " + std::to_string(port);
      records.push_back(pin_owner);
      for (const auto& fig : term->getPins()[port]->getFigs()) figure(records, pin_owner, *fig);
    }
  }
  const auto record_net = [&](const auto& net) {
    const auto owner = "net " + name(net->getName());
    // Supply pins may be connected implicitly; their container order is not
    // part of the DEF semantics and differs between the two importers.
    const bool record_pin_order = include_runtime_details && !net->isSpecial();
    records.push_back(owner + " " + net->getType().getString() + " " + std::to_string(net->isSpecial()) + " " + std::to_string(net->isFake())
                      + " fixed " + std::to_string(net->isFixed())
                      + " ndr " + (net->getNondefaultRule() == nullptr ? "null" : name(net->getNondefaultRule()->getName()))
                      + (include_runtime_details ? " id " + std::to_string(net->getId()) : ""));
    for (std::size_t ordinal = 0; ordinal < net->getInstTerms().size(); ++ordinal) {
      const auto* pin = net->getInstTerms()[ordinal];
      records.push_back(owner + " pin " + (record_pin_order ? std::to_string(ordinal) + " " : "")
                        + name(pin->getInst()->getName()) + " " + name(pin->getTerm()->getName()));
    }
    for (std::size_t ordinal = 0; ordinal < net->getBTerms().size(); ++ordinal) {
      records.push_back(owner + " io " + (record_pin_order ? std::to_string(ordinal) + " " : "")
                        + name(net->getBTerms()[ordinal]->getName()));
    }
    if (include_routes) {
      for (const auto& shape : net->getShapes()) figure(records, owner + " route", *shape);
      for (const auto& via : net->getVias()) figure(records, owner + " route", *via);
      for (const auto& patch : net->getPatchWires()) figure(records, owner + " route", *patch);
    }
  };
  for (const auto& net : block->getNets()) record_net(net);
  for (const auto& net : block->getSNets()) record_net(net);
  for (const auto& blockage : block->getBlockages()) {
    for (const auto& fig : blockage->getPin()->getFigs()) figure(records, "blockage", *fig);
  }
  for (const auto& marker : block->getMarkers()) {
    if (!include_runtime_details) break;
    std::ostringstream row;
    row << "marker " << marker->getLayerNum() << ' ' << marker->hasDir() << ' ' << marker->isH();
    box(row, marker->getBBox());
    const auto* constraint = marker->getConstraint();
    row << ' ' << (constraint == nullptr ? -1 : static_cast<int>(constraint->typeId()));
    std::vector<std::pair<int, int>> sources;
    for (const auto* source : marker->getSrcs()) {
      if (source != nullptr) sources.emplace_back(static_cast<int>(source->typeId()), source->getId());
    }
    std::sort(sources.begin(), sources.end());
    for (const auto [type, id] : sources) row << " source " << type << ' ' << id;
    records.push_back(row.str());
  }
  std::sort(records.begin(), records.end());
  std::ofstream output(path);
  for (const auto& record : records) output << record << '\n';
  if (!output) throw std::runtime_error("cannot write native DRT semantic snapshot");
}
}  // namespace drt::io

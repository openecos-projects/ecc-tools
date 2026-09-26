#pragma once

#include <concepts>
#include <type_traits>
#include <vector>

#include "design/constraint/model/ConstraintComponents.h"
#include "design/fill/model/FillComponents.h"
#include "design/floorplan/model/FloorplanComponents.h"
#include "design/global/model/DesignGlobalComponents.h"
#include "design/guide/model/GuideComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/pin_access/model/PinAccessComponents.h"
#include "design/non_default_rule/component/NonDefaultRuleComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/via/component/ViaComponents.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/cell_master/model/MasterObsComponents.h"
#include "library/master_port/model/MasterPortComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "library/site/model/SiteComponents.h"
#include "tech/common/TechRuleRelations.h"
#include "tech/cut_layer/model/CutLayerComponents.h"
#include "tech/cut_layer/model/CutRuleComponents.h"
#include "tech/cut_layer/model/CutRuleIds.h"
#include "tech/global/model/TechGlobalComponents.h"
#include "tech/implant_layer/model/ImplantLayerComponents.h"
#include "tech/masterslice_layer/model/MastersliceLayerComponents.h"
#include "tech/non_default_rule/model/NonDefaultRuleComponents.h"
#include "tech/overlap_layer/model/OverlapLayerComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/routing_layer/model/RoutingRuleComponents.h"
#include "tech/routing_layer/model/RoutingRuleIds.h"
#include "tech/via_master/model/ViaMasterComponents.h"
#include "tech/via_rule/model/ViaRuleComponents.h"
#include "tech/via_rule_generate/model/ViaRuleGenerateComponents.h"

namespace eccdb {

// TechConductorLayerRef has convenience constructors and is not an aggregate.
template <typename Archive>
void binaryArchive(Archive& archive, const TechConductorLayerRef& value)
{
  archive(value.kind, value.entity);
}

template <typename Archive>
void binaryArchive(Archive& archive, TechConductorLayerRef& value)
{
  archive(value.kind, value.entity);
}

// Instance-local library lookup is a runtime cache; only the ordered pin IDs are persisted.
template <typename Archive>
void binaryArchive(Archive& archive, const DesignInstancePins& value)
{
  archive(value.values);
}

template <typename Archive>
void binaryArchive(Archive& archive, DesignInstancePins& value)
{
  archive(value.values);
  value.term_ids.clear();
  value.master = {};
  value.master_term_name_index = nullptr;
}

// The instance and term pointers are runtime caches; only the four IDs are persisted.
template <typename Archive>
void binaryArchive(Archive& archive, const DesignInstancePin& value)
{
  archive(value.instance, value.master_term, value.net, value.special_net);
}

template <typename Archive>
void binaryArchive(Archive& archive, DesignInstancePin& value)
{
  archive(value.instance, value.master_term, value.net, value.special_net);
  value.instance_ptr = nullptr;
  value.master_term_ptr = nullptr;
}

namespace binary_detail {

inline constexpr uint32_t kTechSchemaVersion = 8;
inline constexpr uint32_t kLibrarySchemaVersion = 2;
inline constexpr uint32_t kDesignSchemaVersion = 3;

template <typename... Types>
struct BinaryTypeList
{
};

// This is the complete persisted Tech ECS schema. Adding a component to a
// storage requires adding its payload and any relation component here, then
// incrementing kTechSchemaVersion.
using TechBinaryComponents = BinaryTypeList<
    TechRoot, TechLayerSequence, TechGlobalUnits, TechManufacturingGrid, TechMaxViaStack, TechLayerInfo, TechLayerProperties,
    TechRoutingLayer, TechCutLayer, TechImplantLayer, TechImplantSpacingRules, TechMastersliceLayer, TechTrimmedMetalRule, TechOverlapLayer,
    TechRoutingSpacingRule, TechRoutingEndOfLineSpacingRule, TechRoutingMinEncloseAreaRule, TechRoutingMinStepRule,
    TechRoutingMinimumCutRule,
    TechRoutingSpacingNotchLengthRule, TechRoutingPrlSpacingTableRule, TechRoutingInfluenceSpacingTableRule,
    TechRoutingTwoWidthsSpacingTableRule, TechRoutingCurrentDensityRule, TechRoutingLef58AreaRule, TechRoutingLef58CornerFillSpacingRule,
    TechRoutingLef58CornerSpacingRule,
    TechRoutingLef58MinimumCutRule, TechRoutingLef58MinStepRule, TechRoutingLef58WidthTableRule, TechRoutingLef58SpacingEolRule,
    TechRoutingLef58SpacingNotchLengthRule, TechRoutingLef58SpacingTableJogToJogRule, TechRuleRefs<TechRoutingSpacingRuleId>,
    TechRuleRefs<TechRoutingEndOfLineSpacingRuleId>,
    TechRuleRefs<TechRoutingMinEncloseAreaRuleId>, TechRuleRefs<TechRoutingMinStepRuleId>, TechRuleRefs<TechRoutingMinimumCutRuleId>,
    TechRuleRefs<TechRoutingSpacingNotchLengthRuleId>, TechRuleRefs<TechRoutingPrlSpacingTableRuleId>,
    TechRuleRefs<TechRoutingInfluenceSpacingTableRuleId>, TechRuleRefs<TechRoutingTwoWidthsSpacingTableRuleId>,
    TechRuleRefs<TechRoutingCurrentDensityRuleId>, TechRuleRefs<TechRoutingLef58AreaRuleId>,
    TechRuleRefs<TechRoutingLef58CornerFillSpacingRuleId>, TechRuleRefs<TechRoutingLef58CornerSpacingRuleId>,
    TechRuleRefs<TechRoutingLef58MinimumCutRuleId>,
    TechRuleRefs<TechRoutingLef58MinStepRuleId>, TechRuleRefs<TechRoutingLef58WidthTableRuleId>,
    TechRuleRefs<TechRoutingLef58SpacingEolRuleId>, TechRuleRefs<TechRoutingLef58SpacingNotchLengthRuleId>,
    TechRuleRefs<TechRoutingLef58SpacingTableJogToJogRuleId>, TechCutSpacingRule, TechCutEnclosureRule, TechCutArraySpacingRule,
    TechCutOrthogonalSpacingTableRule,
    TechCutLef58CutClassRule, TechCutLef58EnclosureRule, TechCutLef58EnclosureEdgeRule, TechCutLef58EolEnclosureRule,
    TechCutLef58EolSpacingRule, TechCutLef58SpacingTableRule, TechCutCurrentDensityRule, TechRuleRefs<TechCutSpacingRuleId>,
    TechRuleRefs<TechCutEnclosureRuleId>, TechRuleRef<TechCutArraySpacingRuleId>, TechRuleRefs<TechCutOrthogonalSpacingTableRuleId>,
    TechRuleRefs<TechCutLef58CutClassRuleId>,
    TechRuleRefs<TechCutLef58EnclosureRuleId>, TechRuleRefs<TechCutLef58EnclosureEdgeRuleId>, TechRuleRef<TechCutLef58EolEnclosureRuleId>,
    TechRuleRef<TechCutLef58EolSpacingRuleId>, TechRuleRefs<TechCutLef58SpacingTableRuleId>, TechRuleRefs<TechCutCurrentDensityRuleId>,
    TechViaRuleGenerate, TechViaRuleGenerateBottomLayer, TechViaRuleGenerateCutLayer, TechViaRuleGenerateTopLayer, TechViaMaster,
    TechViaGeometry, TechGeneratedViaMaster, TechViaRule, TechViaRuleLowerLayer, TechViaRuleUpperLayer, TechViaRuleCandidates,
    TechViaRuleProperties, TechNonDefaultRule, TechNdrRoutingRules, TechNdrMinCutsRules, TechNdrUseVias, TechNdrUseViaRules,
    TechNdrProperties, TechNdrViaDefinition, TechNdrViaDefinitions, TechNdrSameNetSpacingRules, TechRuleOwner>;

using LibraryBinaryComponents = BinaryTypeList<LibrarySite, LibraryCellMaster, LibraryMasterTerm, LibraryMasterPort, LibraryMasterObs>;

// DesignWire owns its Wire-local flat routing vectors, which are persisted as
// part of the component snapshot.
using DesignBinaryComponents
    = BinaryTypeList<DesignRoot, DesignInfo, DesignDieArea, DesignCoreArea, DesignRow, DesignTrackGrid, DesignGCellGrid,
                     DesignInstance, DesignInstancePins, DesignInstancePin, DesignIoPin, DesignNet, DesignNetInstancePins,
                     DesignNetIoPins, DesignSpecialNet, DesignNetOptions, DesignWire, DesignNetGeometry, DesignVia,
                     DesignNonDefaultRule, DesignRegion, DesignGroup, DesignBlockage, DesignFill, DesignNetRouteGuides,
                     DesignNetTrackAssignments, DesignPinAccessCatalog, DesignInstancePinAccessGroupIndex,
                     DesignInstancePreferredPinAccess, DesignIoPinAccessGroups>;

template <typename Entity, typename Registry, typename Archive, typename... Components>
void writeRegistrySnapshot(const Registry& registry, Archive& archive, BinaryTypeList<Components...>)
{
  entt::basic_snapshot snapshot{registry};
  snapshot.template get<Entity>(archive);
  const auto write_component = [&]<typename Component>() {
    if constexpr (std::is_same_v<Component, DesignInstance>) {
      // Stable deletion leaves tombstones; write only live components so a
      // restore followed by another save produces the same binary payload.
      const auto* storage = registry.template storage<Component>();
      std::vector<Entity> live;
      if (storage != nullptr) {
        live.reserve(storage->size());
        const typename Registry::common_type& entities = *storage;
        for (auto current = entities.rbegin(); current != entities.rend(); ++current) {
          if (*current != entt::tombstone) {
            live.push_back(*current);
          }
        }
      }
      snapshot.template get<Component>(archive, live.begin(), live.end());
    } else {
      snapshot.template get<Component>(archive);
    }
  };
  (write_component.template operator()<Components>(), ...);
}

template <typename Entity, typename Registry, typename Archive, typename... Components>
void readRegistrySnapshot(Registry& registry, Archive& archive, BinaryTypeList<Components...>)
{
  entt::basic_snapshot_loader loader{registry};
  loader.template get<Entity>(archive);
  (loader.template get<Components>(archive), ...);
}

}  // namespace binary_detail
}  // namespace eccdb

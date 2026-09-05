# CUT Layer Storage

`cut_layer/` is the in-memory representation of LEF CUT layers and their
layer-attached Rules. It is a storage facade over the shared technology
registry:

```text
TechStore
  TechRegistry
    ROUTING Layer and Rule entities
    CUT Layer and Rule entities
  TechCutLayerStorage
```

`TechEntity` is the database-ID type for this registry. CUT layers, ROUTING
layers, and their complete Rules consume IDs from one shared entity space,
while `TechCutLayerId`, `TechRoutingLayerId`, and each typed Rule ID remain
separate strong types.

## Entities and Components

```text
Layer entity VIA12
  TechLayerInfo
  TechCutLayer
  optional typed Rule-reference components

Spacing Rule entity
  TechRuleOwner
  TechCutSpacingRule
```

A complete Rule is an entity because it has a database ID and can be queried,
updated, deleted, and serialized independently. Its payload is one ordinary
C++ component; EnTT does not split the payload's scalar fields into separate
components.

## Layer-to-Rule Cardinality

The relation follows the cardinality represented by current iDB/LEF import
coverage instead of putting unrelated Rules into one heterogeneous list.

```text
Repeatable Rule                         Layer relation
--------------------------------------------------------------
SPACING                                 TechRuleRefs<TechCutSpacingRuleId>
LEF58 CUTCLASS                          TechRuleRefs<TechCutLef58CutClassRuleId>
LEF58 ENCLOSURE                         TechRuleRefs<TechCutLef58EnclosureRuleId>
LEF58 ENCLOSUREEDGE                     TechRuleRefs<TechCutLef58EnclosureEdgeRuleId>
LEF58 SPACINGTABLE                      TechRuleRefs<TechCutLef58SpacingTableRuleId>
CURRENTDENSITY                          TechRuleRefs<TechCutCurrentDensityRuleId>

Single Rule                             Layer relation
--------------------------------------------------------------
native ENCLOSURE TechRuleRefs<TechCutEnclosureRuleId> (ordered and repeatable, including unqualified rules)
ARRAYSPACING                            TechRuleRef<TechCutArraySpacingRuleId>
LEF58 EOLENCLOSURE                      TechRuleRef<TechCutLef58EolEnclosureRuleId>
LEF58 EOLSPACING                        TechRuleRef<TechCutLef58EolSpacingRuleId>
```

Each repeated reference vector is a distinct EnTT component on its Layer.
`TechRuleOwner` is present on every complete Rule and provides the reverse
Rule-to-Layer direction. `spacingRules(layer)` reads only the spacing vector
for that Layer. It never scans enclosure, cut-class, or current-density Rules.

A single-valued Rule is obtained directly, for example:

```cpp
const auto array_rule = cut.arraySpacingRule(layer);
const auto eol_rule = cut.lef58EolSpacingRule(layer);
const auto below = cut.enclosureRule(layer, CutLayerSide::kBelow);
```

`setArraySpacingRule`, `setEnclosureRule`, and the LEF58 `set...` methods
update an existing Rule entity in place and retain its ID. Repeatable Rules use
named `add...Rule` methods.

For a DRC-style all-layers query, EnTT directly views one payload storage:

```cpp
const auto view = cut.registry().view<const TechCutSpacingRule,
                                      const TechRuleOwner>();
for (const auto entity : view) {
  const auto& rule = view.get<const TechCutSpacingRule>(entity);
  const auto& owner = view.get<const TechRuleOwner>(entity);
}
```

The raw registry is read-only through this facade. All mutation uses named
storage methods so owner references and typed vectors remain consistent.

## Variable-Length Attributes

Rule-internal rows are not independent database objects and do not receive
EnTT IDs. They are ordinary `std::vector` fields in their owning Rule
component:

```text
Array Spacing Rule entity
  TechRuleOwner
  TechCutArraySpacingRule
    items: vector<TechCutArraySpacingItem>

LEF58 Spacing Table Rule entity
  TechRuleOwner
  TechCutLef58SpacingTableRule
    cutclass1_names: vector<string>
    cutclass2_names: vector<string>
    cells: vector<TechCutLef58SpacingTableCell>
```

This applies to `ARRAYSPACING`, `LEF58 EOLSPACING`, `LEF58 SPACINGTABLE`,
and `CURRENTDENSITY`. Replacing or destroying a Rule replaces or frees all of
its child vectors with the component. There is no CUT-specific sidecar pool or
offset/count indirection.

The spacing-table cells are stored in row-major order: class-2 is the row,
class-1 is the column. `lef58SpacingTableCell(rule, class1, class2)` performs
that index calculation. Current-density table entries are frequency-major,
cut-area-minor; `currentDensityAt` performs linear interpolation over the two
axes.

## Persistence Boundary

Persist the shared `TechRegistry` entity set and component storages: every
Layer component, concrete Rule payload, `TechRuleOwner`, each typed
`TechRuleRefs` vector, and each single-Rule reference component. The registry
is serialized once for both ROUTING and CUT; a CUT-only export filters by CUT
component types. Each Rule component serializes its own strings and vectors.

EnTT snapshot loading can restore entity IDs and component attachment into an
empty shared registry. Layer name and query indexes are derived data rebuilt
after loading; they are not a Catalog component.

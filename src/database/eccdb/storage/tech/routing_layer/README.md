# ROUTING Layer Storage

`routing_layer/` is the ROUTING-specific part of the shared technology
database. It follows the same boundary as `cut_layer/`:

```text
model/
  RoutingLayerComponents.h  fixed fields on a ROUTING Layer entity
  RoutingRuleComponents.h   one payload component per complete Rule entity
  RoutingRuleItems.h        ID-less rows used only inside variable Rule data

storage/
  RoutingLayerStorage       creates entities, maintains relations, validates
                            inputs, and provides named table queries
```

## EnTT Objects

All Tech layers and complete Rules use one `TechRegistry` entity space. A
ROUTING Layer entity has `TechLayerInfo` and `TechRoutingLayer`. A complete Rule
is a separate entity with exactly one concrete Rule payload plus the common
relation components:

```text
ROUTING Layer entity
  TechLayerInfo
  TechRoutingLayer
  TechRuleRefs<TechRoutingSpacingRuleId>
  TechRuleRefs<TechRoutingPrlSpacingTableRuleId>
  ... one typed vector for each repeatable Rule family

ROUTING Rule entity
  TechRuleOwner              reverse Rule -> Layer relation
  TechRouting...Rule         one concrete payload component
```

`TechRoutingLayerStorage::spacingRules(layer)` reads the typed vector for
spacing Rules only. It does not scan all Routing Rules or use an owner hash
map. `ruleOwner(rule_id)` is the reverse lookup. A strongly typed
`TechRouting...RuleId` is the same EnTT database ID with a compile-time
payload role; it is not a second allocation scheme.

## Variable Rule Data

Scalar fields and variable data both remain in the Rule component. Rule-
internal rows are not independent database objects, so they do not receive
EnTT IDs. Each Rule owns ordinary `std::vector` fields and strings:

```text
PRL spacing Rule entity
  widths                    vector<int32_t>
  parallel_run_lengths      vector<int32_t>
  cells                     vector<int32_t>
  except_withins            vector<TechRoutingPrlSpacingTableExceptWithin>
  influences                vector<TechRoutingPrlSpacingTableInfluence>
```

The same pattern is used for influence rows, two-width axes/cells,
current-density axes/table entries, LEF58 minimum-cut classes, width tables,
area exceptions, and JogToJog widths. A flattened matrix is still a single
contiguous vector, but it is visibly owned by its Rule entity.

## Table Semantics

The canonical representation is contiguous and serializable, not a hash map:

```text
PARALLELRUNLENGTH: width axis + PRL axis + width-major cells
TWOWIDTHS:         one width/PRL axis + N x N row-major cells
CURRENTDENSITY:    frequency axis + width axis + frequency-major cells
```

`RoutingLayerStorage` validates ascending axes and matrix dimensions while
inserting data. It exposes indexed reads plus LEF-oriented calculations:

- `prlSpacingFor`: strict threshold selection on width and PRL axes.
- `influenceSpacingFor` and `prlInfluenceSpacingFor`: matching influence rows.
- `twoWidthsSpacingFor`: selects two compatible axis rows then indexes the
  dense matrix.
- `currentDensityAt`: scalar lookup or linear interpolation across the dense
  frequency/width table.

An `unordered_map` can later be built as a derived cache for a demonstrably
hot exact-key query, but is not the persistent source of truth. LEF table
queries are threshold/interpolation queries and require ordered axes.

## Persistence and Mutation

Persist the shared `TechRegistry` once, including all Layer/Rule payloads,
their strings/vectors, `TechRuleOwner`, and each typed `TechRuleRefs`
relation component. Layer name and query indexes are derived and rebuilt.
There is no Routing-specific sidecar pool or compaction phase to serialize.

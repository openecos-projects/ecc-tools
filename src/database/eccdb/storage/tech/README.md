# ECCDB Tech Directory

This tree is the EnTT-first technology database. `TechStore` owns one
`TechRegistry`; every complete Tech object receives its database ID from that
registry.

```text
TechStore
  TechRegistry
    TechRoot entity
      TechRoot + TechLayerSequence + optional global-statement components
    Layer entities
      TechLayerInfo + exactly one type component
      TechRoutingLayer | TechCutLayer | TechImplantLayer
      TechMastersliceLayer | TechOverlapLayer
    Rule entities
      TechRuleOwner + one concrete Rule component
    TechViaMaster, TechViaRule, TechViaRuleGenerate, and NDR entities

  common/              shared IDs, properties, sequence, and relations
  global/              TechRoot-owned UNITS, MANUFACTURINGGRID, MAXVIASTACK
  routing_layer/        ROUTING layer fields and rules
  cut_layer/            CUT layer fields and rules
  implant_layer/        IMPLANT layer fields and SPACING rules
  masterslice_layer/    MASTERSLICE subtype and TRIMMEDMETAL rule
  overlap_layer/        OVERLAP marker component
  via_master/           fixed/generated VIA geometry and parameters
  via_rule/             ordinary LEF VIARULE
  via_rule_generate/    LEF VIARULE GENERATE
  non_default_rule/     LEF NONDEFAULTRULE
```

## Model File Names

- `*Components.h` declares EnTT component payloads and their immediate enums
  or small helper values.
- `*Items.h` holds non-entity rows nested in variable-length component data.
- `*Types.h` holds shared enums and strongly typed IDs.
- `*Relations.h` holds owner/reference components between entities.
- `*Storage.h/.cpp` owns validation and CRUD operations over those components.

The old `*Records.h` suffix is not used: it did not say whether a type was an
EnTT component, a nested value, or an input-only object.

There is no `TechLayerCatalog`, `TechLayerKind`, `TechLayerRef`, separate
layer registry, or copied `order` field. `TechLayerInfo` holds only shared
layer metadata (`name`, `mask_count`). The specialized component attached to
the same entity determines its type.

## IDs and Relations

- A layer, VIA, TechViaRule, NDR, or complete layer Rule is one EnTT entity. Its
  EnTT entity value is the database ID.
- Strong ID wrappers (`TechCutLayerId`, `TechRoutingSpacingRuleId`, and so on)
  give compile-time meaning to the same underlying entity value. They do not
  allocate separate ID spaces.
- A repeated complete Rule owns an entity, carries `TechRuleOwner` for reverse
  lookup, and is listed by `TechRuleRefs<RuleId>` on its parent. The relation
  is stored directly as a typed `std::vector` component.
- Rule-internal variable data stays in its payload component as
  `std::string`, `std::vector`, and nested values. It has no identity and does
  not consume an EnTT entity ID.

## Layer Sequence

`TechLayerSequence` is a component on the `TechRoot` entity. Its vector stores
physical layer IDs in bottom-to-top process order and derives process position,
`isBelow`, and routing level. It is not duplicated into every layer component.

`ROUTING`, `CUT`, `IMPLANT`, and `MASTERSLICE` layers can be in this physical
sequence. `OVERLAP` is intentionally excluded: it is a macro placement
footprint abstraction rather than part of the manufacturing layer stack.

Layer names are enforced by `TechStore` in one Tech-wide namespace.
Derived name or spatial indexes belong in rebuildable caches, not in the
persisted entity schema.

## Global Statements

`TechGlobalStorage` owns `UNITS`, `MANUFACTURINGGRID`, and `MAXVIASTACK` as
optional components on the one `TechRoot` entity. These statements do not have
independent identity or lifetime, so they deliberately do not allocate a
second entity. `MAXVIASTACK` still stores typed routing-layer IDs and validates
their membership and bottom-to-top order against `TechLayerSequence`.

`global/model/TechGlobalComponents.h` contains fields only. EnTT access,
validation, and component removal belong to `global/storage/TechGlobalStorage`.

## Persistence Boundary

Persist the shared registry once with a declared component schema: TechRoot
state including `TechLayerSequence`, layer metadata/type components, routing
and cut rule relations, VIA components, and the NDR typed-vector components.
Strings and vectors are serialized as their contents, not as raw object memory.
Derived indexes and any query cache are rebuilt after loading.

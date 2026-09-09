# EccDB Experimental C++ API Usage Notes

Chinese version: [Experimental_API_Usage.zh-CN.md](Experimental_API_Usage.zh-CN.md)

> **Status: experimental attempt.** This interface explores the public access boundary; it is not a finalized SDK. Types, functions, error semantics, installation/export rules, and ABI may change incompatibly. These notes describe the current prototype without source or binary compatibility promises. Pin a source revision when integrating; see the [integration plan](Integration_Plan.en.md).

## 1. API Scope

The EccDB public API is an experimental access layer above the internal EnTT registries, contiguous pools, and storage classes. External programs only include public `eccdb` headers and do not need to understand EnTT entities, internal components, or how storage is partitioned.

The current prototype provides:

- opening a database from LEF/DEF or EccDB binary archives;
- query and CRUD operations for nets, special nets, instances, IO pins, wires, and DEF design vias;
- instance-pin queries and connectivity operations; pins are created/deleted with their instance, with no independent pin CRUD API;
- pin-to-net connectivity operations;
- value types for wire paths, points, via placements, and rectangle extensions;
- DEF and binary export;
- optional C++ `Ref` convenience handles.

Complete public technology and library query/edit APIs are not available yet. Therefore, the `CellMasterId`, `RoutingLayerId`, `LayerId`, and `TechViaId` values required to create instances, wires, or design vias from scratch normally come from objects in an already imported database. The primary prototype workflow is to import existing LEF/DEF or binary data and then query and edit the design.

## 2. Include and Link

Use the aggregate public header:

```cpp
#include <eccdb/eccdb.h>
```

The implementation provides the following experimental CMake export, not a stable installation/ABI contract. Set `CMAKE_PREFIX_PATH` to the installation prefix in a consuming project:

```cmake
find_package(EccDB CONFIG REQUIRED)
target_link_libraries(my_tool PRIVATE EccDB::eccdb)
```

See [BUILDING.md](../BUILDING.md) for source-tree build and installation instructions.

## 3. Open a Database

### 3.1 LEF/DEF input

```cpp
#include <eccdb/eccdb.h>
#include <stdexcept>

eccdb::Config config{
    .input = eccdb::LefDefInput{
        .lef_files = {"technology.lef", "cells.lef"},
        .def_file = "design.def",
    },
    .runtime = eccdb::RuntimeOptions{
        .polygon_mode = eccdb::PolygonMode::kNative,
    },
};

if (!eccdb::Database::supportsLefDef()) {
  throw std::runtime_error("this EccDB build has no LEF/DEF parser");
}

auto database = eccdb::Database::open(config);
```

`PolygonMode::kNative` preserves native polygons. `PolygonMode::kRectangularized` converts supported polygons into rectangles during import.

### 3.2 Generate a binary cache after import

```cpp
eccdb::BinaryFiles cache{
    .technology = "technology.edb",
    .library = "library.edb",
    .design = "design.edb",
};

eccdb::Config config{
    .input = eccdb::LefDefInput{
        .lef_files = {"technology.lef", "cells.lef"},
        .def_file = "design.def",
    },
    .binary_cache = cache,
};

auto database = eccdb::Database::open(config);
```

Technology, library, and design use three binary files because they are related but independent storage domains. `binary_cache` writes a snapshot after successful text import; it does not detect and reuse an existing cache automatically.

### 3.3 Binary input

```cpp
auto database = eccdb::Database::open({
    .input = eccdb::BinaryInput{
        .files = {
            .technology = "technology.edb",
            .library = "library.edb",
            .design = "design.edb",
        },
    },
});
```

Binary input cannot also specify `binary_cache`, or set `runtime.polygon_mode` to `kRectangularized` to override the archived mode; either request currently throws `std::invalid_argument`. Archive format/schema checks are not a cross-version compatibility promise.

The following operation fragments assume a successfully opened `database`. Adjust example object names to match the input. Include `<utility>`, `<iostream>`, and `<stdexcept>` when using moves, output, and exceptions.

## 4. IDs, Data, and Refs

The public API has three central type categories:

| Type | Examples | Semantics |
| --- | --- | --- |
| Typed ID | `NetId`, `WireId`, `InstanceId` | Identity of a database entity; copyable, comparable, and hashable; valid only while its database and entity remain alive |
| Data value | `NetData`, `InstanceData`, `WireRoutingData` | An owning detached snapshot; modifying it does not automatically update the database |
| Ref handle | `NetRef`, `WireRef`, `InstanceRef` | A non-owning C++ convenience object containing `DatabaseState* + ID` |

The Database owns the real entity. The following code only modifies a local snapshot:

```cpp
eccdb::NetId net_id = database.findNetId("clk");
auto data = database.netData(net_id);

if (data) {
  data->name = "clk_main";
  // The database still contains the name clk.
}
```

Write the snapshot back explicitly:

```cpp
if (data) {
  database.updateNet(net_id, *data);
}
```

Every ID belongs to exactly one `Database`. Do not pass a `NetId` from database A to database B. An ID becomes invalid when its entity is destroyed or its owning Database is destroyed. `if (id)` only checks for a non-sentinel value, not whether the entity still exists; deleting an entity does not rewrite IDs saved by callers. Use `contains(id)` on the originating database to check existence. IDs do not carry a database identity check.

## 5. Net CRUD

### 5.1 Query and iteration

```cpp
eccdb::NetId net = database.findNetId("clk");
eccdb::NetId regular = database.findRegularNetId("clk");
eccdb::NetId power = database.findSpecialNetId("VDD");

if (net && database.contains(net)) {
  auto data = database.netData(net);
  bool special = database.isSpecialNet(net);
}

for (eccdb::NetId id : database.regularNetIds()) {
  auto data = database.netData(id);
}
```

When an object is not found, `find*Id()` returns an invalid ID and `*Data()` returns `std::nullopt`.

### 5.2 Create, update, and destroy

```cpp
eccdb::NetId signal = database.createNet({
    .name = "data_bus",
    .use = eccdb::SignalUse::kSignal,
    .source = eccdb::NetSource::kUser,
});

eccdb::NetId ground = database.createSpecialNet({
    .name = "VSS",
    .use = eccdb::SignalUse::kGround,
});

auto signal_data = *database.netData(signal);
signal_data.weight = 10;
database.updateNet(signal, std::move(signal_data));

// Delete a separate temporary net; retain signal for the connectivity example.
auto temporary = database.createNet({.name = "temporary_net"});
bool removed = database.destroyNet(temporary);
```

If a net still has connected pins or owns wires, `destroyNet()` returns `false` and preserves all relationships.

Use `NetOptions` for optional special-net attributes:

```cpp
database.setNetOptions(ground, eccdb::NetOptions{
    .original = "VSS_ORIGINAL",
    .voltage = 0,
});
```

## 6. Instances, Pins, and Connectivity

### 6.1 Query an instance and its materialized pins

```cpp
eccdb::InstanceId instance = database.findInstanceId("u1");
auto instance_data = database.instanceData(instance);

eccdb::InstancePinId input = database.findInstancePinId(instance, "A");
auto pin_data = database.instancePinData(input);
```

Creating an instance automatically materializes its instance pins from the terminals of the referenced `CellMasterId`:

```cpp
if (!instance_data) {
  throw std::runtime_error("example instance u1 was not found");
}
eccdb::CellMasterId master = instance_data->master;

eccdb::InstanceId created = database.createInstance({
    .name = "u2",
    .master = master,
    .origin = {1000, 2000},
    .orientation = eccdb::Orientation::kN,
    .placement_status = eccdb::PlacementStatus::kPlaced,
});
```

### 6.2 IO pins and net connectivity

```cpp
eccdb::IoPinId io_pin = database.createIoPin({
    .name = "DATA_IN",
    .direction = eccdb::IoDirection::kInput,
    .use = eccdb::SignalUse::kSignal,
});

if (!database.contains(input) || !database.contains(signal)) {
  throw std::runtime_error("example pin or net was not found");
}
database.connect(io_pin, signal);
database.connect(input, signal);

auto io_data = database.ioPinData(io_pin);
auto connected_instances = database.instancePinIds(signal);
auto connected_io_pins = database.ioPinIds(signal);

database.disconnect(input, signal);
database.disconnect(io_pin);
```

`IoPinData::net` and `IoPinData::special_net` are connectivity snapshots. Change connectivity through `connect()` and `disconnect()`. `updateIoPin()` changes the name, direction, and use, and requires the connection fields in the supplied snapshot to match the current database state.

## 7. Wires, Paths, and Vias

A wire is an identified database entity represented by `WireId`. Wire paths, points, via placements, and rectangles are values owned by the wire and do not have independent IDs.

### 7.1 Create a DEF design via

The following `layer` is assumed to be a `LayerId` obtained from an imported database object:

```cpp
eccdb::ViaId via = database.createDesignVia({
    .name = "LOCAL_VIA",
    .rectangles = {
        eccdb::ViaRectangle{
            .layer = layer,
            .rectangle = {-5, -5, 5, 5},
        },
    },
});
```

### 7.2 Create wire routing

The following `routing_layer` is an already obtained `RoutingLayerId`:

```cpp
eccdb::WireRoutingData routing{
    .paths = {
        eccdb::WirePathData{
            .layer = routing_layer,
            .width = 100,
            .points = {
                eccdb::WirePoint{.position = {0, 0}},
                eccdb::WirePoint{.position = {1000, 0}},
            },
            .vias = {
                eccdb::ViaPlacementData{
                    .point_index = 1,
                    .definition = eccdb::ViaDefinitionId{via},
                },
            },
        },
    },
};

eccdb::WireId wire = database.createWire(
    eccdb::WireMetadata{
        .net = ground,
        .status = eccdb::WireStatus::kFixed,
    },
    std::move(routing));
```

`ViaDefinitionId` is `std::variant<TechViaId, ViaId>`:

- `ViaDefinitionId{tech_via}` references a LEF technology via;
- `ViaDefinitionId{design_via}` references a DEF design via;
- every placement must reference exactly one of the two.

### 7.3 Query and write back a complete routing value

```cpp
auto metadata = database.wireMetadata(wire);
auto routing_data = database.wireRoutingData(wire);

if (metadata && routing_data && !routing_data->paths.empty()) {
  routing_data->paths.front().width = 120;
  database.updateWire(wire, *metadata, std::move(*routing_data));
}

for (eccdb::WireId id : database.wireIds(ground)) {
  auto wire_data = database.wireRoutingData(id);
}
```

Because a path is an aggregate value, write path changes back through `updateWire()`. Internal pool addresses, offsets, and spans never cross the public API boundary.

## 8. Ref Convenience Layer

Refs support short-lived, chainable operations for C++ callers:

```cpp
eccdb::NetRef net = database.net(net_id);
if (net) {
  net.rename("clk_main");
  net.setUse(eccdb::SignalUse::kClock);

  for (eccdb::WireRef wire : net.wires()) {
    auto path = wire.pathData(0);
  }
}
```

A Ref is normally just a pointer and an ID returned by value. It does not require `new` or `delete`, does not own the Database, and must not outlive it. Caches may store typed IDs within the same database lifetime. This does not permit reusing IDs across databases, reopened files, or versions.

The `std::string_view` returned by `name()` is a short-lived borrow. Do not use an old view after renaming or updating the object, or after destroying the Database.

## 9. Export and Diagnostics

```cpp
database.writeDef("edited.def");

database.writeBinary({
    .technology = "technology.edb",
    .library = "library.edb",
    .design = "design.edb",
});

for (const eccdb::ImportDiagnostic& diagnostic : database.diagnostics()) {
  std::cout << diagnostic.source << ": "
            << diagnostic.statement << " x"
            << diagnostic.occurrence_count << '\n';
}
```

## 10. Errors, Lifetimes, and Concurrency

- For a missing ID, `contains()` returns `false`, Data queries return `std::nullopt`, and Ref queries return an empty handle.
- Invalid create or update operations normally throw `std::invalid_argument`. While its database state is still alive, checked accessors on an empty Ref or deleted entity throw `std::out_of_range`. After database destruction a Ref is dangling; no access, including a validity check, is protected by this exception behavior.
- A `destroy*()` result of `false` normally means the object does not exist or is still referenced.
- IDs are meaningful only within their owning Database. An ID does not currently carry a Database cookie.
- Refs and views do not own storage and become invalid when the Database is destroyed.
- The public API does not currently guarantee concurrent read/write safety. Callers must synchronize access to a shared Database.
- Transactions, undo/redo, revisions/change sets, and a stable Rust C ABI are not part of the current API and remain future extensions.

## 11. Public Headers

| Header | Contents |
| --- | --- |
| `eccdb/eccdb.h` | Recommended aggregate entry point |
| `eccdb/db.h` | Compatibility header forwarding to the aggregate entry point |
| `eccdb/Config.h` | Input, runtime options, and binary file configuration |
| `eccdb/Types.h` | Typed IDs, basic geometry, and enums |
| `eccdb/DesignData.h` | Net, instance, and pin snapshots |
| `eccdb/RoutingData.h` | Design via, wire, and path values |
| `eccdb/Ref.h` | Non-owning C++ Ref convenience handles |
| `eccdb/Database.h` | Database ownership and core CRUD API |

External programs must not include headers from `api/internal`, `storage`, `io`, or `adapters`.

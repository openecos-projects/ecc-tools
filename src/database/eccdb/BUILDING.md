# Standalone build

The eccdb directory can be configured without building the complete
ecc-tools application.

Public API guides: [English](doc/API_Usage.en.md) | [简体中文](doc/API_Usage.zh-CN.md)

Differential testing guides: [English](doc/Differential_Testing.en.md) | [简体中文](doc/Differential_Testing.zh-CN.md)

Architecture guides: [English](doc/Architecture.en.md) | [简体中文](doc/Architecture.zh-CN.md)

## Core EnTTDB

```bash
cmake -S src/database/eccdb -B build/eccdb-core -G Ninja \
  -DECCDB_BUILD_TESTS=ON

# Build only the EnTTDB libraries.
cmake --build build/eccdb-core --target eccdb_database --parallel 128

# Build and run the standalone core tests.
cmake --build build/eccdb-core --parallel 128
ctest --test-dir build/eccdb-core --output-on-failure --parallel 128
```

This builds the EnTTDB storage, binary persistence, exporters, and core unit
tests. It does not build a LEF or DEF parser.

## Direct LEF/DEF and OpenDB differential tests

```bash
cmake -S src/database/eccdb -B build/eccdb-differential -G Ninja \
  -DECCDB_BUILD_TESTS=ON \
  -DECCDB_STANDALONE_LEF_DEF=ON \
  -DOPENROAD_SOURCE_DIR=/path/to/OpenROAD \
  -DOPENDB_PYTHON_MODULE_DIR=/path/to/OpenROAD/bazel-bin/src/odb
cmake --build build/eccdb-differential \
  --target eccdb_differential_tests --parallel 128
ctest --test-dir build/eccdb-differential --output-on-failure \
  --parallel 128 --label-regex eccdb_differential
```

This configuration adds only the checked-in SI2 LEF/DEF parsers. OpenDB is a
runtime differential oracle and is not linked into EnTTDB; its paths are
optional, and tests that require unavailable external corpora are skipped.

Every standalone configure emits `compile_commands.json` in its build
directory for clangd.

## Installable C++ API

Build and install the public API with direct LEF/DEF support:

```bash
cmake -S src/database/eccdb -B build/eccdb-api -G Ninja \
  -DECCDB_BUILD_TESTS=ON \
  -DECCDB_STANDALONE_LEF_DEF=ON \
  -DCMAKE_INSTALL_PREFIX=/opt/eccdb
cmake --build build/eccdb-api --target eccdb_api --parallel 128
cmake --install build/eccdb-api
```

An external CMake project consumes the installed package as follows:

```cmake
find_package(EccDB CONFIG REQUIRED)
target_link_libraries(my_tool PRIVATE EccDB::eccdb)
```

The public C++ entry point is:

```cpp
#include <eccdb/eccdb.h>

eccdb::Config config{
    .input = eccdb::LefDefInput{
        .lef_files = {"technology.lef", "cells.lef"},
        .def_file = "design.def",
    },
};
auto database = eccdb::Database::open(config);
```

The stable API uses database-owned entities and explicit value snapshots:

```cpp
eccdb::NetId net = database.findNetId("clk");
auto data = database.netData(net);  // owned snapshot
if (data) {
  data->name = "clk_main";         // does not mutate the database yet
  database.updateNet(net, *data);  // explicit writeback
}

// Optional C++ convenience handle. It is non-owning and must not outlive the
// Database; the ID remains the stable identity used for storage and bindings.
eccdb::NetRef net_ref = database.net(net);
net_ref.setUse(eccdb::SignalUse::kClock);
```

`ObjectId` values are local to one `Database`: they can be copied, hashed and
stored by callers, but become invalid when their entity or owning database is
destroyed. `Data` and routing-path values own their contents. `Ref` values and
returned `string_view` objects are borrowed conveniences and must not be kept
past their documented lifetime.

Binary input and output use three files because technology, library and
design registries have independent schemas:

```cpp
eccdb::BinaryFiles files{
    .technology = "technology.edb",
    .library = "library.edb",
    .design = "design.edb",
};
database.writeBinary(files);
auto restored = eccdb::Database::open(
    {.input = eccdb::BinaryInput{.files = files}});
```

A standalone build without `ECCDB_STANDALONE_LEF_DEF=ON` can still open and
write binary databases. `Database::supportsLefDef()` reports whether the
installed library contains the direct text importers.

## Legacy iDB LEF adapter differential tests

```bash
cmake -S src/database/eccdb -B build/eccdb-legacy-differential -G Ninja \
  -DECCDB_BUILD_TESTS=ON \
  -DECCDB_STANDALONE_LEGACY_IDB=ON
cmake --build build/eccdb-legacy-differential \
  --target eccdb_differential_tests --parallel 128
ctest --test-dir build/eccdb-legacy-differential --output-on-failure \
  --parallel 128 --label-regex eccdb_differential
```

This option implies `ECCDB_STANDALONE_LEF_DEF`. It adds only the legacy
layout objects, geometry, LEF service, and LEF builder required to compare the
direct EnTT importer with the legacy iDB materialization path. It does not add
the complete `IdbBuilder` or the DEF, Verilog, GDS, JSON, GUI, Python, iRT, and
iDRC target trees.

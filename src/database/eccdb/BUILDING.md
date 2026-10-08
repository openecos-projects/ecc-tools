# Standalone build

The eccdb directory can be configured without building the complete
ecc-tools application.

Native C++ API: [原生接口与生命周期](doc/Native_API.md)

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

## Installable native C++ API

The API uses native Storage types and services. `Database` only owns the
stores and loads/saves them; duplicate Data/Ref types and the old C/Rust APIs
have been removed. Pin the source version and compile configuration: the
native C++ ABI is not a cross-version compatibility boundary.

Build and install the prototype with direct LEF/DEF support:

```bash
cmake -S src/database/eccdb -B build/eccdb-api -G Ninja \
  -DECCDB_BUILD_TESTS=ON \
  -DECCDB_STANDALONE_LEF_DEF=ON \
  -DCMAKE_INSTALL_PREFIX=/opt/eccdb
cmake --build build/eccdb-api --target eccdb_database --parallel 8
cmake --install build/eccdb-api
```

An external CMake project consumes the installed package as follows:

```cmake
find_package(EccDB CONFIG REQUIRED)
target_link_libraries(my_tool PRIVATE EccDB::database EccDB::core)
```

The public C++ entry point is:

```cpp
#include <eccdb.h>

eccdb::Config config{
    .input = eccdb::LefDefInput{
        .lef_files = {"technology.lef", "cells.lef"},
        .def_file = "design.def",
    },
};
auto database = eccdb::Database::open(config);
```

Read the native model and coordinate mutations through business services:

```cpp
#include <connectivity/ConnectivityService.h>

auto& context = database.context();
auto& netlist = context.design().netlistStorage();
eccdb::DesignNetId net = netlist.findRegularNet("clk");
const eccdb::DesignNet& value = netlist.net(net);
eccdb::ConnectivityService connectivity{context};
connectivity.renameNet(net, "clk_main");
```

Use native IDs for long-lived identity. Native references, spans and string
views are short-lived borrows and must be reacquired after relevant mutations.
The owning Database must outlive its Context and services. See the
[native API guide](doc/Native_API.md) for the ownership and move contract.

Design schema 13 stores independent InstanceTerm entities and rebuilds their
name/pointer caches after restore. Embedded-Term schema 12 snapshots are
rejected; reimport their original LEF/DEF inputs with this build.

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

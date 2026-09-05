# EccDB Integration Plan: Linking and Rust FFI

Chinese version: [Integration_Plan.zh-CN.md](Integration_Plan.zh-CN.md)

This document records the current linking choices and the planned integration
model for future iRT/iDRC and Rust applications. It is a roadmap, not a claim
that every planned boundary is already implemented.

> **Current status: exploratory API.** `eccdb_api` is not stable yet. It is
> still being used to explore and validate the public API boundary. LEF/DEF,
> routing, geometry, and rule coverage is incomplete, and the API names, data
> model, ABI, and exported CMake targets may change. It must not be treated as
> a stable third-party SDK or a long-term ABI. The dynamic-linking and Rust
> integration described here are target directions; consumers should pin an
> explicit source revision and expect interface changes.

Current unimplemented items:

- **TODO:** stabilize the public API and validate installation, export, and
  dynamic linking from a clean external project;
- **TODO:** define runtime deployment and version-compatibility rules for
  `libeccdb.so` across supported environments;
- **TODO:** add the CXX bridge and Rust wrapper. The repository does not yet
  provide a usable Rust crate.

## 1. Two access modes

### 1.1 Direct Store access: internal static linking

The current iRT and iDRC adapters use the internal Store classes directly:

```cpp
void setDesignSource(
    eccdb::DesignStore* design,
    eccdb::TechStore* tech,
    eccdb::LibraryStore* library);
```

Their CMake targets link internal EccDB targets such as:

```cmake
target_link_libraries(irt_interface PRIVATE
    eccdb_design
    eccdb_tech_database
    eccdb_library)
```

These Storage targets are ordinary CMake libraries and are currently built as
static archives in the main ecc-tools build:

```text
libeccdb_design.a
libeccdb_tech_database.a
libeccdb_library.a
```

The resulting relationship is:

```text
iRT/iDRC adapter
    -> internal Store and EnTT components
    -> EccDB static archives
```

This mode is useful for code that is part of the EccDB implementation or for
an experimental adapter that needs direct access to internal data. It is not a
stable external API because callers depend on Store layouts, EnTT entities,
component types, and storage partitioning.

### 1.2 Public API access: experimental dynamic-link target

The current public API target is built as a shared library:

```cmake
add_library(eccdb_api SHARED ...)
```

It produces:

```text
libeccdb.so
libeccdb.so.0
libeccdb.so.0.1.0
```

An external C++ program uses only the installed headers and the exported CMake
target:

```cpp
#include <eccdb/eccdb.h>
```

```cmake
find_package(EccDB CONFIG REQUIRED)
target_link_libraries(my_tool PRIVATE EccDB::eccdb)
```

The intended relationship is:

```text
external tool
    -> EccDB public Database/API
    -> libeccdb.so
    -> internal Store, pools, and EnTT
```

The caller receives public IDs, Data values, and controlled API operations. It
does not receive a mutable EnTT registry or internal Storage reference.

This only means that the current build contains a shared-library target. It does
not mean that the external dynamic-link workflow, installation package, runtime
search paths, or ABI compatibility policy is complete. A supported dynamic-link
workflow is still **TODO**.

## 2. TODO: Planned iRT/iDRC integration

The long-term boundary is for iRT and iDRC to consume the public API and link
the shared library:

```cmake
target_link_libraries(irt_interface PRIVATE EccDB::eccdb)
target_link_libraries(idrc_interface PRIVATE EccDB::eccdb)
```

Their interfaces should gradually move from Store pointers:

```cpp
eccdb::DesignStore*
eccdb::TechStore*
eccdb::LibraryStore*
```

to public database and identity types:

```cpp
void setDesignSource(eccdb::Database& database);
```

The migration must not link both `libeccdb.so` and a second copy of the same
EccDB Storage implementation into one tool. Having two registries or two
copies of the same database implementation can cause inconsistent state,
duplicate symbols, and lifetime problems.

The public API must first cover the operations required by routing and DRC:
net and pin connectivity, wire/path editing, via resolution, technology layer
rules, geometry queries, and controlled writeback. Until then, the existing
adapters may continue to use internal static Store targets.

## 3. TODO: Rust and CXX integration

Rust integration is not implemented yet. It only needs a thin Rust wrapper that
calls `libeccdb.so` through a CXX bridge; it does not need a second EccDB core:

```text
Rust application
    -> safe Rust EccDB wrapper
    -> CXX FFI bridge and small C++ shim
    -> libeccdb.so
```

### 3.1 TODO: Rust wrapper

The wrapper only translates the C++ public API into Rust ownership, error, and
borrowing semantics. It may expose only the subset needed by a renderer or
editor; it does not need to duplicate every C++ method. Database state, ID
semantics, Storage, and EnTT remain owned by `eccdb_api`.

### 3.2 TODO: Dynamic library deployment

The Rust program must find `libeccdb.so` at runtime. Development can use
`LD_LIBRARY_PATH`; the production choice between RPATH/RUNPATH and an
application-local library directory remains TODO.

The key rule is: **one authoritative EccDB state, one public ownership model,
and no Rust reimplementation of the database core.**

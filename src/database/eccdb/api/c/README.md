# Experimental ECCDB C ABI

The C facade calls the existing C++ Database API. It does not bypass database
validation or expose storage registries. This is an experimental ABI (SOVERSION 0).

## Build and consume

```sh
cmake -S src/database/eccdb -B build/eccdb-c-api -G Ninja \
  -DECCDB_BUILD_C_API=ON -DECCDB_BUILD_TESTS=ON
cmake --build build/eccdb-c-api --target eccdb_c eccdb_c_smoke eccdb_c_fixture eccdb_c_consumer eccdb_c_conversions
ctest --test-dir build/eccdb-c-api -R '^eccdb_c_' --output-on-failure
cmake --install build/eccdb-c-api --prefix /your/sdk
```

An external C project uses `find_package(EccDB CONFIG REQUIRED)` and links
`EccDB::c`. Include `eccdb_c/eccdb.h`. Deploy both libeccdb_c.so and libeccdb.so
from the same build. On Linux the installed C facade locates its C++ dependency
in the same library directory; the application must still locate libeccdb_c.so.
LEF/DEF input requires the optional importers, as with the C++ API;
`eccdb_supports_lef_def` reports availability. DEF output works in binary-only builds.

## Coverage

| C++ API capability | C ABI |
| --- | --- |
| Config/open, runtime polygon mode, multiple LEFs, binary cache | eccdb_config_t / eccdb_open; convenience open_binary/open_lef_def |
| Binary/DEF output, import diagnostics | write_binary, write_def, diagnostics |
| Net CRUD, regular/special lookup and enumeration | net_* |
| NetData and optional NetOptions fields | net_get/update, net_options_get/set |
| Net instance pins, IO pins, wires | net_pin_ids, net_wire_ids |
| Instance CRUD and pin lookup/enumeration | instance_*, instance_pin_* |
| InstancePin snapshots, connect, both disconnect overloads | instance_pin_* |
| IO pin CRUD and relationships | io_pin_* |
| Wire metadata and complete routing snapshots, CRUD | wire_* |
| DesignVia fixed and generated definitions, CRUD | design_via_* |
| C++ non-owning Ref conveniences | use IDs and snapshots; Ref pointers do not cross the ABI |

Generated VIA conversion includes rule/layer IDs, all sizes/spacings/enclosures,
row/column, origin, offsets, and cut_pattern. Wire paths include points,
extensions, virtual flags, via definition tags, masks, arrays, rectangles and
all optional path attributes. Optional values use independent has_* fields;
presence and numerical zero remain distinct. C++ semantic validation still
applies (for example a present NetPattern::kNone is rejected by storage).

## Ownership and errors

- IDs are database-local; invalid means UINT64_MAX, **not zero**.
- Initialize input Net/Instance/IOPin structs with their init functions to set
  absent IDs correctly. Zero-initialize lists and other output structs.
- Output buffers must be empty before a query; free previous results first.
- Input strings/arrays are borrowed during a call. Do not call an output free
  function on stack arrays or borrowed string literals.
- Queries return owned snapshots. Call the matching free function even after
  a failed query: partial outputs are releasable. Free does not destroy entities.
- Only use query results when status is OK and found is nonzero. A missing
  optional snapshot is OK with found=0; invalid operations return a status.
- Initialize Database* to NULL before open. Database-free accepts NULL.
- Exceptions are caught at the C boundary. Error text is thread-local, truncated
  to 2047 bytes, and valid until the next status call on that thread.
- Serialize calls involving the same database; no cross-thread synchronization
  or cross-database ID provenance check is added by this facade.

The independent `tests/consumer.c` has its own main, includes only the C header,
and links the shared library. A separate C++ fixture producer creates real binary
inputs. The consumer exercises editing, relationships, generated vias, routing,
binary reopen and DEF export. White-box conversion tests separately verify
Config mapping and optional zero values that storage may disallow.

Validation in the binary-only build: five CTest tests pass (smoke, export
symbols, conversions, fixture, independent C consumer). Installation and a
separate C-only CMake consumer have also been exercised. ASan/UBSan checks of
the facade and consumer pass with leak detection disabled; LeakSanitizer
cannot run under the execution environment's ptrace. The linked C++ database
library was not sanitizer-instrumented. Multi-LEF/runtime/cache conversion is
tested, but LEF/DEF import itself is not exercised by this binary-only build.

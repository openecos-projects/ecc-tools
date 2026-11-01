# EccDB Operation adaptors

These adaptors connect EccDB to the Operation tools. An adaptor reads EccDB
into the tool's working model and writes results through EccDB services where
the tool updates a design. Tool algorithms consume their own working models.

iFP, iCTS, iRT, iDRC, iSTA, iPW, iEMIR, iLVS and iMJ expose database-neutral input hooks. Their EccDB
Context, projection and binding logic lives here, rather than in the Operation
interface. Link the appropriate adaptor target and bind the caller-owned
Context for the duration of the run:

| Tool | Header under `adapters/` | Binding API | Target |
|---|---|---|---|
| iFP | `ifp/FPInterfaceEccdb.h` | `bindFpSource(tool, context)` | `eccdb_ifp_adapter` |
| iSTA | `ista/STAInterfaceEccdb.h` | `bindStaSource(tool, context, inputs)` | `eccdb_ista_adapter` |
| iPW | `ipw/PWInterfaceEccdb.h` | `bindPwSource(tool, context, inputs)` | `eccdb_ipw_adapter` |
| iCTS | `icts/CtsEccdbAdapter.hh` | `eccdb::adapter::icts::CtsEccdbAdapter` passed to `CTSAPI::setDesignSource` | `eccdb_icts_adapter` |
| iRT | `irt/RTInterfaceEccdb.h` | `bindRtSource(tool, context)` | `eccdb_irt_adapter` |
| iDRC | `idrc/DRCInterfaceEccdb.h` | `bindDrcSource(tool, context)` | `eccdb_idrc_adapter` |
| iEMIR | `iemir/EMIRInterfaceEccdb.h` | `bindEmirSource(tool, context)` | `eccdb_iemir_adapter` |
| iLVS | `ilvs/LVSInterfaceEccdb.h` | `bindLvsSource(tool, context, reference)` | `eccdb_ilvs_adapter` |
| iMJ | `imj/MJInterfaceEccdb.h` | `bindMjSource(tool, context)` | `eccdb_imj_adapter` |

Pass a null Context to detach and restore the tool's iDB default. iLVS also
borrows an independent logical reference for the run; `wrapLvsNetlist(context)`
projects that reference without modifying the physical database. The source
and reference must outlive the binding. Detach after both success and failure.
iDRC keeps its per-run net-name snapshot in its adaptor source; standalone
queries use the live design after the run is torn down.

The `bind*Source` functions are in `eccdb::adapters`. For iCTS, clear its source
with `setDesignSource(nullptr)`. iRT's binder also
binds the iDRC source used inside routing to the same Context. Complete or abort
the routing session before detaching both sources with `bindRtSource(tool,
nullptr)`. Its `RTDesignSource` contract fills the existing router work model;
the concrete implementation and Context lifetime live in the adaptor library.
The wrapper comparison lives under `tests/adapters/irt` and is not compiled
into the production tool or Python extension. Tests and native benchmarks
explicitly link the adaptor.

iFP sources fill and write the existing floorplan work model; keep the source
bound through `destroyFP()`, which publishes the result, then detach. Failed
sessions use `abortFP()` without writing results. iSTA/iPW bind a database wrapper
and retain their existing explicit timing/power inputs. The caller owns the
Context, Liberty libraries and SPEF/VCD readers until the session is destroyed or
aborted; detaching clears the wrapper and borrowed inputs. Operation libraries
never link these concrete adaptors. These source-boundary migrations do not
change rule checking, IR solving, alias matching, or the disabled status of the
native iRCX bridge.

The experimental OpenROAD/TritonRoute bridge, external patches and local build
scripts are maintained outside this repository. They are not Operation adaptors.
The independent OpenDB replay projections belong to `../benchmarks/odb/`.

The EccDB-only antenna graph, numerical implementation and internal run summary
live in `imj/antenna/` and are compiled by `eccdb_imj_adapter`. The legacy iDB
`AntennaChecker` remains identical to upstream. `imj/AntennaIdb.*` runs that
checker once and reads its existing structured `-step_json` output; coverage
diagnostics and the differential result conversion stay in the adaptor. It
does not parse human-readable logs or run the EccDB numerical kernel over iDB.
Legacy CLOCK-net and RECT-route omissions remain visible in differential tests.
The two antenna backends do not share an algorithm implementation.

The legacy iDB metal-fill path keeps the upstream `MetalInserter` unchanged.
The EccDB path owns its input projection, geometry/spacing source, fill planner
(`imj/metal/MetalFillKernel.*`) and native Fill writeback in this adaptor.
The planner follows the legacy calculation, but the two backends do not share
an algorithm implementation; differential tests check their results. Operation
does not include or call the native planner.

Use `EccDB` for the database name in documentation and diagnostics, `Eccdb`
in C++ identifiers, and `eccdb` in variables and paths. Backend-specific input
and output helpers use `FromEccdb` and `ToEccdb`; interface implementation files
use `InterfaceEccdb.cpp`. For example:

```cpp
void wrapDatabaseFromEccdb();
void outputToEccdb();
```

EnTT is an implementation dependency, not the backend's name. Keep `entt::`,
EnTT header paths and terminology describing entity storage unchanged.

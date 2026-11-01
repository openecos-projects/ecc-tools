# `ecc_py.eccdb`

This optional Python submodule owns one native `eccdb::Database` and runs
EccDB-aware Operation adapters against that same in-memory design.  It is
compiled when the host builds the native EccDB database target; legacy top-level `ecc_py` iDB
commands keep their existing behavior.

```python
import ecc_py

db = ecc_py.eccdb.open_lef_def(
    ["tech.lef", "cells.lef"],
    "placed.def",
)

# Every stage borrows db.context() only for the call.  The resulting edits are
# committed to the same db and can be consumed by the following stage.
ecc_py.eccdb.run_fp(db, "floorplan.json")
ecc_py.eccdb.run_cts(
    db,
    "cts.json",
    {"liberty_files": ["cells.lib"], "sdc_file": "constraints.sdc"},
)
ecc_py.eccdb.run_rt(db, "rt.json", {"thread_number": 32})
ecc_py.eccdb.run_drc(db, "drc.json")

print(db.info_json())
db.write_def("routed.def")
db.write_binary("technology.eccdb", "library.eccdb", "design.eccdb")
```

`supports_lef_def()` reports whether this build includes the native LEF/DEF
reader before a script attempts text import. `Database.info()` returns the
same compact diagnostic summary as a Python dictionary; `info_json()` returns
the raw JSON payload for Tcl/Python log parity.

`run_tool(db, tool, config=None, options=None)` is the generic entry point.
Supported tools are reported by `supported_tools()`.  `options` must be a
regular JSON-compatible Python dictionary; paths inside it are strings.
`run_fp`, `run_ifp`, `run_cts`, `run_rt`, `run_drc`, `run_sta`, `run_pw`,
`run_emir`, `run_mj`, and `run_lvs` are convenience aliases. Native RCX is deliberately
outside this boundary; pass an external SPEF to STA or power analysis instead.

LVS compares the physical database with an **independent, post-synthesis
Verilog reference**:

```python
ecc_py.eccdb.run_lvs(db, options={
    "reference_verilog": "reference.v",
    "top_module": "top",  # optional; otherwise infer the Verilog top
    "temp_directory_path": "out/lvs",
    "thread_number": 8,
})
# Reports: out/lvs/lvs_reporter/ilvs.rpt and ilvs.json
# Equivalent: db.run_tool("lvs", options={...})
```

The physical database can come from LEF/DEF or an EccDB binary snapshot.
The reference is imported into a separate temporary DesignStore sharing the
already-loaded technology and library. It never replaces the physical design
or obtains its connections from that design. Unknown masters, invalid Verilog,
and a reference top differing from the physical design raise an exception.
As with the legacy LVS API, returning normally means that checking completed;
read the report to determine whether LVS passed. A detected discrepancy does
not itself raise an exception. The tool recreates its temporary directory;
use a dedicated output directory, keeping input files elsewhere.

The reference importer supports structural Verilog with LEF-resolved cell
masters; Liberty/behavioral RTL is not a substitute for the mapped netlist.
No reference import or physical wrapping falls back to iDB. Legacy top-level
`lvs_verilog_init`, `init_lvs`, `run_lvs`, and `destroy_lvs` remain separate.

iMJ selects its operation through options and writes edits to the same database:

```python
ecc_py.eccdb.run_mj(db, options={
    "operation": "antenna",
    "temp_directory_path": "out/mj-antenna",
    "antenna_report_file": "out/antenna.report",
})
ecc_py.eccdb.run_mj(db, options={
    "operation": "filler",
    "filler": "FILL2 FILL1",
    "temp_directory_path": "out/mj-filler",
})
ecc_py.eccdb.run_mj(db, options={
    "operation": "metal",
    "min_fill_layer": "M1",
    "max_fill_layer": "M1",
    "temp_directory_path": "out/mj-metal",
})
db.write_def("out/edited.def")
```

The MJ temporary directory is recreated by the tool. Give each stage its own
output directory. In this checkout, PW uses the averaged `runPW()` API;
`peak=True` raises an error because the newer peak-flow API is unavailable.

EccDB binary files are an EccDB-specific three-file snapshot.  They are not
interchangeable with iDB's archive formats.  Liberty, SDC, SPEF, and power
inputs remain explicit tool options because they are not part of an EccDB
physical design snapshot.

`ecc_py.eccdb.Database` intentionally has its own Python binding from the GUI
extension's `eccdb_viewer.Database`, so both modules can be imported in either
order.  Create a database through the module whose API you intend to use;
database objects are not passed between those two extensions.

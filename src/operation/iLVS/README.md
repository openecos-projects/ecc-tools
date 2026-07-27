# iLVS: Layout Versus Netlist Checking

iLVS checks post-route layout-versus-netlist consistency after routing and
DRC. The active implementation compares value-owned netlist and DEF views,
checks routing and power connectivity, and writes RPT and JSON reports. The
previous implementation is retained under `src/operation/refactor/iLVS` and
is not part of the default build.

The active Tcl lifecycle is:

```tcl
init_lvs ?-temp_directory_path <directory>? ?-thread_number <integer>?
run_lvs
destroy_lvs
```

`run_lvs` executes EntityChecker, RoutingChecker, PDNChecker, and
LVSReporter in that order. Current IDB accessors temporarily provide the same
DEF-backed view as both inputs until IDB supports independent logical and DEF
views; this exercises the active flow but is not a replacement for a reference
netlist comparison.

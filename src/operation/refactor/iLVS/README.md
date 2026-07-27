# iLVS: Layout Versus Schematic

## Background

iLVS is the post-route LVS operation in iEDA. It compares a logical netlist
view with a routed DEF view after routing and DRC. The primary flow is
in-memory: after `def_init`, `init_lvs` builds the two views through named IDB
accessors, then `run_lvs` reports LVS results without `.bin` files.

IDB currently retains one design view. The temporary netlist and DEF
accessors therefore both return the current DEF-backed `IdbDesign`, which
keeps the direct flow runnable while making the future two-view boundary
explicit. When IDB supports concurrent views, only those accessors need to
change. The snapshot write/read commands remain available as a legacy,
independent-process compatibility flow.

## Software Structure

### API: iLVS Tcl and C++ interfaces

The interface owns the `init_lvs`, `run_lvs`, and `destroy_lvs` lifecycle.
`init_lvs` initializes DataManager, which builds its database through
`LVSInterface::wrapDatabase()`. `run_lvs` initializes, runs, and destroys
LVSChecker followed by LVSReporter. NetlistExtractor and LVSSnapshotIO are
also created and destroyed by the direct wrapper or their snapshot action.
`destroy_lvs` releases DataManager. `read_lvs` can replace the direct values
with a legacy snapshot pair for that run.

### Data Manager: Top-level data manager

The data manager owns configuration, the two value views, the check
result, report output paths, and the per-module temporary directories.

### Module: Main LVS modules

- NetlistExtractor builds direct and legacy extracted values at its call site.
- LVSSnapshotIO reads and writes legacy binary values at its call site.
- LVSChecker compares entities and checks routed-net and power connectivity.
- LVSReporter produces the console, RPT, and JSON LVS reports.

### Utility: Tool modules

- Logger: Log module.
- Monitor: Runtime status monitor.
- Utility: Configuration, filesystem, and table helpers.

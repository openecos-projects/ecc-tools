# Driver Analysis and DEF Cutting

Load LEF and a design using the existing IDB commands, then initialize iMJ:

```tcl
tech_lef_init -path tech.lef
lef_init -path cells.lef
def_init -path design.def
# Or: verilog_init -path design.v -top top
init_mj -temp_directory_path ./mj_result
analyze_driver
# Only for a physically placed DEF; coordinates are microns.
cut_def -rect {10 10 40 40}
destroy_mj
# Export the cropped design already in IDB.
def_save -path ./cropped.def
netlist_save -path ./cropped.v
```

- `analyze_driver` takes no arguments and writes
  `driver_analyzer/driver_report.rpt`. It always uses the active design loaded
  from DEF or ordinary Verilog, never the separate LVS reference netlist.
- Top-level inputs and instance outputs are structural drivers. A module-local
  pin-net graph preserves those known roles and propagates the single-driver
  constraint: one known driver forces unknown peers to loads; no known driver
  and exactly one unknown candidate forces that pin to a driver. Processing
  repeats until no further roles can be inferred. Multiple candidates remain
  unresolved; multiple known drivers remain a multi-driver finding.
- The graph shares nodes by pin identity and deduplicates repeated connections.
  It does not invent connections between different pins of a cell. Inferred
  directions are conditional on the single-driver assumption, not independent
  proof that a bidirectional circuit is electrically valid. Original IDB pin
  directions are unchanged. `InferredPins` counts distinct inferred nodes, and
  affected nets include `inferred from inout/unknown` annotations in the report.
- POWER/GROUND connectivity is excluded before graph construction. A net is
  excluded if its IDB net type or any connected IO/instance terminal type is
  POWER/GROUND. These attributes come from DEF/LEF `USE` and Verilog
  `supply1`/`supply0`; ordinary Verilog wires connected to LEF PG terminals are
  excluded too. Same-name ordinary/special records are excluded together and
  counted once in `ExcludedPGNets`. Names such as VDD/VSS alone, or storage in
  SPECIALNETS alone, do not establish PG status. Untyped supplies with no PG
  terminal evidence cannot be recognized from connectivity alone.
- Multiple tristate outputs remain multiple structural drivers; this command
  does not prove enable exclusivity.
- Pure IO-cell boundary nets are also excluded before graph construction: one
  distinct top-level IO pin connected only to one terminal of a LEF `CLASS PAD`
  cell represents the package boundary, not core routing. The terminal need not
  be named PAD. Same-name ordinary/special net records are merged and duplicate
  pins removed for this decision. `ExcludedIOBoundaryNets` counts these nets
  separately from PG nets; `SignalNets` counts only nets actually analyzed.
  Direct top-level IO to core nets, PAD-to-core nets, and nets with additional
  terminals or ports remain in analysis. No IDB objects are removed or changed.
- `cut_def -rect {llx lly urx ury}` replaces the active IDB design with the
  cropped result and writes `def_cutter/cut_report.rpt`. It does not write an
  intermediate DEF. Use `def_save` and `netlist_save` to export the result;
  no `def_init` reload is needed, including after `destroy_mj`.
- The LEF technology and separate LVS reference netlist are preserved. Repeated
  cuts operate on the current cropped design, not on the original input.
- The supplied rectangle is always the new DIEAREA and must lie inside the
  input die. There is no boundary option or core mode. Complete row sites are
  retained on the original site grid, rounded inward at the crop edges.
- Coordinates remain absolute. Only complete placed instances and complete IO
  ports are retained. Rows/tracks are cropped on their existing grids. Ordinary
  and special routing, rectangular blockages, fills, regions and slots are
  clipped. Complete vias are retained. For a partially clipped via, its cut is
  removed while its in-die routing enclosures remain as metal and IO access.
- All removed terminals on the same ordinary net share one logical IO. For example,
  internal A connected to external B/C/D produces one IO, not three. Reuse a
  retained top-level IO when available. An external driver produces an INPUT;
  internal drivers driving external loads produce an OUTPUT. Internal-only
  nets do not gain IOs.
- SPECIALNETS do not generate or extend IOs, regardless of their USE. Same-name
  ordinary net records are excluded too, even if the special record itself is
  outside the crop. Retained original IOs, instance connections and clipped
  special routing are preserved.
- Boundary routing cuts on the selected layer supply IO shapes where available.
  Multiple shapes are PORTs of one IO, not separate IOs. Unrouted IOs are
  placed inside and flush with the new die boundary, on DEF tracks (or LEF
  pitch/offset when no DEF tracks exist), using layer direction, minimum width,
  minimum area and spacing. Retained metal, vias, IO, routing blockages and fill
  reserve their space. Insufficient legal edge capacity fails before updating
  IDB; the cutter does not overlap pins or enlarge the die to make them fit.
- The new IO layer is the mean metal-layer position of all distinct net pins,
  inside and outside the crop. Routing layers are ordered by LEF stack order,
  not by their names or by an interleaved metal/CUT layer index. Each multilayer
  pin first averages its unique routing layers, including pin-via metal, then
  all pins have equal weight. Round to the nearest metal layer, ties upward.
  Unplaced terminals still provide their LEF/DEF port layers. CUT layers never
  participate. Missing terminal layer data or exhausted selected-layer slots
  fails without choosing another layer.
- Ordinary routing-only nets without terminals average their boundary metal
  layers. Existing retained IO geometry is preserved. Ordinary PG nets and
  unknown directions use INOUT.
- The result has explicit ports and connectivity for DEF and Verilog export.
  It does not synthesize the deleted logic or reroute the retained metal: the
  new ports describe the interface to the removed environment. A newly placed
  IO without a pre-existing routing cut is logically connected, not newly
  routed. Metal on non-selected layers remains routing metal rather than IO
  geometry; no via is synthesized to connect it to the new IO. Added/extended
  IOs, selected layers and shapes are listed in the report.
- The current geometry path supports rectangular DIEAREA and Manhattan
  routing represented by IDB. Unsupported diagonal routing fails explicitly.
  Output uses the IDB DEF writer; geometry is preserved as clipped metal
  rectangles rather than preserving the original textual route segmentation.
- Cropping uses the information materialized by IDB, not a lossless DEF text
  transform. For example, IDB currently does not populate group member callbacks;
  it cannot reconstruct those memberships from an imported DEF.

Python uses the same flags and implementation:

```python
ecc_py.init_mj(config_dict={"-temp_directory_path": "./mj_result"})
ecc_py.analyze_driver()
ecc_py.cut_def(config_dict={"-rect": "10 10 40 40"})
ecc_py.destroy_mj()
ecc_py.def_save("./cropped.def")
ecc_py.netlist_save("./cropped.v")
```

For commands with configuration options, an optional JSON file uses the existing
top-level `MJ` object with string flag values. Nonempty `config_dict` values
override JSON values. `analyze_driver()` has no configuration arguments.

Regression:

The C++ regression is a standalone CTest executable using only the small LEF,
DEF and Verilog fixtures in this directory's `data/`. It does not need an
external PDK or a Python extension. From the repository root, with the normal
project build dependencies installed:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target test_imj_design_tools --parallel 2
ctest --test-dir build -R '^test_imj_design_tools$' --output-on-failure --no-tests=error
```

CI runs these same build/test commands after the wheel build, reusing the
already compiled libraries. A missing CTest registration is an error rather
than a successful empty test run.

The IO-placement regression compares indexed candidates against an exhaustive
spacing check for fixed spacing and a non-monotonic width/PRL table. The index
only filters nearby candidates; exact spacing, candidate order and distance
tie-breaking remain unchanged.

The Python binding smoke test can also be run against a local extension build:

```sh
cmake --build build --target ecc_py
PYTHONPATH=bin python3 src/operation/iMJ/test/test_design_tools/test_bindings.py
```

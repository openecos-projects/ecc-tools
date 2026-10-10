# Physical net names across DEF and Verilog

## Scope

DEF has separate namespaces for ports, nets and instances. Verilog does not:
a declaration of port `y` also declares an internal signal `y`. After output
buffering, DEF can contain an internal net `y` even though port `y` connects to
the buffer's output net. Exporting that design must not short the buffer or
rename only its Verilog connection.

## API and call boundary

```cpp
std::size_t IdbDesign::canonicalizeNetNames();
std::string IdbDesign::makeUniqueNetName(const std::string& prefix) const;
```

DEF (plain and gzip) and Verilog imports call `canonicalizeNetNames()` after
all connections have been built. Native operations that change connectivity
must call it at the end of their topology update, before constructing or
rebuilding timing/parasitic views. Do not call it during partial DEF parsing:
later statements still refer to the original names.

## Contract

- A net may share a port's name only if that port connects to the same net.
- Net names must not collide with instance names. Fresh `__ecc_net_<n>` names
  reserve all existing port, net and instance names, including unused names.
- Renaming uses `renameNet()`: the net object, its terminals, wires and other
  attributes stay intact; the lookup index and pin name caches change together.
- A single inout port and its net share the port name. Multiple inout aliases
  still require an unsupported bidirectional Verilog connection.
- The return value counts renames. A second call on an unchanged design returns
  zero. `makeUniqueNetName()` avoids all three namespaces for newly created nets.
- Saving DEF or Verilog does not mutate iDB or create a private net-name mapping.
  Keep SDC/SPEF and timing caches aligned with the canonical design: do not
  normalize names after loading an external SPEF or constructing timing views.

Verilog assignment collapsing retains the physical name referenced by leaf-cell
connections before choosing a port-only alias. Ties prefer internal names, then
assignment sources, then original declaration order. Constant nets retain the
existing constant convention and driver validation. This supports both
`assign output_port = physical_net` and `assign physical_net = input_port`.

## Validation and errors

| Input state | Behavior |
| --- | --- |
| Port `clk` connects to net `clk` | Preserve both names |
| Port `y` connects to `buffer_out`, internal net is named `y` | Rename the internal net in iDB |
| Net and instance both named `hold22` | Rename the net in iDB |
| `assign eoi_0 = mem_addr_0`, cell drives `mem_addr_0` | Keep physical net `mem_addr_0` |
| Net conflicts introduced after import, without finalizing the edit | Verilog export reports the conflict; no private rename |
| Multiple aliased inout ports, conflicting constant or assignment drivers | Existing unsupported-driver/connection errors remain |

## Examples

Base case: `input clk; BUF u(.A(clk));` retains net `clk`.

Good: finalize a native buffer insertion with `canonicalizeNetNames()`, then
build timing views and export both DEF and Verilog from that iDB.

Bad: write DEF with net `y`, then independently rename the same net to
`__ecc_verilog_net_0` in the Verilog writer. Terminal connectivity can be identical
while a name-based LVS comparison reports two missing nets.

## Regression checks

`verilog_alias_names_test` checks assignment order, output aliases, input aliases
and physical-name preservation. `verilog_def_names_test` checks imported
port/net and instance/net collisions, buffer insertion, inout naming, occupied
generated names, lookup consistency and idempotence. Both compare complete
net-name-to-terminal maps after DEF and Verilog export/reimport, including gzip,
and check that saving does not mutate the live design.

```sh
cmake --build build --target verilog_net_names_test verilog_import_test verilog_roundtrip_test
ctest --test-dir build -R 'verilog_(alias_names|def_names|import|roundtrip)_test' --output-on-failure
```

## Wrong versus correct

Wrong: fix the LVS report by ignoring names, or rename inside a writer after
STA has cached the old names.

Correct: establish one physical net name in iDB before downstream analysis,
preserve that name when collapsing Verilog aliases, and export the same name
in both formats.

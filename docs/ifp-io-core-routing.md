# IO-core power routing policy

The dedicated `PDNGenerator::buildIOCoreConnect()` connector currently permits
only collinear, same-layer `direct_extension` and `straight_bridge` paths.
Manhattan bends, same-layer detours and layer-change detours are disabled.
This is a temporary restriction while their corner/via geometry is repaired;
their candidate implementations are retained but excluded from route selection.
There is no configuration switch to re-enable them.

The restriction applies to the IO power pin selected by the IO-cell definition.
It does not change the separate macro-PG connector or ordinary signal routing.
Existing width, grid, obstacle, spacing and contact checks still apply.

Use the existing strict configuration for chip-level verification:

```json
"io_core_connect": {"enable": true, "fail_on_unresolved": true}
```

Blocked, misaligned or out-of-core pads without a legal straight connection
remain unresolved in `pdn_generator/io_core_connect.rpt`. Strict mode fails
instead of falling back to a bend. Setting `fail_on_unresolved=false` does not
waive the separate PLOC requirement for a connected IO power cell on each
selected supply net. An all-unconnected supply still prevents PLOC export.

The report records `policy=straight` and `outside_core_range` independently;
an out-of-core pad does not mean a Manhattan route was generated. A successful
connection report is not a DRC pass. Re-enabling non-straight candidates needs
both physical connectivity and corner/adjacent-via DRC validation.

`test_ifp_pdn_generator` exercises legal straight extensions/bridges and the
blocked, offset and out-of-core cases. It checks emitted geometry and unresolved
reports, including strict-connectivity and downstream PLOC failure behavior.

# iRT EccDB Adapter Tests

`irt_adapter_differential_test` verifies the EccDB iRT source adapter against
the legacy iDB input path. It is a test-only consumer of
`eccdb_irt_adapter`.

The suite covers wrapper semantics, routed writeback, and generated/design Via
handling. Its source-lifecycle case verifies that binding an EccDB source also
binds the nested iDRC source, a partial Store binding is rejected without
replacing the active source, and detaching returns both tools to the legacy
iDB path.

`IrtWrappedDatabaseCompare` compares materialized iRT databases and normalizes
compared collections in place. Pass copies when the original order must remain
available for routing; the router does not call this test helper.

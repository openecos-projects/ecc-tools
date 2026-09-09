# EccDB LEF58 parsing internals

The native import path is SI2 `lefr` callbacks → EccDB syntax values →
DBU conversion and staged rules → EccDB storage. The DEF importer independently
uses SI2 `defr`. Neither native importer converts through an iDB object.

This directory owns three parser families in
`eccdb::lef_detail::grammar::{layer,routing,cut}`. Routing and cut syntax values
describe LEF text (micron-valued numbers, optional qualifiers and table rows).
They are transient values, separate from persistent `TechRouting*` and
`TechCut*` components. They contain no registry IDs or iDB objects.
The importer uses the layer parser for TYPE/BACKSIDE and the property preparer
for rule parsing and unit conversion. These headers are internal and are not
installed as public API.

The boundary follows the local OpenDB implementation in
`OpenROAD/src/odb/src/lefin/lefLayerPropParser.h`,
`WidthTableParser.cpp` and `lefTechLayerCornerSpacingParser.cpp`:
property grammars belong to the database importing them, and a full parse must
succeed before a rule is retained. EccDB keeps its existing staging/commit
mechanism rather than adopting OpenDB rule objects. No OpenDB library is linked.

The existing grammar implementations formerly shared with the extended iDB parser were moved
here with their existing license notices. This migration preserves the existing
grammar coverage and typed-rule materialization; it does not claim full LEF58
grammar support. Unsupported properties retain the existing importer behavior.
Original `TechProperty` text is still preserved independently of typed rules.
Parser functions report failure to the EccDB importer rather than logging
through the legacy parser.

Validate independence with `ECCDB_STANDALONE_LEF_DEF=ON` and
`ECCDB_STANDALONE_LEGACY_IDB=OFF`. The native LEF importer/exporter and binary
archive tests exercise extension fields even when the legacy iDB adapter is not
built. iDB differential comparisons must use the fields materialized by the
legacy reader; missing iDB fields are not missing EccDB parser features.

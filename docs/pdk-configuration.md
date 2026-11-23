# Explicit PDK inputs

STA accepts `STA.-cell_catalog` in its JSON configuration (or `-cell_catalog`
in the dictionary interface). The file is whitespace-separated TSV with one
`master family vt drive_rank` row per cell. Comment lines begin with `#`.
Names must be single tokens; ranks must be finite and nonnegative. Duplicate
masters and malformed rows are errors. ECC generates this file from its frozen
PDK description.

Family labels are candidate equivalence groups, not proof of equivalence. The
exporter additionally compares pin directions, output Boolean expressions and
arc signatures. Sequential cells, macros and entries without output functions
remain singleton families. Without a catalog, names are not parsed to guess a
process, VT or drive family.

Timing path constant sources are recognized through constant Liberty output
functions, not TIEHI/TIELO name prefixes. EM limits refer to metal or via rules
already declared in the technology model; their names do not determine type.

RC extraction still has distinct native and ICS55-specific adapters. Selecting
an adapter does not qualify it for arbitrary technology; callers must provide
the matching model. The ICS55-specific binary is not a generic process model.

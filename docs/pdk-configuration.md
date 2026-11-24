# Explicit PDK inputs

Cell equivalence families, their VT (voltage-threshold) axis and their drive-strength
axis are derived entirely from the loaded Liberty libraries, not from cell naming
conventions or an external catalog file.

A family groups cells with the same output Boolean function, the same pin directions
and the same arc signature (sense, trigger, check type). Sequential cells, macros and
cells without an output function each stay in their own singleton family: this
interface cannot prove equivalence of state or reset behavior from Liberty alone.

Within a family, the VT axis is the cell's source Liberty library
(`LibertyCell::get_library_name()`), ranked by that library's lowest leakage in the
family. The drive axis is the cell's footprint (area) plus a strength rank among
same-library, same-area cells, ranked by the fastest (lowest-resistance) variant's
drive resistance; drive resistance is the delay-table slope over the load axis at the
median input slew, taken as the maximum over the cell's non-check delay arcs.

Timing path constant sources are recognized through constant Liberty output functions,
not TIEHI/TIELO name prefixes. EM limits refer to metal or via rules already declared in
the technology model; their names do not determine type.

RC extraction still has distinct native and ICS55-specific adapters. Selecting an
adapter does not qualify it for arbitrary technology; callers must provide the matching
model. The ICS55-specific binary is not a generic process model.

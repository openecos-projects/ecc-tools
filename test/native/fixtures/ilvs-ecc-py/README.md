# Native EccDB iLVS Python fixture

`design.def` is the physical layout for `top`: two placed `BUF` instances,
two top-level ports, and three connected M1 nets. `reference.v` is the
independent matching structural source. `mismatch.v` changes only the
reference connection at `U1/A`; it must produce an entity/net difference
without changing the physical EccDB.

The fixture intentionally contains no power or ground net. It isolates the
native EccDB physical-to-iLVS adapter and the independent Verilog reference
projection from PDK-specific PG conventions.

# Native extraction API fixture

This synthetic seven-layer process exercises the native process reader, layer mapping,
resistance and capacitance extraction, SPEF output, and timing refresh without an
embedded process library. Both standard-cell variants use the same fixture.

All layer parameters and capacitance samples are fixed test constants. They are not
calibrated to a foundry process and do not measure extraction accuracy. No DEF or
reference SPEF is used to construct or adjust these inputs.

`nominal.captab` enumerates every lower/upper context in the fixed layer stack at
spacings 0.1, 1 and 10 um. The corresponding coupling/ground coefficients are
(0.1, 0.05), (0.01, 0.06) and (0.001, 0.07) fF/um.

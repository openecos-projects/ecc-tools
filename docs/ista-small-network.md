# Small-network cell model

`init_sta -small_net_limit_ps 2` enables the capacitive cell approximation for
networks whose total interconnect resistance times total load capacitance is
strictly below 2 ps. The default is 2; zero disables the approximation. The same
`-small_net_limit_ps` option is available through the C++ and Python configuration
maps. Values must be finite and nonnegative.

The limit is a configurable engineering policy, not a universal error bound.
Increasing it can change both cell and net results; 5 ps is not equivalent to
2 ps. Compare the intended design and libraries before changing it.

For eligible networks, the cell delay and transition tables are evaluated at
the total load capacitance. No source correction factor is applied. Each
receiver uses its own first RC moment from the existing network model to form
a one-pole response to a linear ramp with the library's output slew. Its delay
is measured relative to the ramp's midpoint, and its transition uses the
receiver library's slew derate. Different branches therefore retain distinct
delays. This receiver model is an approximation, not a complete distributed
waveform solution.

The approximation requires a complete passive single-driver network, a 50%
delay threshold, symmetric slew thresholds, and compatible source/receiver
voltage and threshold conventions. Unsupported networks, missing table data,
or failed numerical calculations use the existing distributed solver.

The resistance is converted from ohms to kOhms and capacitance is in pF, so the
model's RC product is in ns. The configuration value in ps is converted to ns
once for comparison. Rise and fall loads are evaluated separately.

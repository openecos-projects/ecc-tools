# iPW clock source object identity

`create_clock` and `create_generated_clock` preserve the object type returned
by `get_ports`, `get_pins` and `get_nets`. A query result is an exact object,
not another wildcard or leaf-name query.

For example, this remains valid when a macro Liberty also has a pin named
`clock`:

```tcl
create_clock -name clock -period 10 [get_ports clock]
create_generated_clock -name divided -master_clock clock \
    -source [get_ports clock] -divide_by 2 [get_pins u_div/Q]
```

Previously, iPW converted those objects to strings and searched all object
classes again. A macro library pin with the same leaf name could then cause
`clock sources must be ports, pins, or nets: clock`. It could also expand a
literal bus/metacharacter name or confuse a selected net with a same-named port.

The resolver now validates tagged pins/ports by their canonical key, and
tagged nets resolve to their driver pins. Other object types and stale source
objects remain errors. Plain textual source names prefer an exact existing
object before using the existing pattern lookup. Virtual clocks are unchanged.

Regression coverage:

```sh
cmake --build build --target test_ipw_sdc_clock_source
ctest --test-dir build -R '^ipw_sdc_clock_source$' --output-on-failure
```

The test includes a top port and macro Liberty pin sharing a name, net/port
name collisions, primary-clock redefinition, generated clocks, literal bus and
wildcard characters, textual patterns, and invalid source types. This repair
does not change the power model or timing constraints.

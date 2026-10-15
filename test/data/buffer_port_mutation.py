"""Exercise port-name ownership when native buffering moves an output load."""

from ecc_tools_bin import ecc_py


def assert_output_port_buffering(place_db, action_template, action_digest):
    names = list(map(str, place_db.net_names))
    pins = list(map(str, place_db.pin_names))
    for net_id, name in enumerate(names):
        driver = int(
            place_db.flat_net2pin_map[int(place_db.flat_net2pin_start_map[net_id])]
        )
        loads = [int(pin) for pin in place_db.net2pin_map[net_id] if int(pin) != driver]
        if any(pins[pin] == name for pin in loads):
            break
    else:
        raise AssertionError("fixture needs an output port on a same-name driven net")

    actions = [
        dict(
            action_template,
            net_name=name,
            driver_pin_name=pins[driver].replace(":", "/"),
            load_pin_name=name,
            downstream_pin_names=[name],
        )
    ]
    committed = dict(place_db.apply_buffer_actions(actions, action_digest(actions)))
    if not committed["ok"] or committed["accepted_count"] != 1:
        raise AssertionError(f"output port buffering failed: {committed}")

    fresh = ecc_py.pydb(ecc_py.get_dmInst(), 4, 4, False, False)  # noqa: FBT003
    net_names = list(map(str, fresh.net_names))
    pin_nets = {
        str(pin): net_names[int(fresh.pin2net_map[index])]
        for index, pin in enumerate(fresh.pin_names)
    }
    buffer = committed["committed_buffer_instance_names"][0]
    downstream = committed["committed_downstream_net_names"][0]
    upstream = pin_nets[pins[driver]]
    expected = {name: downstream, buffer + ":A": upstream, buffer + ":Y": downstream}
    if {pin: pin_nets[pin] for pin in expected} != expected or name in net_names:
        raise AssertionError(
            "output buffering lost connectivity or left a detached net in the port namespace"
        )
    return committed

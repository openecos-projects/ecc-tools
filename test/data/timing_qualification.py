"""Check real PyPlaceDB qualification after native export and rebuild."""


def assert_endpoint_qualification(pydb):
    assert int(pydb.timing_schema_version) == 2
    names = [str(name) for name in pydb.pin_names]
    endpoint_valid = {}
    for pin, valid, reasons in zip(
        pydb.end_points, pydb.endpoints_max_valid, pydb.endpoints_max_reason, strict=True
    ):
        assert len(valid) == len(reasons) == 2
        assert list(valid) == [int(reason) == 0 for reason in reasons]
        endpoint_valid[int(pin)] = list(valid)
    for arcs, valid_rows, reason_rows, clock_local in (
        (
            pydb.endpoints_constraint_arcs,
            pydb.endpoints_constraint_max_valid,
            pydb.endpoints_constraint_max_reason,
            True,
        ),
        (
            pydb.endpoints_timing_check_arcs,
            pydb.endpoints_timing_check_max_valid,
            pydb.endpoints_timing_check_max_reason,
            False,
        ),
    ):
        for arc, valid, reasons in zip(arcs, valid_rows, reason_rows, strict=True):
            assert len(arc) == 8
            source = int(pydb.clock_pins[int(arc[0])]) if clock_local else int(arc[0])
            assert 0 <= source < len(names) and 0 <= int(arc[1]) < len(names)
            assert len(valid) == len(reasons) == 2
            assert list(valid) == [int(reason) == 0 for reason in reasons]
            assert all(
                not flag or endpoint_valid[int(arc[1])][edge] for edge, flag in enumerate(valid)
            )
    return {names[pin]: valid for pin, valid in endpoint_valid.items()}

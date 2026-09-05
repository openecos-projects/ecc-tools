#!/usr/bin/env python3

"""Load LEF/DEF through OpenDB and write OpenDB's normalized DEF."""

import argparse

import odb


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input_def")
    parser.add_argument("output_def")
    parser.add_argument("lefs", nargs="+")
    arguments = parser.parse_args()

    database = odb.dbDatabase.create()
    for lef in arguments.lefs:
        if odb.read_lef(database, lef) is None:
            raise RuntimeError(f"OpenDB failed to import LEF: {lef}")

    technology = database.getTech()
    if technology is None:
        raise RuntimeError("OpenDB did not create a technology")

    chip = odb.read_def(technology, arguments.input_def)
    if chip is None or chip.getBlock() is None:
        raise RuntimeError("OpenDB did not create a design block")
    if not odb.write_def(chip.getBlock(), arguments.output_def):
        raise RuntimeError("OpenDB failed to write DEF")


if __name__ == "__main__":
    main()

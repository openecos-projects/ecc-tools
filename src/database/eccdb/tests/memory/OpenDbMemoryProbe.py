#!/usr/bin/env python3

"""Measure retained OpenDB memory after loading one LEF and DEF."""

import argparse
import ctypes
import gc
import json
import time

import odb


class Mallinfo2(ctypes.Structure):
    _fields_ = [
        ("arena", ctypes.c_size_t),
        ("ordblks", ctypes.c_size_t),
        ("smblks", ctypes.c_size_t),
        ("hblks", ctypes.c_size_t),
        ("hblkhd", ctypes.c_size_t),
        ("usmblks", ctypes.c_size_t),
        ("fsmblks", ctypes.c_size_t),
        ("uordblks", ctypes.c_size_t),
        ("fordblks", ctypes.c_size_t),
        ("keepcost", ctypes.c_size_t),
    ]


_libc = ctypes.CDLL(None)
_libc.malloc_trim.argtypes = [ctypes.c_size_t]
_libc.malloc_trim.restype = ctypes.c_int
_libc.mallinfo2.argtypes = []
_libc.mallinfo2.restype = Mallinfo2


def _status_value(field: str) -> int:
    with open("/proc/self/status", encoding="ascii") as status:
        for line in status:
            values = line.split()
            if values and values[0] == field:
                return int(values[1])
    raise RuntimeError(f"missing process status field: {field}")


def _memory() -> dict[str, int]:
    allocator = _libc.mallinfo2()
    result = {
        "allocator_in_use_kib": (allocator.uordblks + allocator.hblkhd) // 1024,
        "peak_rss_kib": _status_value("VmHWM:"),
        "rss_kib": 0,
        "pss_kib": 0,
        "private_dirty_kib": 0,
        "anonymous_kib": 0,
    }
    fields = {
        "Rss:": "rss_kib",
        "Pss:": "pss_kib",
        "Private_Dirty:": "private_dirty_kib",
        "Anonymous:": "anonymous_kib",
    }
    with open("/proc/self/smaps_rollup", encoding="ascii") as memory:
        for line in memory:
            values = line.split()
            if len(values) >= 3 and values[0] in fields and values[2] == "kB":
                result[fields[values[0]]] = int(values[1])
    if result["rss_kib"] == 0 or result["pss_kib"] == 0:
        raise RuntimeError("incomplete /proc/self/smaps_rollup data")
    return result


def _settled_memory() -> dict[str, int]:
    gc.collect()
    _libc.malloc_trim(0)
    return _memory()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("lef")
    parser.add_argument("def_file")
    arguments = parser.parse_args()

    baseline = _settled_memory()

    start = time.monotonic()
    database = odb.dbDatabase.create()
    library = odb.read_lef(database, arguments.lef)
    if library is None or database.getTech() is None:
        raise RuntimeError("OpenDB failed to import LEF")
    lef_milliseconds = round((time.monotonic() - start) * 1000)
    after_lef = _settled_memory()

    start = time.monotonic()
    chip = odb.read_def(database.getTech(), arguments.def_file)
    if chip is None or chip.getBlock() is None:
        raise RuntimeError("OpenDB failed to import DEF")
    def_milliseconds = round((time.monotonic() - start) * 1000)
    after_def = _settled_memory()

    block = chip.getBlock()
    nets = block.getNets()
    counts = {
        "layers": len(database.getTech().getLayers()),
        "sites": len(library.getSites()),
        "masters": len(library.getMasters()),
        "master_terms": sum(len(master.getMTerms()) for master in library.getMasters()),
        "rows": len(block.getRows()),
        "track_grids": len(block.getTrackGrids()),
        "instances": len(block.getInsts()),
        "instance_pins": len(block.getITerms()),
        "io_pins": len(block.getBTerms()),
        "regular_nets": sum(not net.isSpecial() for net in nets),
        "special_nets": sum(net.isSpecial() for net in nets),
        "vias": len(block.getVias()),
        "blockages": len(block.getBlockages()),
        "fills": len(block.getFills()),
    }
    print(
        "OPENDB_MEMORY_JSON="
        + json.dumps(
            {
                "baseline": baseline,
                "after_lef": after_lef,
                "after_def": after_def,
                "lef_milliseconds": lef_milliseconds,
                "def_milliseconds": def_milliseconds,
                "counts": counts,
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()

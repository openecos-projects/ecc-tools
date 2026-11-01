# SPDX-License-Identifier: MulanPSL-2.0
"""Check that OpenDB PORT sampling compares groups, rather than ordinals."""

from collections import Counter
import unittest

from compare_ifp_odb import port_group_counter


def snapshot(ports):
    terms, boxes = Counter(), Counter()
    for ordinal, rectangles in enumerate(ports):
        terms[("BTERM", "A", "N", str(ordinal), "PLACED")] += 1
        for rectangle in rectangles:
            boxes[("IOBOX", "A", str(ordinal), "PLACED", "M1", *map(str, rectangle))] += 1
    return {"BTERM": terms, "IOBOX": boxes}


class IfpOdbPortSemanticsTest(unittest.TestCase):
    def test_port_reordering_preserves_semantics(self):
        p, q = [(0, 0, 10, 10)], [(20, 0, 30, 10)]
        self.assertEqual(port_group_counter(snapshot([p, q])), port_group_counter(snapshot([q, p])))

    def test_splitting_a_strongly_connected_port_changes_semantics(self):
        p, q = [(0, 0, 10, 10)], [(20, 0, 30, 10)]
        self.assertNotEqual(port_group_counter(snapshot([p + q])), port_group_counter(snapshot([p, q])))

    def test_duplicate_port_is_not_discarded(self):
        p = [(0, 0, 10, 10)]
        self.assertNotEqual(port_group_counter(snapshot([p])), port_group_counter(snapshot([p, p])))

    def test_duplicate_shape_within_port_is_not_discarded(self):
        p = [(0, 0, 10, 10)]
        self.assertNotEqual(port_group_counter(snapshot([p])), port_group_counter(snapshot([p + p])))

    def test_one_dbu_move_changes_semantics(self):
        self.assertNotEqual(port_group_counter(snapshot([[(0, 0, 10, 10)]])),
                            port_group_counter(snapshot([[(1, 0, 11, 10)]])))

    def test_connectivity_changes_semantics(self):
        left = snapshot([[(0, 0, 10, 10)]])
        right = snapshot([[(0, 0, 10, 10)]])
        right["BTERM"] = Counter({("BTERM", "A", "OTHER", "0", "PLACED"): 1})
        self.assertNotEqual(port_group_counter(left), port_group_counter(right))

    def test_missing_port_fails_instead_of_dropping_geometry(self):
        broken = snapshot([[(0, 0, 10, 10)]])
        broken["BTERM"].clear()
        with self.assertRaises(ValueError):
            port_group_counter(broken)


if __name__ == "__main__":
    unittest.main()

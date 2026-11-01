# SPDX-License-Identifier: MulanPSL-2.0
"""Boundary regressions for the formal iFP semantic-pair runner.

These tests use disposable worker artifacts and a tiny semantic-probe stub.
They intentionally never invoke iFP or require a native build.
"""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import textwrap
import unittest
from unittest import mock


RUNNER_PATH = Path(__file__).with_name("paired_ifp.py")
SPEC = importlib.util.spec_from_file_location("paired_ifp_under_test", RUNNER_PATH)
assert SPEC is not None and SPEC.loader is not None
paired_ifp = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = paired_ifp
SPEC.loader.exec_module(paired_ifp)


class PairedIfpSemanticRunnerTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.case = self.base / "case"
        self.case.mkdir()
        self.manifest = self.case / "manifest.json"
        for name, content in {
            "tech.lef": "VERSION 5.8 ;\n",
            "input.def": "VERSION 5.8 ;\nEND DESIGN\n",
            "config.json": "{}\n",
            "checkpoint.bin": "checkpoint\n",
        }.items():
            (self.case / name).write_text(content, encoding="utf-8")
        self.manifest.write_text(json.dumps({
            "lef_files": ["tech.lef"],
            "def_file": "input.def",
            "config": "config.json",
            "binary_checkpoint": "checkpoint.bin",
        }), encoding="utf-8")
        self.root = self.base / "experiment"
        self._write_saved_experiment()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def _write_saved_experiment(self) -> None:
        self.root.mkdir()
        (self.root / "comparison").mkdir()
        for backend in paired_ifp.BACKENDS:
            directory = self.root / backend
            directory.mkdir()
            # Deliberately different JSON: it is a diagnostic, not the formal gate.
            for name in paired_ifp.MODEL_FILES:
                (directory / name).write_text(json.dumps({"backend": backend, "model": name}), encoding="utf-8")
            (directory / "import.def").write_text(f"{backend} import\n", encoding="utf-8")
            (directory / "output.def").write_text(f"{backend} output\n", encoding="utf-8")
            (directory / "result.json").write_text('{"status":"passed"}\n', encoding="utf-8")

        manifest_value = json.loads(self.manifest.read_text(encoding="utf-8"))
        inputs = paired_ifp._manifest_inputs(self.manifest, manifest_value)
        invocation = {
            "manifest": paired_ifp._file_metadata(self.manifest),
            "inputs": inputs,
        }
        (self.root / "invocation.json").write_text(json.dumps(invocation), encoding="utf-8")
        workers = {}
        for backend in paired_ifp.BACKENDS:
            directory = self.root / backend
            workers[backend] = {
                "artifacts": {
                    name: paired_ifp._file_metadata(directory / name)
                    for name in (*paired_ifp.MODEL_FILES, "import.def", "output.def", "result.json")
                }
            }
        (self.root / "comparison.json").write_text(json.dumps({"workers": workers}), encoding="utf-8")

    def _add_saved_io_order_control(self) -> None:
        """Turn the disposable base experiment into a verified controlled run."""
        raw_orders = {"idb": ["Z_OUT", "A_IN"], "eccdb": ["A_IN", "Z_OUT"]}
        controlled_order = ["A_IN", "Z_OUT"]
        for backend in paired_ifp.BACKENDS:
            directory = self.root / backend
            (directory / "controlled-input-model.json").write_text(json.dumps({
                "backend_specific_field": backend,
                "io_order": controlled_order,
            }), encoding="utf-8")
            (directory / "io-order-control.json").write_text(json.dumps({
                "mode": "stable-exact-name",
                "raw_names": raw_orders[backend],
                "controlled_names": controlled_order,
                "index_rebuilt": True,
                "pin_count": len(controlled_order),
            }), encoding="utf-8")
            (directory / "result.json").write_text(json.dumps({
                "status": "passed", "io_order_control": "stable-exact-name",
            }), encoding="utf-8")

        invocation = json.loads((self.root / "invocation.json").read_text(encoding="utf-8"))
        invocation["io_order_control"] = {
            "mode": "stable-exact-name", "experimental": True,
        }
        (self.root / "invocation.json").write_text(json.dumps(invocation), encoding="utf-8")
        workers = {}
        for backend in paired_ifp.BACKENDS:
            directory = self.root / backend
            workers[backend] = {
                "artifacts": {
                    name: paired_ifp._file_metadata(directory / name)
                    for name in paired_ifp._worker_artifact_names("stable-exact-name")
                }
            }
        (self.root / "comparison.json").write_text(json.dumps({
            "workers": workers,
            "io_order_control": {"mode": "stable-exact-name", "status": "applied"},
        }), encoding="utf-8")

    def _probe(self, body: str) -> Path:
        path = self.base / f"probe-{len(list(self.base.glob('probe-*')))}.py"
        path.write_text(
            f"#!{sys.executable}\n" + textwrap.dedent(body), encoding="utf-8"
        )
        path.chmod(0o755)
        return path

    @staticmethod
    def _workers(root: Path) -> dict[str, dict[str, object]]:
        return {backend: paired_ifp._existing_worker(root, backend) for backend in paired_ifp.BACKENDS}

    def _comparison(self, probe: Path) -> dict[str, object]:
        return paired_ifp._build_comparison(
            self.root, self.manifest, self._workers(self.root), 1e-9, probe, 10
        )

    def _compare_only(self, probe: Path) -> int:
        argv = [
            "paired_ifp.py", "--compare-only", "--manifest", str(self.manifest),
            "--output-root", str(self.root), "--semantic-probe", str(probe),
        ]
        with mock.patch.object(sys, "argv", argv):
            return paired_ifp.main()

    def test_diagnostic_json_difference_does_not_block_two_complete_canonical_matches(self) -> None:
        probe = self._probe(
            """
            import json, pathlib, sys
            pathlib.Path(sys.argv[4]).write_text(json.dumps({
                'status': 'completed', 'ifp_semantics_equal': True,
                'represented_semantics_equal': False,
                'ifp_semantics': {'left_complete': True, 'right_complete': True},
            }))
            raise SystemExit(0)
            """
        )
        result = self._comparison(probe)
        self.assertFalse(result["work_model_diagnostics_equal"])
        self.assertTrue(result["differential_passed"])
        self.assertEqual(result["def_semantics"]["import"]["status"], "equal")
        self.assertEqual(result["def_semantics"]["output"]["status"], "equal")

    def test_one_canonical_phase_different_fails_formal_comparison(self) -> None:
        probe = self._probe(
            """
            import json, pathlib, sys
            is_import = pathlib.Path(sys.argv[2]).name == 'import.def'
            pathlib.Path(sys.argv[4]).write_text(json.dumps({
                'status': 'completed', 'ifp_semantics_equal': is_import,
                'ifp_semantics': {'left_complete': True, 'right_complete': True},
            }))
            raise SystemExit(0 if is_import else 1)
            """
        )
        result = self._comparison(probe)
        self.assertTrue(result["def_semantics"]["import"]["ifp_semantics_equal"])
        self.assertFalse(result["def_semantics"]["output"]["ifp_semantics_equal"])
        self.assertFalse(result["differential_passed"])

    def test_stale_equal_report_cannot_survive_failed_probe_or_return_success(self) -> None:
        comparison_dir = self.root / "comparison"
        comparison_dir.mkdir(exist_ok=True)
        for phase in ("import", "output"):
            (comparison_dir / f"{phase}.semantic.json").write_text(json.dumps({
                "status": "completed", "ifp_semantics_equal": True,
                "ifp_semantics": {"left_complete": True, "right_complete": True},
            }), encoding="utf-8")
        probe = self._probe("raise SystemExit(2)\n")
        result = self._comparison(probe)
        self.assertIsNone(result["differential_passed"])
        self.assertIsNone(result["def_semantics"]["import"]["ifp_semantics_equal"])
        self.assertEqual(result["def_semantics"]["import"]["status"], "failed")
        self.assertEqual(self._compare_only(probe), 2)

    def test_incomplete_or_unsupported_canonical_report_is_never_a_pass(self) -> None:
        probe = self._probe(
            """
            import json, pathlib, sys
            pathlib.Path(sys.argv[4]).write_text(json.dumps({
                'status': 'completed', 'ifp_semantics_equal': True,
                'ifp_semantics': {
                    'left_complete': False, 'right_complete': True,
                    'left_unsupported': ['unhandled-record'], 'right_unsupported': [],
                },
            }))
            raise SystemExit(0)
            """
        )
        result = self._comparison(probe)
        self.assertIsNone(result["differential_passed"])
        for phase in ("import", "output"):
            self.assertEqual(result["def_semantics"][phase]["status"], "failed")
            self.assertIsNone(result["def_semantics"][phase]["ifp_semantics_equal"])

    def test_compare_only_rejects_changed_worker_artifact(self) -> None:
        (self.root / "idb" / "output.def").write_text("tampered\n", encoding="utf-8")
        probe = self._probe("raise SystemExit(2)\n")
        with self.assertRaisesRegex(SystemExit, "saved worker artifact is missing or changed"):
            self._compare_only(probe)

    def test_compare_only_rejects_changed_manifest_and_sources(self) -> None:
        self.manifest.write_text(json.dumps({
            "lef_files": ["tech.lef"], "def_file": "input.def", "config": "config.json",
            "binary_checkpoint": "checkpoint.bin", "extra": "changed",
        }), encoding="utf-8")
        probe = self._probe("raise SystemExit(2)\n")
        with self.assertRaisesRegex(SystemExit, "manifest hash changed"):
            self._compare_only(probe)

    def test_compare_only_rejects_changed_source_hash(self) -> None:
        (self.case / "input.def").write_text("changed source\n", encoding="utf-8")
        probe = self._probe("raise SystemExit(2)\n")
        with self.assertRaisesRegex(SystemExit, "source metadata changed"):
            self._compare_only(probe)

    def test_compare_only_without_formal_probe_never_returns_success(self) -> None:
        argv = [
            "paired_ifp.py", "--compare-only", "--manifest", str(self.manifest),
            "--output-root", str(self.root),
        ]
        with mock.patch.object(sys, "argv", argv):
            self.assertEqual(paired_ifp.main(), 2)

    def test_order_control_forwards_only_opt_in_tail_argument_and_preserves_raw_artifact(self) -> None:
        worker = self._probe(
            """
            import json, pathlib, sys
            out = pathlib.Path(sys.argv[3])
            mode = next((arg.split('=', 1)[1] for arg in sys.argv[4:]
                         if arg.startswith('--io-order-control=')), 'none')
            for name in ('import.def', 'output.def'):
                (out / name).write_text(name)
            (out / 'input-model.json').write_text(json.dumps({'kind': 'raw', 'mode': mode}))
            (out / 'computed-model.json').write_text('{}')
            if mode != 'none':
                (out / 'controlled-input-model.json').write_text(json.dumps({'kind': 'controlled', 'mode': mode}))
                (out / 'io-order-control.json').write_text(json.dumps({
                    'mode': mode, 'raw_names': ['Z', 'A'], 'controlled_names': ['A', 'Z'],
                    'index_rebuilt': True, 'pin_count': 2,
                }))
            (out / 'result.json').write_text(json.dumps({'status': 'passed', 'io_order_control': mode}))
            raise SystemExit(0)
            """
        )
        controlled_root = self.base / "controlled-worker"
        controlled = paired_ifp._run_worker(
            worker, self.manifest, controlled_root, "idb", 10, "stable-exact-name"
        )
        self.assertEqual(controlled["command"][-1], "--io-order-control=stable-exact-name")
        self.assertIn("controlled-input-model.json", controlled["artifacts"])
        self.assertEqual(json.loads((controlled_root / "input-model.json").read_text())["kind"], "raw")
        self.assertEqual(json.loads((controlled_root / "controlled-input-model.json").read_text())["kind"], "controlled")

        baseline = paired_ifp._run_worker(worker, self.manifest, self.base / "baseline-worker", "idb", 10)
        self.assertEqual(len(baseline["command"]), 4)
        self.assertNotIn("controlled-input-model.json", baseline["artifacts"])

    def test_controlled_order_reports_only_order_equivalence_not_full_model_equivalence(self) -> None:
        self._add_saved_io_order_control()
        workers = {
            backend: paired_ifp._existing_worker(self.root, backend, "stable-exact-name")
            for backend in paired_ifp.BACKENDS
        }
        result = paired_ifp._build_comparison(
            self.root, self.manifest, workers, 1e-9, None, 10,
            io_order_control="stable-exact-name",
        )
        control = result["io_order_control"]
        self.assertEqual(control["status"], "applied")
        self.assertFalse(control["raw_names_order_equal"])
        self.assertTrue(control["raw_name_multiset_equal"])
        self.assertTrue(control["controlled_names_equal"])
        self.assertTrue(control["index_rebuilt_all"])
        self.assertFalse(control["controlled_input_model_diagnostics_equal"])
        self.assertFalse(control["input_equivalence_established"])
        self.assertIn("temporary iFP IO-vector order", control["conclusion"])

    def test_compare_only_rejects_missing_or_changed_control_artifact(self) -> None:
        self._add_saved_io_order_control()
        probe = self._probe("raise SystemExit(2)\n")
        argv = [
            "paired_ifp.py", "--compare-only", "--io-order-control", "stable-exact-name",
            "--manifest", str(self.manifest), "--output-root", str(self.root),
            "--semantic-probe", str(probe),
        ]
        (self.root / "idb" / "io-order-control.json").write_text("{}", encoding="utf-8")
        with mock.patch.object(sys, "argv", argv):
            with self.assertRaisesRegex(SystemExit, "saved worker artifact is missing or changed"):
                paired_ifp.main()

        self._add_saved_io_order_control()
        (self.root / "eccdb" / "controlled-input-model.json").unlink()
        with mock.patch.object(sys, "argv", argv):
            with self.assertRaisesRegex(SystemExit, "saved worker artifact is missing or changed"):
                paired_ifp.main()


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python
"""Tests for the ecc_py binding census lexer, manifest join, and --check gate."""

import json
import shutil
from pathlib import Path

import pytest
from jsonschema import ValidationError

import dead_bindings
import lexer
import manifest as census_manifest

CENSUS_DIR = Path(__file__).resolve().parent
REPO_ROOT = CENSUS_DIR.parents[1]

CALLED = {"register_eval", "register_imp", "register_ipdn", "register_irt", "register_good"}


def discover(text, module="py_test", called=CALLED):
    return lexer.discover_bindings_in_text(text, module=module, file=f"{module}/py_register_test.h", called_registers=called)


# ---------------------------------------------------------------------------
# Lexer fixtures
# ---------------------------------------------------------------------------

MULTILINE = """\
void register_imp(pybind11::module& m)
{
  m.def(
      "pydb",
      [](idm::DataManager* db, int num_routing_grids_x, int num_routing_grids_y, bool with_routability, bool with_sta) {
        return PyPlaceDB(db, num_routing_grids_x, num_routing_grids_y, with_routability, with_sta);
      },
      "Convert PlaceDB to PyPlaceDB");
}
"""

COMMENTED = """\
void register_imp(pybind11::module& m)
{
  m.def("active_one", active_one);
  // m.def("runMP", runMP, py::arg("config"), py::arg("output_tcl") = "");
  /*
   * m.def("runRef", runRef, py::arg("output_tcl") = "");
   */
}
"""

COMMENT_MARKERS_IN_STRINGS = """\
void register_eval(py::module& m)
{
  m.def("fetch//doc", fetch, py::arg("url") = "http://example.com/*x*/y", py::arg("note") = "a // b");
}
"""

LAMBDA_WITH_BODY = """\
void register_eval(py::module& m)
{
  m.def("cell_density", [](int bin_cnt_x = 256, int bin_cnt_y = 256, const std::string& save_path = "") -> py::tuple {
      auto [max_density, avg_density] = cell_density(bin_cnt_x, bin_cnt_y, save_path);
      return py::make_tuple(max_density, avg_density);
  }, py::arg("bin_cnt_x") = 256, py::arg("bin_cnt_y") = 256, py::arg("save_path") = "");
}
"""

MAP_DEFAULT = """\
void register_irt(py::module& m)
{
  m.def("init_rt", initRT, py::arg("config") = "", py::arg("config_dict") = std::map<std::string, std::string>{});
}
"""

UPPERCASE_NAMES = """\
void register_ipdn(py::module& m)
{
  m.def("connectMacroPdn", pdnConnectMacro, py::arg("pin_layer"), py::arg("orient"));
  m.def("get_dmInst", &getDMInst, "A function which returns a DataManager instance", pybind11::return_value_policy::reference);
}
"""

CLASS_CHAIN = """\
void register_eval(py::module& m)
{
  py::class_<ieval::TotalWLSummary>(m, "TotalWLSummary")
      .def_readwrite("HPWL", &ieval::TotalWLSummary::HPWL)
      .def_readwrite("FLUTE", &ieval::TotalWLSummary::FLUTE)
      .def("summary", &ieval::TotalWLSummary::summary);
  m.def("total_wirelength_dict", []() -> py::dict {
      py::dict result;
      result["1"] = 0;
      return result;
  });
}
"""


def test_multiline_m_def():
    (binding,) = discover(MULTILINE, module="py_imp")
    assert binding.py_name == "pydb"
    assert binding.cpp_target == "<lambda>"
    assert binding.line == 3
    assert binding.status_in_source == "active"
    assert binding.params == []


def test_line_and_block_commented_m_def():
    bindings = {b.py_name: b for b in discover(COMMENTED, module="py_imp")}
    assert set(bindings) == {"active_one", "runMP", "runRef"}
    assert bindings["active_one"].status_in_source == "active"
    assert bindings["runMP"].status_in_source == "disabled"
    assert bindings["runRef"].status_in_source == "disabled"
    assert [(p.name, p.default) for p in bindings["runMP"].params] == [("config", None), ("output_tcl", '""')]


def test_comment_markers_inside_string_literals():
    (binding,) = discover(COMMENT_MARKERS_IN_STRINGS)
    assert binding.py_name == "fetch//doc"
    assert binding.status_in_source == "active"
    assert [(p.name, p.default) for p in binding.params] == [
        ("url", '"http://example.com/*x*/y"'),
        ("note", '"a // b"'),
    ]


def test_lambda_target_with_commas_and_braces_in_body():
    (binding,) = discover(LAMBDA_WITH_BODY)
    assert binding.cpp_target == "<lambda>"
    assert [(p.name, p.default) for p in binding.params] == [
        ("bin_cnt_x", "256"),
        ("bin_cnt_y", "256"),
        ("save_path", '""'),
    ]


def test_py_arg_extraction_at_depth_one_with_template_commas():
    (binding,) = discover(MAP_DEFAULT)
    assert [(p.name, p.default) for p in binding.params] == [
        ("config", '""'),
        ("config_dict", "std::map<std::string, std::string>{}"),
    ]


def test_uppercase_binding_names_and_reference_target():
    bindings = {b.py_name: b for b in discover(UPPERCASE_NAMES)}
    assert bindings["connectMacroPdn"].cpp_target == "pdnConnectMacro"
    assert bindings["get_dmInst"].cpp_target == "getDMInst"


def test_class_chain_def_and_def_readwrite_not_collected():
    (binding,) = discover(CLASS_CHAIN)
    assert binding.py_name == "total_wirelength_dict"
    assert binding.cpp_target == "<lambda>"


def _write_fake_repo(root: Path) -> None:
    py_dir = root / "src" / "interface" / "python"
    (py_dir / "py_good").mkdir(parents=True)
    (py_dir / "py_gone").mkdir(parents=True)
    (py_dir / "python_moodule.cc").write_text(
        "PYBIND11_MODULE(ecc_py, m)\n"
        "{\n"
        "  register_good(m);\n"
        "  // register_gone(m);  // disabled: module removed\n"
        "}\n"
    )
    (py_dir / "py_good" / "py_register_good.h").write_text(
        'void register_good(py::module& m)\n{\n  m.def("good_one", good_one);\n}\n'
    )
    (py_dir / "py_gone" / "py_register_gone.h").write_text(
        'void register_gone(py::module& m)\n{\n  m.def("gone_one", gone_one, py::arg("path") = "");\n}\n'
    )


def test_module_level_disable_via_uncalled_register_function(tmp_path):
    _write_fake_repo(tmp_path)
    discovery = lexer.discover(tmp_path)
    bindings = {b.py_name: b for b in discovery["bindings"]}
    assert bindings["good_one"].status_in_source == "active"
    assert bindings["gone_one"].status_in_source == "disabled"
    assert bindings["gone_one"].register_function == "register_gone"
    assert bindings["good_one"].register_function == "register_good"


# ---------------------------------------------------------------------------
# Schema + --check gate
# ---------------------------------------------------------------------------


def _spec_param(**overrides):
    param = {
        "param": "save_path",
        "old_type": "const std::string&",
        "old_default": '""',
        "new_type": "std::optional<std::filesystem::path>",
        "new_default": "py::none()",
        "scalar_or_list": "scalar",
        "required_or_optional": "optional",
        "classification": "path",
        "classification_rationale": "output file path with empty-string unset sentinel",
    }
    param.update(overrides)
    return param


def test_ambiguous_without_rationale_fails_schema_validation():
    spec = {
        "version": 1,
        "bindings": [
            {
                "module": "py_eval",
                "py_name": "cell_density",
                "params": [_spec_param(classification="ambiguous", classification_rationale="")],
            }
        ],
    }
    with pytest.raises(ValidationError):
        census_manifest.validate_spec(spec, census_manifest.load_schema(CENSUS_DIR / "binding_spec.schema.json"))


def _write_fake_census(census_dir: Path, spec: dict, manifest: dict) -> None:
    census_dir.mkdir(parents=True, exist_ok=True)
    for name in ("binding_spec.schema.json", "manifest.schema.json"):
        shutil.copy(CENSUS_DIR / name, census_dir / name)
    (census_dir / "binding_spec.json").write_text(json.dumps(spec, indent=2, sort_keys=True) + "\n")
    (census_dir / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")


def test_check_fails_when_active_string_param_missing_from_spec(tmp_path):
    repo = tmp_path / "repo"
    _write_fake_repo(repo)
    # An active binding whose py::arg carries a string-literal default.
    register = repo / "src" / "interface" / "python" / "py_good" / "py_register_good.h"
    register.write_text(
        'void register_good(py::module& m)\n{\n  m.def("good_one", good_one, py::arg("save_path") = "");\n}\n'
    )
    spec = {"version": 1, "bindings": []}
    manifest = {"version": 1, "entries": []}
    census_dir = tmp_path / "census"
    _write_fake_census(census_dir, spec, manifest)
    failures = census_manifest.check(repo, census_dir)
    assert failures, "expected --check to fail on a missing active string param"
    assert any("good_one" in failure and "save_path" in failure for failure in failures)


def test_check_fails_on_stale_manifest(tmp_path):
    repo = tmp_path / "repo"
    _write_fake_repo(repo)
    spec = {"version": 1, "bindings": []}
    manifest = {"version": 1, "entries": [{"stale": True}]}
    census_dir = tmp_path / "census"
    _write_fake_census(census_dir, spec, manifest)
    failures = census_manifest.check(repo, census_dir)
    assert any("byte-stable" in failure or "out of date" in failure for failure in failures)


# ---------------------------------------------------------------------------
# Real-repo integration
# ---------------------------------------------------------------------------


def test_real_repo_discovery_statuses():
    discovery = lexer.discover(REPO_ROOT)
    bindings = {b.py_name: b for b in discovery["bindings"]}
    assert bindings["flow_init"].status_in_source == "active"
    assert bindings["pydb"].status_in_source == "active"
    assert bindings["runMP"].status_in_source == "disabled"
    assert bindings["runRef"].status_in_source == "disabled"
    # py_vec's register function is never called -> whole module disabled.
    for name in ("layout_patchs", "layout_graph", "generate_vectors", "read_vectors_nets", "get_timing_wire_graph"):
        assert bindings[name].status_in_source == "disabled", name
        assert bindings[name].register_function == "register_vectorization"


def test_real_repo_check_is_green():
    assert census_manifest.check(REPO_ROOT, CENSUS_DIR) == []


def test_real_repo_generation_is_byte_stable():
    first = census_manifest.generate_manifest_bytes(REPO_ROOT, CENSUS_DIR)
    second = census_manifest.generate_manifest_bytes(REPO_ROOT, CENSUS_DIR)
    assert first == second
    assert first == (CENSUS_DIR / "manifest.json").read_bytes()


# ---------------------------------------------------------------------------
# Dead-binding audit (synthetic wrapper)
# ---------------------------------------------------------------------------

SYNTHETIC_WRAPPER = '''\
class ECCToolsModule:
    def live(self):
        return self.ecc.flow_init("x")

    def dead_mp(self, config):
        return self.ecc.runMP(config)

    def dead_absent(self):
        self.ecc.run_pnp("c")
        self.ecc.run_placer("c")

    def mixed(self):
        self.ecc.runMP("c")
        self.ecc.flow_init("x")

    def no_calls(self):
        return None
'''


def test_dead_binding_audit_classifies_calls(tmp_path):
    register = """\
void register_imp(pybind11::module& m)
{
  m.def("flow_init", flow_init, py::arg("flow_config"));
  // m.def("runMP", runMP, py::arg("config"), py::arg("output_tcl") = "");
}
"""
    discovery_bindings = lexer.discover_bindings_in_text(
        register, module="py_imp", file="py_imp/py_register_imp.cpp", called_registers={"register_imp"}
    )
    wrapper = tmp_path / "module.py"
    wrapper.write_text(SYNTHETIC_WRAPPER)
    audit = dead_bindings.audit_wrapper(wrapper, {"bindings": discovery_bindings})
    statuses = {call["binding"]: call["status"] for row in audit["rows"] for call in row["calls"]}
    assert statuses["flow_init"] == "active"
    assert statuses["runMP"].startswith("disabled")
    assert statuses["run_pnp"] == "absent"
    assert statuses["run_placer"] == "absent"
    assert audit["dead_method_candidates"] == ["dead_absent", "dead_mp"]
    assert audit["methods_without_calls"] == ["no_calls"]

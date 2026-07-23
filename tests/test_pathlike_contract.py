"""Behavior contract suite for the os.PathLike bindings in ecc_py.

Runs against an INSTALLED ecc-tools-bin wheel, not the source tree: the
source tree ships an ``ecc_tools_bin`` package without the compiled
extension, so the import guard below fails loudly if the suite is
accidentally run with the repo root on ``sys.path``.

Calls that mutate global C++ state (the ``*_init`` family, ``save_data``,
``idb_get``) are executed in a fresh subprocess each so one test cannot
poison the next through module-level state.
"""

import subprocess
import sys
import textwrap

import pytest

from ecc_tools_bin import ecc_py


def test_installed_wheel_is_imported():
    # The source tree's ecc_tools_bin/ has no compiled extension; the
    # installed wheel resolves ecc_py to a .so inside site-packages.
    path = getattr(ecc_py, "__file__", "") or ""
    print(f"ecc_py imported from: {path}")
    assert path.endswith(".so"), f"ecc_py is not the compiled extension: {path!r}"
    assert "site-packages" in path, f"ecc_py not imported from an installed wheel: {path!r}"


def _run(body, cwd):
    """Run ``body`` in a fresh interpreter; assertions inside it gate the exit code."""
    script = "from ecc_tools_bin import ecc_py\nfrom pathlib import Path\n\n" + textwrap.dedent(body)
    proc = subprocess.run(
        [sys.executable, "-c", script],
        cwd=cwd,
        capture_output=True,
        text=True,
    )
    assert proc.returncode == 0, (
        f"subprocess failed with exit code {proc.returncode}\n"
        f"--- stdout ---\n{proc.stdout}\n--- stderr ---\n{proc.stderr}"
    )


# ---------------------------------------------------------------------------
# 1. input-file path: def_init
# ---------------------------------------------------------------------------


def test_def_init_accepts_str_path_and_custom_pathlike(tmp_path):
    _run(
        """
        class CustomPath:
            def __init__(self, path):
                self._path = path
            def __fspath__(self):
                return self._path

        target = '/nonexistent/input.def'
        expected = ecc_py.def_init(target)
        assert expected is False
        assert ecc_py.def_init(Path(target)) == expected
        assert ecc_py.def_init(CustomPath(target)) == expected
        """,
        cwd=tmp_path,
    )


def test_def_init_rejects_non_pathlike(tmp_path):
    _run(
        """
        for bad in (None, 5):
            try:
                ecc_py.def_init(bad)
            except TypeError:
                pass
            else:
                raise AssertionError(f'def_init({bad!r}) did not raise TypeError')
        """,
        cwd=tmp_path,
    )


# ---------------------------------------------------------------------------
# 2. output-file path: save_data
# ---------------------------------------------------------------------------


def test_save_data_path_matches_str(tmp_path):
    _run(
        """
        expected = ecc_py.save_data('/nonexistent/out.db')
        assert expected is False
        assert ecc_py.save_data(Path('/nonexistent/out.db')) == expected
        """,
        cwd=tmp_path,
    )


def test_save_data_rejects_none(tmp_path):
    _run(
        """
        try:
            ecc_py.save_data(None)
        except TypeError:
            pass
        else:
            raise AssertionError('save_data(None) did not raise TypeError')
        """,
        cwd=tmp_path,
    )


# ---------------------------------------------------------------------------
# 3. optional config path: init_rt
# ---------------------------------------------------------------------------


def test_init_rt_none_and_empty_match_omitted(tmp_path):
    _run(
        """
        omitted = ecc_py.init_rt(config_dict={})
        assert ecc_py.init_rt(config=None, config_dict={}) == omitted
        assert ecc_py.init_rt(config='', config_dict={}) == omitted
        """,
        cwd=tmp_path,
    )


def test_init_rt_path_matches_str(tmp_path):
    _run(
        """
        expected = ecc_py.init_rt(config='/nonexistent/rt.toml', config_dict={})
        assert ecc_py.init_rt(config=Path('/nonexistent/rt.toml'), config_dict={}) == expected
        """,
        cwd=tmp_path,
    )


def test_init_rt_rejects_non_pathlike_config(tmp_path):
    _run(
        """
        try:
            ecc_py.init_rt(config=5, config_dict={})
        except TypeError:
            pass
        else:
            raise AssertionError('init_rt(config=5) did not raise TypeError')
        """,
        cwd=tmp_path,
    )


def test_init_rt_config_dict_still_requires_str_values(tmp_path):
    _run(
        """
        try:
            ecc_py.init_rt(config='', config_dict={'-temp_directory_path': Path('/tmp/x')})
        except TypeError:
            pass
        else:
            raise AssertionError('init_rt accepted a Path value in config_dict')
        """,
        cwd=tmp_path,
    )


# ---------------------------------------------------------------------------
# 4. optional report/save path: idb_get
# ---------------------------------------------------------------------------


def test_idb_get_none_and_empty_match_omitted(tmp_path):
    _run(
        """
        omitted = ecc_py.idb_get()
        assert ecc_py.idb_get(file_name=None) == omitted
        assert ecc_py.idb_get(file_name='') == omitted
        """,
        cwd=tmp_path,
    )


def test_idb_get_path_matches_str(tmp_path):
    _run(
        """
        expected = ecc_py.idb_get(file_name='/nonexistent/out.db')
        assert ecc_py.idb_get(file_name=Path('/nonexistent/out.db')) == expected
        """,
        cwd=tmp_path,
    )


def test_idb_get_rejects_non_pathlike_file_name(tmp_path):
    _run(
        """
        try:
            ecc_py.idb_get(file_name=5)
        except TypeError:
            pass
        else:
            raise AssertionError('idb_get(file_name=5) did not raise TypeError')
        """,
        cwd=tmp_path,
    )


# ---------------------------------------------------------------------------
# 5. list path: lef_init
# ---------------------------------------------------------------------------


def test_lef_init_accepts_str_path_mixed_and_custom(tmp_path):
    _run(
        """
        class CustomPath:
            def __init__(self, path):
                self._path = path
            def __fspath__(self):
                return self._path

        a = '/nonexistent/a.lef'
        b = '/nonexistent/b.lef'
        expected = ecc_py.lef_init([a, b])
        assert expected is True
        assert ecc_py.lef_init([Path(a), Path(b)]) == expected
        assert ecc_py.lef_init([a, Path(b), CustomPath(a)]) == expected
        """,
        cwd=tmp_path,
    )


def test_lef_init_rejects_non_pathlike_elements(tmp_path):
    _run(
        """
        for bad in ([None], [3]):
            try:
                ecc_py.lef_init(bad)
            except TypeError:
                pass
            else:
                raise AssertionError(f'lef_init({bad!r}) did not raise TypeError')
        """,
        cwd=tmp_path,
    )


# ---------------------------------------------------------------------------
# 6. sdc equivalence: sdc_init
# ---------------------------------------------------------------------------


def test_sdc_init_none_and_empty_match_omitted(tmp_path):
    _run(
        """
        omitted = ecc_py.sdc_init()
        assert omitted is True
        assert ecc_py.sdc_init(None) == omitted
        assert ecc_py.sdc_init('') == omitted
        """,
        cwd=tmp_path,
    )


def test_sdc_init_path_matches_str(tmp_path):
    _run(
        """
        expected = ecc_py.sdc_init('/nonexistent.sdc')
        assert ecc_py.sdc_init(Path('/nonexistent.sdc')) == expected
        """,
        cwd=tmp_path,
    )


def test_sdc_init_rejects_non_pathlike(tmp_path):
    _run(
        """
        try:
            ecc_py.sdc_init(5)
        except TypeError:
            pass
        else:
            raise AssertionError('sdc_init(5) did not raise TypeError')
        """,
        cwd=tmp_path,
    )


# ---------------------------------------------------------------------------
# 7. doc smoke: rendered signatures advertise os.PathLike
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    "name",
    [
        "sdc_init",
        "def_init",
        "lef_init",
        "init_rt",
        "idb_get",
        "cell_density",
        "report_drc",
        "init_rcx",
        "db_init",
    ],
)
def test_doc_mentions_pathlike(name):
    doc = getattr(ecc_py, name).__doc__ or ""
    assert "os.PathLike" in doc, f"{name} doc does not mention os.PathLike: {doc!r}"


@pytest.mark.parametrize(
    "name,param",
    [
        ("sdc_init", "sdc_path"),
        ("init_rt", "config"),
        ("idb_get", "file_name"),
        ("cell_density", "save_path"),
        ("db_init", "config_path"),
        ("db_init", "def_path"),
        ("db_init", "sdc_path"),
    ],
)
def test_doc_optional_pathlike_defaults_to_none(name, param):
    doc = getattr(ecc_py, name).__doc__ or ""
    assert f"{param}: Optional[os.PathLike] = None" in doc, (
        f"{name} doc does not render '{param}: Optional[os.PathLike] = None': {doc!r}"
    )


def test_doc_lef_init_renders_list_of_pathlike():
    doc = ecc_py.lef_init.__doc__ or ""
    assert "lef_paths: List[os.PathLike]" in doc, f"lef_init doc: {doc!r}"


def test_doc_verilog_init_top_module_still_str():
    doc = ecc_py.verilog_init.__doc__ or ""
    assert "top_module: str" in doc, f"verilog_init doc: {doc!r}"


def test_doc_init_rcx_pdk_still_optional_str():
    doc = ecc_py.init_rcx.__doc__ or ""
    assert "pdk: Optional[str] = None" in doc, f"init_rcx doc: {doc!r}"


def test_doc_init_rt_config_dict_still_dict():
    doc = ecc_py.init_rt.__doc__ or ""
    assert "config_dict: Dict" in doc, f"init_rt doc: {doc!r}"
    assert "config_dict: os.PathLike" not in doc, f"init_rt doc: {doc!r}"

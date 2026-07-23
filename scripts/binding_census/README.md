# Binding census

Machine-verified census of the `ecc_py` pybind11 bindings whose parameters
carry strings (`std::string` / `std::vector<std::string>` and similar string
containers). It is the source of truth for widening path-carrying parameters
to `std::filesystem::path` / `std::optional<std::filesystem::path>`.

## Layout

- `binding_census.py` — thin CLI entry point (argument parsing and
  orchestration only). The implementation is split into sibling modules:
  `lexer.py` (lexical discovery scanner), `manifest.py` (schema validation,
  spec/manifest join, `--check` gate), and `dead_bindings.py` (the
  `--ecc-root` cross-repo audit). Together they need only the stdlib plus
  `jsonschema`.
- `binding_spec.json` — curated, human-reviewed semantics per in-scope
  parameter (types, defaults, shape, path/non_path/ambiguous classification
  with rationale). Validated against `binding_spec.schema.json`.
- `manifest.json` — generated join of discovery + spec, one entry per
  (binding, parameter). Validated against `manifest.schema.json`. Committed;
  never edit by hand.
- `dead_bindings.md` — generated audit of the ecc wrapper's calls against
  this census (needs the outer repo, see below).
- `baseline_diff.md` — count comparison of the manifest against the reviewed
  classification baseline, with a written rationale for every deviation.

## Why discovery + curated spec

Discovery is a small lexer (a state machine over code / line comment / block
comment / string / char literals with paren-brace-bracket depth tracking — no
cross-line regex) that finds every module-level `m.def(` in
`src/interface/python/py_*/py_register_*.h` and `.../py_register_*.cpp`,
including multiline and commented-out statements, extracts the
`py::arg("name") = default` entries at paren depth 1, and resolves whether a
binding is `active` or `disabled` (commented statement, or its enclosing
`register_*` function is never called from `python_moodule.cc`).

Semantics — which string parameter is actually a filesystem path, and what
the converted type and default should be — cannot be derived from arbitrary
C++ declarations without writing half a C++ parser. They live in the curated
spec instead, one row per parameter, each with a written rationale. The
manifest is the deterministic join of the two.

## Regenerating

From the repository root:

```sh
uv run --no-project --with jsonschema python scripts/binding_census/binding_census.py
```

Running it twice with unchanged inputs produces identical bytes
(`--no-project` matters: plain `uv run` in the repo root would trigger a
project sync and a full C++ build).

To also regenerate the dead-binding audit you need a checkout of the outer
ecc repo (read-only):

```sh
uv run --no-project --with jsonschema python scripts/binding_census/binding_census.py \
  --ecc-root /path/to/ecc
```

## CI gate

```sh
uv run --no-project --with jsonschema python scripts/binding_census/binding_census.py --check
```

Exits nonzero with one message per violation when any of these fail:

1. regeneration is not byte-stable against the committed `manifest.json`;
2. schema validation of `manifest.json` or `binding_spec.json`;
3. coverage: every discovered active binding with a `py::arg` string-literal
   default has spec entries for at least those parameters, every parameter
   named in the classification baseline is spec'd as `path`, and every spec
   entry names a discovered binding/parameter;
4. every `ambiguous` classification carries a non-empty rationale;
5. every `path`-classified parameter of an active binding carries the
   converted `new_type` (`std::filesystem::path` when required,
   `std::optional<std::filesystem::path>` when optional,
   `std::vector<std::filesystem::path>` for lists).

## Known limitation

The lexer discovers parameters from `py::arg(...)` entries. String parameters
of `py::arg`-free bindings (e.g. `idb_init`'s `config_path`, `tech_lef_init`'s
`techlef_path`) and string parameters without literal defaults are
curated-only: they exist in the spec (named after the C++ declaration) but
discovery cannot cross-check them. `binding_spec.json` covers them; the
coverage gate cross-checks everything discovery can see.

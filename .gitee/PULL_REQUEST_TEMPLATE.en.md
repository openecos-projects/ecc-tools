## What Changed

-

## Scope

- [ ] C++ core/database/EDA integration
- [ ] Python API or native bindings
- [ ] Geometry, placement, timing, DRC, or evaluation behavior
- [ ] Build/package - CMake, wheel, manylinux, or dependencies
- [ ] CI/release
- [ ] Tests/docs only

## Runtime And Packaging Impact

- [ ] No runtime or packaging impact
- [ ] C++ runtime behavior or native ABI changed
- [ ] Python API or extension module behavior changed
- [ ] Wheel contents or supported platform changed
- [ ] CMake, compiler, dependency, or toolchain requirement changed

Notes:

-

## Validation

- [ ] C/C++ formatting: `uvx prek run --config .pre-commit-config.yaml --files <changed files>`
- [ ] Native build: `bash build.sh` or equivalent CMake command
- [ ] Wheel build: `uv build --wheel --no-build-isolation --verbose`
- [ ] Python API integration tests
- [ ] Focused tests or smoke test
- [ ] Other:

Skipped checks and reason:

-

## Checklist

- [ ] The change is scoped to ecc-tools.
- [ ] Public C++/Python or packaging impact is documented.
- [ ] Dependencies and `uv.lock` are updated when needed.
- [ ] No local caches, virtual environments, or generated outputs are included.
- [ ] Skipped validation and remaining risk are explained.

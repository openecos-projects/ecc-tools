## What Changed

-

## Scope

Select the areas touched by this PR:

- [ ] C++ core/database/EDA integration
- [ ] Python API or native bindings
- [ ] Geometry, placement, timing, DRC, or evaluation behavior
- [ ] Build/package - CMake, wheel, manylinux, or dependencies
- [ ] CI/release - GitHub Actions, version checks, or release automation
- [ ] Tests/docs only

## Runtime And Packaging Impact

- [ ] No runtime or packaging impact
- [ ] C++ runtime behavior or native ABI changed
- [ ] Python API or extension module behavior changed
- [ ] Wheel contents or supported platform changed
- [ ] CMake, compiler, dependency, or toolchain requirement changed
- [ ] Release artifact or version metadata changed

Notes:

-

## Validation

List the commands you ran. Mark checks that are not applicable as N/A.

- [ ] C++ formatting: `uvx prek run --config .pre-commit-config.yaml --files <changed C/C++ files>`
- [ ] Native build: `bash build.sh` or an equivalent CMake command
- [ ] Wheel build: `uv build --wheel --no-build-isolation --verbose`
- [ ] Python API integration: `python -m pytest data operations -v --maxfail=1`
- [ ] Focused unit or scenario tests:
- [ ] Manual native/Python smoke test:
- [ ] Other:

Skipped checks and reason:

-

## Checklist

- [ ] I kept the change scoped to ecc-tools.
- [ ] I updated documentation or user-facing API text where behavior changed.
- [ ] I updated `uv.lock` or version metadata when dependencies changed.
- [ ] I documented any CMake, compiler, ABI, or platform compatibility impact.
- [ ] I did not include local caches, virtual environments, or generated build outputs.
- [ ] I explained skipped validation and remaining risk.

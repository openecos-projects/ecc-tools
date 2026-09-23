# Contributing to ecc-tools

ecc-tools is a C++/CMake toolchain with Python bindings and manylinux wheel
packaging. Keep changes focused on the component being changed and make the
native, Python, and packaging contracts explicit in commits and pull requests.

## Branches

Create a topic branch from the latest `main`, using a name such as
`<username>/<short-topic>`. Pull requests target `main` and should be rebased
onto the latest `main` before review when the branch has diverged substantially.

## Commit Messages

Use [Conventional Commits](https://www.conventionalcommits.org/) with a concise,
imperative subject:

```text
<type>(<scope>): <subject>
```

The scope is optional. The allowed types are:

- `feat` - add user-visible or API behavior.
- `fix` - correct a bug or regression.
- `docs` - change documentation only.
- `style` - change formatting without changing behavior.
- `refactor` - restructure code without changing behavior.
- `perf` - improve performance without changing behavior.
- `test` - add or update tests.
- `build` - change CMake, wheel, compiler, or dependency setup.
- `ci` - change GitHub Actions or other automation.
- `chore` - maintenance that does not fit another type.
- `revert` - revert an earlier change.

Use a component scope when it improves discoverability. Typical scopes include
`database`, `geometry`, `placement`, `timing`, `drc`, `evaluation`, `python`,
`build`, `ci`, `docs`, and `deps`.

Examples:

```text
feat(geometry): preserve layer metadata in snapshots
fix(python): reject invalid placement map dimensions
build(cmake): expose the native extension target
test(database): cover Python row iteration
docs: document wheel build prerequisites
```

Keep the subject short, start it with a verb, and do not end it with a full
stop. The local hooks allow `Merge`, `fixup!`, `squash!`, and `amend!` commits
needed during branch maintenance; normal submitted commits and the PR title
must still follow the Conventional Commit format.

Install the hooks from the repository root:

```bash
uvx prek install --config .pre-commit-config.yaml --hook-type pre-commit --hook-type commit-msg --overwrite
```

The formatting hook calls `clang-format` from the system. Install a
distribution package that provides `clang-format` before enabling the hooks;
the repository's `.clang-format` file is the source of formatting options.

The `pre-commit` stage checks changed C/C++ files with the repository's
`.clang-format`. The `commit-msg` stage checks the Conventional Commit format
and subject punctuation. CI repeats the commit-message checks for every commit
in a pull request and for the PR title.

## Pull Requests

Use `.github/pull_request_template.md` and include:

- a short summary of the behavior or interface change;
- the affected native, Python, build, or release areas;
- C++ runtime, Python API, ABI, platform, compiler, and wheel impact;
- exact validation commands and their results;
- skipped checks with a reason and remaining risk;
- documentation, lockfile, version, and generated-artifact updates when relevant.

Keep build directories, wheels, test artifacts, caches, virtual environments,
and other generated files out of commits. If a change updates a public Python
or C++ contract, include a focused test or explain why a test is not practical.

## Validation

Run the narrowest relevant checks locally, then run the checks that match the
changed surface:

```bash
# C/C++ formatting for changed files
uvx prek run --config .pre-commit-config.yaml --files path/to/changed/file.cpp path/to/changed/header.h

# Native build (requires the documented compiler and system dependencies)
bash build.sh

# Python wheel build
uv build --wheel --no-build-isolation --verbose

# Python API integration tests after installing the built wheel
python -m pytest data operations -v --maxfail=1
```

The required CI checks are defined in `.github/workflows/ci.yml`: Build Wheel
and Test Python API. Changes to build, packaging, native ABI, or release inputs
should state which additional checks were run or why they were skipped.

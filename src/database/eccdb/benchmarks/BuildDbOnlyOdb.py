#!/usr/bin/env python3
# ***************************************************************************************
# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory
# Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
# Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
#
# iEDA is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
# http://license.coscl.org.cn/MulanPSL2
#
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
#
# See the Mulan PSL v2 for more details.
# ***************************************************************************************

"""Build a native OpenDB benchmark through a temporary Bazel overlay.

OpenROAD remains the Bazel workspace and supplies the ODB target.  The only
root package visible before it is a temporary directory containing the selected
benchmark sources and a minimal BUILD.bazel file.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


BENCHMARK_DIRECTORY = Path(__file__).resolve().parent
BENCHMARKS = {
    "db-only": ("eccdb_db_only_odb", ("DbOnlyOdbBenchmark.cpp", "DbOnlyBenchCommon.h")),
    "drt-service": ("eccdb_drt_service_odb", ("DrtServiceOdbBenchmark.cpp", "DbOnlyBenchCommon.h")),
}
OVERLAY_BUILD = """\
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")

cc_binary(
    name = "{target}",
    srcs = {sources},
    deps = [
        "//src/odb/src/db:db",
        "@boost.json",
    ],
)
"""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--benchmark", choices=tuple(BENCHMARKS), default="db-only")
    parser.add_argument(
        "--openroad-root",
        type=Path,
        required=True,
        help="read-only OpenROAD Bazel workspace root",
    )
    parser.add_argument(
        "--bazel",
        default="bazelisk",
        help="Bazel or Bazelisk executable (default: bazelisk)",
    )
    parser.add_argument("--output", type=Path, required=True, help="destination for the built executable")
    parser.add_argument(
        "--jobs",
        type=int,
        default=max(1, os.cpu_count() or 1),
        help="maximum concurrent Bazel actions (default: CPU count)",
    )
    cache_group = parser.add_mutually_exclusive_group()
    cache_group.add_argument(
        "--output-user-root",
        type=Path,
        help="Bazel output user root; defaults to a stable directory under the system temporary directory",
    )
    cache_group.add_argument(
        "--output-base",
        type=Path,
        help="exact Bazel output base to reuse instead of creating one under --output-user-root",
    )
    parser.add_argument(
        "--offline",
        action="store_true",
        help="pass --nofetch; requires an existing local Bazel repository cache",
    )
    parser.add_argument(
        "--install-base",
        type=Path,
        help="Bazel installation cache; defaults to a stable directory under the system temporary directory",
    )
    return parser.parse_args()


def fail(message: str) -> None:
    raise RuntimeError(message)


def resolve_bazel(value: str) -> str:
    candidate = Path(value)
    if candidate.parent != Path("."):
        if not candidate.is_file() or not os.access(candidate, os.X_OK):
            fail(f"--bazel is not an executable file: {candidate}")
        return str(candidate.resolve())
    resolved = shutil.which(value)
    if resolved is None:
        fail(f"cannot find --bazel executable on PATH: {value}")
    return resolved


def validate_inputs(args: argparse.Namespace) -> tuple[Path, tuple[Path, ...], str, str]:
    if args.jobs <= 0:
        fail("--jobs must be positive")

    openroad_root = args.openroad_root.resolve()
    if not openroad_root.is_dir() or not (openroad_root / "MODULE.bazel").is_file():
        fail(f"--openroad-root is not an OpenROAD Bazel workspace: {openroad_root}")

    target, source_names = BENCHMARKS[args.benchmark]
    sources = tuple(BENCHMARK_DIRECTORY / source for source in source_names)
    for source in sources:
        if not source.is_file():
            fail(f"missing benchmark source: {source}")
    return openroad_root, sources, resolve_bazel(args.bazel), target


def user_root_for(openroad_root: Path) -> Path:
    # Keep dependency extraction outside both source trees.  The stable path
    # makes a later --offline invocation reuse the repository/action cache.
    digest = hashlib.sha256(str(openroad_root).encode("utf-8")).hexdigest()[:16]
    return Path(tempfile.gettempdir()) / f"eccdb-db-only-odb-bazel-{digest}"


def run(command: list[str], *, cwd: Path, environment: dict[str, str], capture_output: bool = False) -> str:
    try:
        completed = subprocess.run(
            command,
            check=True,
            cwd=cwd,
            env=environment,
            text=True,
            stdout=subprocess.PIPE if capture_output else None,
        )
    except FileNotFoundError as error:
        fail(f"could not execute Bazel: {error}")
    except subprocess.CalledProcessError as error:
        fail(f"Bazel failed with exit code {error.returncode}")
    return completed.stdout if capture_output else ""


def copy_executable(source: Path, destination: Path) -> None:
    if not source.is_file() or not os.access(source, os.X_OK):
        fail(f"Bazel did not produce an executable: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix=f".{destination.name}.", dir=destination.parent, delete=False) as temporary:
        temporary_path = Path(temporary.name)
    try:
        shutil.copy2(source, temporary_path)
        os.replace(temporary_path, destination)
    finally:
        temporary_path.unlink(missing_ok=True)


def build(args: argparse.Namespace) -> Path:
    openroad_root, sources, bazel, target = validate_inputs(args)
    output = args.output.resolve()
    if args.output_base is not None:
        cache_startup_option = f"--output_base={args.output_base.resolve()}"
    else:
        bazel_user_root = (
            args.output_user_root.resolve()
            if args.output_user_root is not None
            else user_root_for(openroad_root)
        )
        bazel_user_root.mkdir(parents=True, exist_ok=True)
        cache_startup_option = f"--output_user_root={bazel_user_root}"
    install_base = (
        args.install_base.resolve()
        if args.install_base is not None
        else Path(tempfile.gettempdir()) / "eccdb-odb-bazel-install"
    )

    # Copy the current workspace sources, rather than referring to them by an
    # absolute path, so Bazel sees one self-contained root package overlay.
    with tempfile.TemporaryDirectory(prefix="eccdb-db-only-odb-overlay-") as temporary_directory:
        overlay = Path(temporary_directory)
        for source in sources:
            shutil.copy2(source, overlay / source.name)
        (overlay / "BUILD.bazel").write_text(
            OVERLAY_BUILD.format(target=target, sources=json.dumps([source.name for source in sources])),
            encoding="utf-8",
        )

        package_path = f"{overlay}{os.pathsep}%workspace%"
        command_options = [
            f"--package_path={package_path}",
            # OpenROAD is read-only to this entry point.  Put Bazel's
            # convenience symlinks in the disposable overlay instead.
            f"--symlink_prefix={overlay / 'bazel-'}",
        ]
        startup_options = [
            bazel,
            "--batch",
            f"--install_base={install_base}",
            cache_startup_option,
        ]
        # Copy the environment verbatim.  In particular, HTTP(S)_PROXY and
        # related variables are inherited but never added, changed, or unset.
        environment = os.environ.copy()
        # Do not inherit OpenROAD's remote cache setting.  This entry point
        # only consumes the local cache and preserves proxy variables for any
        # explicitly enabled repository fetches.
        mode_options = ["--remote_cache="]
        if args.offline:
            mode_options.append("--nofetch")
        build_command = [*startup_options, "build", *command_options, f"--jobs={args.jobs}"]
        build_command.extend(mode_options)
        build_command.append(f"//:{target}")
        run(build_command, cwd=openroad_root, environment=environment)

        bazel_bin = Path(
            run(
                [*startup_options, "info", *command_options, *mode_options, "bazel-bin"],
                cwd=openroad_root,
                environment=environment,
                capture_output=True,
            ).strip()
        )
        artifact = bazel_bin / target
        copy_executable(artifact, output)

    return output


def main() -> int:
    try:
        output = build(parse_args())
        print(f"wrote {output}")
        return 0
    except (OSError, RuntimeError) as error:
        print(f"BuildDbOnlyOdb.py: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

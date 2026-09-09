# EnTT

This directory contains the EnTT headers used by the EnTT-based database
experiments in this repository. EnTT is header-only and is consumed directly
by the C++ targets that include this directory; no upstream build target is
required.

- Upstream: https://github.com/skypjack/entt
- Source release: `v4.0.0`
- Source commit: `85c6bba014049b5de8fad49d25424df2f1f6a8c1`
- License: MIT; see `LICENSE`

Only the headers required by the current database experiments are included
from the upstream `src/entt` tree. This is a curated, directly vendored copy
rather than a Git submodule, and it does not represent a complete upstream
checkout. When updating it, copy the required headers from a specific upstream
commit and record the source release and commit above.

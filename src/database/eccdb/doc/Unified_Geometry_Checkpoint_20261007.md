# Unified Geometry checkpoint — 2026-10-07

Branch: `refactor/eccdb-unified-geometry-20261003`.

This checkpoint includes the accumulated geometry migration and subsequent native GUI services. It preserves the existing working tree, including the intended retirement of the old GeometryPool and obsolete tests/benchmarks. The separate Python dependency-lock refresh does not change the geometry model.

## Storage and service boundary

- Design, Library and Technology retain separate GeometryRegistry instances. Each canonical primitive has a GeometryShapeId, GeometryShape metadata and one typed payload; owners retain ordered ID relations. Instances reuse master geometry rather than cloning every pin into Design.
- Design geometry covers routing, SPECIALNET geometry, block pins, design vias, regions/blockages/fills, inline instance bounds, die/core boundaries, guides and track assignments. Routing, Pin and Via payloads still have distinct component types; a common RectangleGeometry/PolygonGeometry schema is a future proposal, not part of this checkpoint.
- GeometryBounds is an optional derived cache. The spatial index stores BBoxes and side-array references; it does not copy the canonical payload. Known service edits incrementally maintain a warm R-tree and invalidate affected overview tiles. Other geometry changes use epoch-based lazy rebuilding.
- GeometryQueryService, GeometryPayloadService, GeometryOccurrenceService, GeometryEditService and DesignModelService provide viewport queries, exact detached primitive batches, shared-master occurrences, editing/undo and typed model inspection. Python receives read-only NumPy views of detached batches, not mutable EnTT storage.
- LEF/DEF, binary persistence, iRT/iDRC and native TritonRoute consumers use the new owner/ID relations. Binary schemas are Tech 11, Library 5 and Design 12; older incompatible snapshots should be rebuilt from LEF/DEF.

## Build hygiene

Both the repository and standalone EccDB CMake entry points reject in-source builds. Use an explicit `-B` directory:

```bash
# Default standalone core and tests; no SI2 importer dependency.
cmake -S src/database/eccdb -B build/eccdb-core -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DECCDB_BUILD_TESTS=ON
cmake --build build/eccdb-core --parallel 16
ctest --test-dir build/eccdb-core --output-on-failure

# Add LEF/DEF importers and their IO/persistence tests.
cmake -S src/database/eccdb -B build/eccdb-io -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DECCDB_BUILD_TESTS=ON \
  -DECCDB_STANDALONE_LEF_DEF=ON
cmake --build build/eccdb-io --parallel 16
```

Binary and DEF test directories are registered only when their actual dependency targets exist. The optional design archive test checks `eccdb_def_import`, not merely the SI2 `def` parser target.

The repository `.gitignore` covers the default build directory, root `build-*`/`build_*`, standalone EccDB build variants, CMake/Ninja intermediates, compiled outputs and Python tooling caches. The existing repository `bin/` output convention is retained for full-tool scripts and remains ignored. Build scripts, builder sources and new geometry tests remain visible to Git.

Tracked source/header additions must be staged explicitly; `git commit -am` alone does not include newly added GeometryRegistry, query services, bindings or unified tests. The pre-checkpoint audit found 37 intentional new source/test/document files and no generated files among them. Existing tracked binary dependencies are unaffected by ignore rules.

## Validation and remaining limits

The checkpoint validation uses a fresh ignored build tree at `build/checkpoint-unified-20261007`, with ASan/UBSan and compiler warnings enabled. Exact commands and final results are recorded in `workspace/daily/2026年10月7日/ECCDB_Unified_Checkpoint_And_Build_Hygiene_20261007.md`.

The default core/test build and the LEF/DEF-enabled IO build both succeeded. Across the two configurations, 146 distinct enabled tests passed: 117 core/service tests and 29 additional IO/persistence tests. One core fixture originally read a borrowed ID span after clearing its storage; the fixture now copies the two IDs before clearing, and its sanitizer rerun passed. Two pre-existing disabled DEF cases and two large corpus/stress cases were not run. This is not a full-tool or performance validation.

Earlier migration evidence remains in [Geometry_Migration_20261003.md](Geometry_Migration_20261003.md); its results describe that historical work, not a new performance run. The latest checkpoint does not rerun the large ISPD Test10 replay, restore deleted build artifacts, or claim all GUI operations meet 100 ms.

Remaining boundaries include domain-specific duplicate payload layouts, partially populated aggregate Bounds, GUI batch invalidation by global epoch, and overview cache eviction by whole-map clearing at its limit. The independent legacy `src/database/data/geometry` pipeline still exists. Undo is session-local, and geometry edits do not automatically repair routing connectivity or DRC legality. Retired LEF/legacy conversion tests and old replay targets are a real coverage/tooling reduction; they were not silently replaced by equivalent tests.

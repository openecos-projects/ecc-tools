# ECCDB Rust API

One crate, two internal layers: private raw C ABI declarations and a safe Rust
API. The database implementation remains C++. No CXX or third-party Rust crates
are required. Rust 1.85 or newer, a C11 compiler and ar are required.

Build/install the C ABI first (see ../c/README.md), then:

```sh
export ECCDB_LIB_DIR=/absolute/path/to/sdk/lib
export ECCDB_INCLUDE_DIR=/absolute/path/to/sdk/include
export LD_LIBRARY_PATH="$ECCDB_LIB_DIR:${LD_LIBRARY_PATH:-}"
cargo test --manifest-path src/database/eccdb/api/rust/Cargo.toml --offline
cargo run --manifest-path src/database/eccdb/api/rust/consumer/Cargo.toml --offline -- \
  /absolute/path/to/build/eccdb-c-api/api/c/test-data
cargo package --manifest-path src/database/eccdb/api/rust/Cargo.toml --allow-dirty --offline
```

Generate test-data first with the C ABI fixture target/test. The consumer is an
independent Cargo project with its own main; it uses only the public safe crate.
ECCDB_INCLUDE_DIR selects the C header used for layout checks; otherwise the
packaged ABI header is used. Always pair the crate with the matching experimental
C ABI library. Native libraries are not embedded in the .crate archive.

```toml
[dependencies]
eccdb = { path = "/path/to/eccdb/api/rust" }
```

```no_run
use eccdb::{BinaryFiles, Database, Result};
fn main() -> Result<()> {
    let mut db = Database::open_binary(BinaryFiles::new("tech.bin", "library.bin", "design.bin"))?;
    if let Some(id) = db.find_instance("u1")? {
        let mut instance = db.instance(id)?.expect("existing instance");
        instance.origin.x += 100;
        db.update_instance(id, &instance)?;
    }
    db.write_def("edited.def")?;
    Ok(())
}
```

## Ownership and API coverage

- Database owns its native handle and closes it in Drop. It is neither Send nor
  Sync; writes require &mut self. No raw pointer is exposed publicly.
- Snapshots own String/Vec contents. Native output guards release allocations on
  both success and error, including when UTF-8 conversion fails.
- Typed IDs are database-local. Never use an ID from another database; resolve
  names again after reopening. from_packed supports metadata obtained elsewhere.
- Missing objects become Result<Option<T>>. Semantic failures become typed
  ErrorKind values. Input strings with NUL are rejected. Unix paths preserve
  non-UTF-8 bytes; returned database text must be UTF-8.
- Config covers binary and multi-LEF/DEF input, polygon mode and binary cache.
- Net, Instance, InstancePin, IoPin, Wire and DesignVia operations cover the C ABI.
  Full NetOptions, nested routing and fixed/generated via snapshots are mapped.
- C init/free functions are internal implementation details. Convenience C
  aliases are consolidated (for example net_wire_ids covers both net wire APIs).
- Raw declarations are checked against C with size, alignment and field-offset
  tests. The packaged C header is a snapshot, not a promise of stable ABI.

## Layout

```text
src/lib.rs        public exports
src/ffi.rs        private raw FFI and C layout checks
src/types.rs      IDs, enums, owned snapshots, configuration
src/convert.rs    temporary input storage and guarded output conversion
src/database.rs   safe database operations and Drop
src/error.rs      status-to-Result mapping
native/           C ABI header snapshot and layout probe
consumer/         independent Rust application
```

## Verified

- Offline cargo test: five tests, including 241 C/Rust size/alignment/offset
  checks, optional zero values, malformed text, nested input lifetimes and
  cleanup during errors/unwinding.
- Three compile-fail doctests check that Database is not Send/Sync and that
  NetId cannot be passed where InstanceId is required.
- The independent consumer exercises all entity families against the binary
  fixture, including generated/fixed vias, edits, persistence and DEF export.
- Clippy with warnings denied passes for both the crate and the consumer.
- cargo package builds the extracted archive successfully. The resulting
  target/package/eccdb-0.1.0.crate is ready for local/internal distribution;
  it has not been published to a registry.

Native libraries remain a separate deployment requirement. Validation used
the binary-only native build: multi-LEF import availability still depends on
the C++ importers, as reported by Database::supports_lef_def().

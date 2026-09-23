# Independent Rust caller

This is a separate Cargo package. Both src/main.rs and tests/api.rs use only
the public eccdb dependency. They do not access private FFI or C++ storage.

First run the C fixture producer in a dedicated directory to create tech.bin,
library.bin, design.bin and ids.txt. Then run:

```sh
export ECCDB_LIB_DIR=/absolute/path/to/sdk/lib
export ECCDB_INCLUDE_DIR=/absolute/path/to/sdk/include
export LD_LIBRARY_PATH="$ECCDB_LIB_DIR:${LD_LIBRARY_PATH:-}"
export ECCDB_TEST_DATA=/absolute/path/to/fixture-directory
cargo test --manifest-path src/database/eccdb/api/rust/consumer/Cargo.toml \
  --offline --test api -- --nocapture
cargo run --manifest-path src/database/eccdb/api/rust/consumer/Cargo.toml \
  --offline -- "$ECCDB_TEST_DATA"
```

The five black-box tests check:

1. Detached snapshots do not mutate the database before update and remain owned
   after the database closes.
2. Invalid edits, duplicate names and embedded NUL inputs return errors without
   changing existing objects.
3. Pin/net relationships are bidirectional; referenced nets cannot be removed;
   destroyed IDs remain invalid after new entities are created.
4. Full generated-via definitions, wire edits, optional zero values and net
   options survive binary save/reopen. Invalid routing updates leave the prior
   routing intact. DEF output contains the expected net/via names.
5. Missing files, unavailable LEF/DEF import and unwritable output paths report
   errors rather than preventing subsequent API calls.

Each test opens its own database. Tests never overwrite fixture inputs.
Persistence outputs use test-owned temporary directories removed on completion.
LEF/DEF successful import is not covered when using the binary-only native build.

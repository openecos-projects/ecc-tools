//! Safe, owned Rust snapshots over ECCDB's experimental C ABI.
//! IDs are database-local; do not use an ID with a different database.
#![deny(unsafe_op_in_unsafe_fn)]
mod convert;
mod database;
mod error;
mod ffi;
mod types;
pub use database::Database;
pub use error::{Error, ErrorKind, Result};
pub use types::*;

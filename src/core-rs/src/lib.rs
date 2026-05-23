//! Typio Core — Rust implementation of business logic
//!
//! This crate exports a C ABI compatible with `include/typio/*.h`.
//! It is intended to gradually replace the C runtime implementations
//! in `src/core/runtime/`.

pub mod config;
pub mod config_schema;
pub mod input_context;
pub mod rime_schema_list;
pub mod types;

// Re-export at crate root so cbindgen can see them easily
pub use config::*;
pub use config_schema::*;
pub use input_context::*;
pub use rime_schema_list::*;
pub use types::*;

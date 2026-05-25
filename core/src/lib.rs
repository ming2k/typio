//! Typio Core — Rust implementation of business logic
//!
//! This crate exports a C ABI compatible with `include/typio/*.h`.
//! It is intended to gradually replace the C runtime implementations
//! in `core/src/runtime/`.

// This crate is a C-ABI boundary: almost every public function is a
// `#[no_mangle] extern "C"` entry point that dereferences raw pointers passed
// in by the C caller. Marking them all `unsafe` does not change the C-callable
// signature and only adds noise, so the lint is allowed crate-wide. The safety
// contract lives at the call sites in `daemon/` and the engine plugins.
#![allow(clippy::not_unsafe_ptr_arg_deref)]

pub mod build_info;
pub mod config;
pub mod config_schema;
pub mod engine;
pub mod engine_label;
pub mod engine_manager;
pub mod event;
pub mod input_context;
pub mod instance;
pub mod log;
pub mod rime_schema_list;
pub mod string;
pub mod types;
pub mod voice;

// Re-export at crate root so cbindgen can see them easily
pub use build_info::*;
pub use config::*;
pub use config_schema::*;
pub use engine::*;
pub use engine_label::*;
pub use engine_manager::*;
pub use event::*;
pub use input_context::*;
pub use instance::*;
pub use log::*;
pub use rime_schema_list::*;
pub use string::*;
pub use types::*;

//! Typio Core — Rust implementation of business logic
//!
//! This crate exports a C ABI compatible with `include/typio/*.h`.
//! It is intended to gradually replace the C runtime implementations
//! in `src/core/runtime/`.

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

//! Rust-native engine trait hierarchy.
//!
//! Built-in engines implement these traits directly.  Plugin engines are
//! adapted via `CPluginAdapter` in `c_plugin_adapter.rs`.  The unified
//! `RustEngineAdapter` in `rust_adapter.rs` wraps any `Box<dyn Engine>` in a
//! `*mut TypioEngine` shell so the daemon C ABI needs no changes.

use crate::types::{TypioEngineMode, TypioEngineType, TypioKeyEvent, TypioKeyProcessResult, TypioResult};
use crate::{TypioInputContext, TypioInstance};
use std::ffi::c_char;

/// Pure-Rust engine metadata used internally by core.
///
/// Fields intentionally mirror the leading fields of `TypioEngineInfo` so that
/// an adapter can construct a C-compatible info struct when creating the
/// wrapper shell.  The two ABI-version sentinels (`api_version`, `struct_size`)
/// are added by the adapter layer, not by engine authors.
pub struct EngineInfo {
    pub name: *const c_char,
    pub display_name: *const c_char,
    pub description: *const c_char,
    pub version: *const c_char,
    pub author: *const c_char,
    pub icon: *const c_char,
    pub language: *const c_char,
    pub type_: TypioEngineType,
    pub capabilities: u32,
}

/// Base trait implemented by every engine (keyboard, voice, or future kinds).
pub trait Engine: Send {
    fn info(&self) -> &EngineInfo;
    fn init(&mut self, instance: &mut TypioInstance) -> TypioResult;
    fn deactivate(&mut self);
    fn focus_in(&mut self, ctx: *mut TypioInputContext);
    fn focus_out(&mut self, ctx: *mut TypioInputContext);
    fn reset(&mut self, ctx: *mut TypioInputContext);
    fn reload_config(&mut self) -> TypioResult;

    /// Down-cast to keyboard-specific operations, if applicable.
    fn as_keyboard(&mut self) -> Option<&mut dyn KeyboardEngine> {
        None
    }

    /// Down-cast to voice-specific operations, if applicable.
    fn as_voice(&mut self) -> Option<&mut dyn VoiceEngine> {
        None
    }
}

/// Extension trait for keyboard engines.
pub trait KeyboardEngine: Engine {
    fn process_key(&mut self, ctx: *mut TypioInputContext, event: &TypioKeyEvent) -> TypioKeyProcessResult;
    fn get_mode(&self, ctx: *mut TypioInputContext) -> Option<*const TypioEngineMode>;
    fn set_mode(&mut self, ctx: *mut TypioInputContext, mode_id: &str) -> TypioResult;
}

/// Extension trait for voice engines.
///
/// Requires `Send + Sync` because `process_audio` is called from the inference
/// thread in `voice/session.rs` while the main thread may call `focus_in`.
pub trait VoiceEngine: Engine + Send + Sync {
    fn is_ready(&self) -> bool;
    fn process_audio(&self, samples: &[f32]) -> Option<String>;
}

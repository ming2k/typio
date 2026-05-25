//! Adapts legacy C plugin engines (Rime, Mozc, etc.) to the Rust `Engine` trait.
//!
//! Plugin `.so` files continue to export `typio_engine_create` and
//! `typio_engine_get_info`.  `engine_manager/load.rs` `dlopen`s them, calls the
//! factory, and wraps the returned `*mut TypioEngine` in a `CPluginAdapter`.
//!
//! All trait method calls delegate to the plugin's original C vtable.  This is
//! the only place in core where built-in code dereferences a plugin's
//! `base_ops` / `keyboard` / `voice` pointers.

use crate::engine::r#trait::{Engine, EngineInfo, KeyboardEngine, VoiceEngine};
use crate::types::*;
use crate::{TypioInputContext, TypioInstance};
use std::ffi::{c_void, CStr, CString};
use std::ptr;

pub struct CPluginAdapter {
    c_engine: *mut TypioEngine,
    info: EngineInfo,
}

impl CPluginAdapter {
    /// Wrap a raw plugin `TypioEngine`.
    ///
    /// # Safety
    /// `c_engine` must be a valid, fully-constructed `TypioEngine` returned by a
    /// plugin's `typio_engine_create`.  The adapter **takes ownership** of the
    /// pointer and releases it with `typio_engine_free` on drop (which runs the
    /// plugin's `destroy` callback and frees the engine struct and config path).
    pub unsafe fn new(c_engine: *mut TypioEngine) -> Self {
        let info = if c_engine.is_null() || (*c_engine).info.is_null() {
            EngineInfo {
                name: c"unknown".as_ptr(),
                display_name: c"Unknown".as_ptr(),
                description: ptr::null(),
                version: ptr::null(),
                author: ptr::null(),
                icon: ptr::null(),
                language: ptr::null(),
                type_: TypioEngineType::TypioEngineTypeCustom,
                capabilities: 0,
            }
        } else {
            let c_info = &*(*c_engine).info;
            EngineInfo {
                name: c_info.name,
                display_name: c_info.display_name,
                description: c_info.description,
                version: c_info.version,
                author: c_info.author,
                icon: c_info.icon,
                language: c_info.language,
                type_: c_info.type_,
                capabilities: c_info.capabilities,
            }
        };
        Self { c_engine, info }
    }
}

// SAFETY: CPluginAdapter is used only from the engine manager thread.
// The raw pointer is never shared across threads concurrently.
unsafe impl Send for CPluginAdapter {}
unsafe impl Sync for CPluginAdapter {}

// SAFETY: EngineInfo contains only pointers to static/immortal strings.
unsafe impl Send for EngineInfo {}
unsafe impl Sync for EngineInfo {}

impl Drop for CPluginAdapter {
    fn drop(&mut self) {
        if self.c_engine.is_null() {
            return;
        }
        // Runs the plugin's `destroy` callback and frees the engine struct and
        // config_path allocated by `typio_engine_new` / `set_config_path`.
        crate::engine::typio_engine_free(self.c_engine);
        self.c_engine = ptr::null_mut();
    }
}

impl Engine for CPluginAdapter {
    fn info(&self) -> &EngineInfo {
        &self.info
    }

    fn init(&mut self, instance: &mut TypioInstance) -> TypioResult {
        unsafe {
            if self.c_engine.is_null() {
                return TypioResult::TypioErrorInvalidArgument;
            }
            (*self.c_engine).instance = instance as *mut TypioInstance;
            let base = &*(*self.c_engine).base_ops;
            match base.init {
                Some(f) => f(self.c_engine, instance),
                None => TypioResult::TypioOk,
            }
        }
    }

    fn deactivate(&mut self) {
        unsafe {
            if self.c_engine.is_null() {
                return;
            }
            let base = &*(*self.c_engine).base_ops;
            if let Some(f) = base.deactivate {
                f(self.c_engine);
            }
        }
    }

    fn focus_in(&mut self, ctx: *mut TypioInputContext) {
        unsafe {
            if self.c_engine.is_null() {
                return;
            }
            let base = &*(*self.c_engine).base_ops;
            if let Some(f) = base.focus_in {
                f(self.c_engine, ctx);
            }
        }
    }

    fn focus_out(&mut self, ctx: *mut TypioInputContext) {
        unsafe {
            if self.c_engine.is_null() {
                return;
            }
            let base = &*(*self.c_engine).base_ops;
            if let Some(f) = base.focus_out {
                f(self.c_engine, ctx);
            }
        }
    }

    fn reset(&mut self, ctx: *mut TypioInputContext) {
        unsafe {
            if self.c_engine.is_null() {
                return;
            }
            let base = &*(*self.c_engine).base_ops;
            if let Some(f) = base.reset {
                f(self.c_engine, ctx);
            }
        }
    }

    fn reload_config(&mut self) -> TypioResult {
        unsafe {
            if self.c_engine.is_null() {
                return TypioResult::TypioErrorInvalidArgument;
            }
            let base = &*(*self.c_engine).base_ops;
            match base.reload_config {
                Some(f) => f(self.c_engine),
                None => TypioResult::TypioOk,
            }
        }
    }

    fn as_keyboard(&mut self) -> Option<&mut dyn KeyboardEngine> {
        unsafe {
            if self.c_engine.is_null() || (*self.c_engine).keyboard.is_null() {
                return None;
            }
        }
        Some(self)
    }

    fn as_voice(&mut self) -> Option<&mut dyn VoiceEngine> {
        unsafe {
            if self.c_engine.is_null() || (*self.c_engine).voice.is_null() {
                return None;
            }
        }
        Some(self)
    }
}

impl KeyboardEngine for CPluginAdapter {
    fn process_key(
        &mut self,
        ctx: *mut TypioInputContext,
        event: &TypioKeyEvent,
    ) -> TypioKeyProcessResult {
        unsafe {
            let kb = &*(*self.c_engine).keyboard;
            match kb.process_key {
                Some(f) => f(self.c_engine, ctx, event as *const _ as *const c_void),
                None => TypioKeyProcessResult::TypioKeyNotHandled,
            }
        }
    }

    fn get_mode(&self, ctx: *mut TypioInputContext) -> Option<*const TypioEngineMode> {
        unsafe {
            let kb = &*(*self.c_engine).keyboard;
            match kb.get_mode {
                Some(f) => {
                    let mode = f(self.c_engine, ctx);
                    if mode.is_null() {
                        None
                    } else {
                        Some(mode)
                    }
                }
                None => None,
            }
        }
    }

    fn set_mode(&mut self, ctx: *mut TypioInputContext, mode_id: &str) -> TypioResult {
        unsafe {
            let kb = &*(*self.c_engine).keyboard;
            match kb.set_mode {
                Some(f) => {
                    let c_mode = CString::new(mode_id).unwrap_or_default();
                    f(self.c_engine, ctx, c_mode.as_ptr())
                }
                None => TypioResult::TypioOk,
            }
        }
    }
}

impl VoiceEngine for CPluginAdapter {
    fn is_ready(&self) -> bool {
        unsafe {
            let voice = &*(*self.c_engine).voice;
            match voice.is_ready {
                Some(f) => f(self.c_engine),
                None => true,
            }
        }
    }

    fn process_audio(&self, samples: &[f32]) -> Option<String> {
        unsafe {
            let voice = &*(*self.c_engine).voice;
            match voice.process_audio {
                Some(f) => {
                    let c_str = f(self.c_engine, samples.as_ptr(), samples.len());
                    if c_str.is_null() {
                        None
                    } else {
                        let s = CStr::from_ptr(c_str).to_string_lossy().into_owned();
                        libc::free(c_str as *mut c_void);
                        Some(s)
                    }
                }
                None => None,
            }
        }
    }
}

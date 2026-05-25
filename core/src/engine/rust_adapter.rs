//! Unified `TypioEngine` wrapper for Rust trait objects.
//!
//! Every built-in engine implements `Engine` (and optionally `KeyboardEngine` or
//! `VoiceEngine`).  `create_wrapper_typio_engine` produces a `*mut TypioEngine`
//! whose vtables all point to the trampolines in this module.  The trampolines
//! extract `Box<dyn Engine>` from `user_data` and forward to the trait method.
//!
//! This is the **only** file in `core/src/engine/` that contains `extern "C"`
//! functions for built-in engine dispatch.

use crate::engine::r#trait::Engine;
use crate::types::*;
use crate::{TypioInputContext, TypioInstance};
use std::ffi::{c_char, c_void, CStr, CString};
use std::ptr;

/* -------------------------------------------------------------------------- */
/* Helper to reach the trait object inside a wrapper shell                    */
/* -------------------------------------------------------------------------- */

/// # Safety
/// `c_engine` must be a wrapper created by `create_wrapper_typio_engine` (or
/// by `CPluginAdapter` via the same convention) where `user_data` holds a
/// `*mut Box<dyn Engine>`.
pub(crate) unsafe fn get_engine<'a>(c_engine: *mut TypioEngine) -> Option<&'a mut dyn Engine> {
    if c_engine.is_null() {
        return None;
    }
    let ptr = (*c_engine).user_data as *mut Box<dyn Engine>;
    if ptr.is_null() {
        return None;
    }
    Some(&mut **ptr)
}

/* -------------------------------------------------------------------------- */
/* Wrapper lifecycle                                                          */
/* -------------------------------------------------------------------------- */

/// Create a C-compatible `TypioEngine` shell around a Rust trait object.
///
/// The returned pointer is suitable for returning to daemon code.  All vtable
/// entries point to the unified trampolines below.  `c_info` must remain valid
/// for the lifetime of the wrapper (built-in engines use `'static` data).
pub fn create_wrapper_typio_engine(
    engine: Box<dyn Engine>,
    c_info: *const TypioEngineInfo,
) -> *mut TypioEngine {
    let c_engine = unsafe { libc::calloc(1, std::mem::size_of::<TypioEngine>()) as *mut TypioEngine };
    if c_engine.is_null() {
        return ptr::null_mut();
    }

    let engine_ptr = Box::into_raw(Box::new(engine));

    unsafe {
        (*c_engine).info = c_info;
        (*c_engine).base_ops = &UNIFIED_BASE_OPS;
        (*c_engine).user_data = engine_ptr as *mut c_void;

        // Wire extension vtables only when the engine advertises them.
        let engine_ref = &mut *engine_ptr;
        if engine_ref.as_keyboard().is_some() {
            (*c_engine).keyboard = &UNIFIED_KEYBOARD_OPS;
        }
        if engine_ref.as_voice().is_some() {
            (*c_engine).voice = &UNIFIED_VOICE_OPS;
        }
    }

    c_engine
}

/// Tear down a wrapper created by `create_wrapper_typio_engine`.
///
/// Calls the Rust `drop`, frees `config_path`, and releases the C struct.
/// The `info` pointer is **not** freed — it is either static or owned by the
/// plugin `.so`.
///
/// # Safety
/// `c_engine` must be null or a wrapper previously returned by
/// `create_wrapper_typio_engine`, and must not be used after this call.
pub unsafe fn destroy_wrapper_typio_engine(c_engine: *mut TypioEngine) {
    if c_engine.is_null() {
        return;
    }
    let e = &mut *c_engine;

    if !e.config_path.is_null() {
        libc::free(e.config_path as *mut c_void);
        e.config_path = ptr::null_mut();
    }

    if !e.user_data.is_null() {
        let ptr = e.user_data as *mut Box<dyn Engine>;
        drop(Box::from_raw(ptr));
        e.user_data = ptr::null_mut();
    }

    libc::free(c_engine as *mut c_void);
}

/* -------------------------------------------------------------------------- */
/* Unified trampolines                                                        */
/* -------------------------------------------------------------------------- */

extern "C" fn trampoline_init(
    c_engine: *mut TypioEngine,
    instance: *mut TypioInstance,
) -> TypioResult {
    unsafe {
        let engine = match get_engine(c_engine) {
            Some(e) => e,
            None => return TypioResult::TypioErrorInvalidArgument,
        };
        if instance.is_null() {
            return TypioResult::TypioErrorInvalidArgument;
        }
        engine.init(&mut *instance)
    }
}

extern "C" fn trampoline_destroy(c_engine: *mut TypioEngine) {
    unsafe {
        destroy_wrapper_typio_engine(c_engine);
    }
}

extern "C" fn trampoline_deactivate(c_engine: *mut TypioEngine) {
    unsafe {
        let engine = match get_engine(c_engine) {
            Some(e) => e,
            None => return,
        };
        engine.deactivate();
    }
}

extern "C" fn trampoline_focus_in(
    c_engine: *mut TypioEngine,
    ctx: *mut TypioInputContext,
) {
    unsafe {
        let engine = match get_engine(c_engine) {
            Some(e) => e,
            None => return,
        };
        engine.focus_in(ctx);
    }
}

extern "C" fn trampoline_focus_out(
    c_engine: *mut TypioEngine,
    ctx: *mut TypioInputContext,
) {
    unsafe {
        let engine = match get_engine(c_engine) {
            Some(e) => e,
            None => return,
        };
        engine.focus_out(ctx);
    }
}

extern "C" fn trampoline_reset(
    c_engine: *mut TypioEngine,
    ctx: *mut TypioInputContext,
) {
    unsafe {
        let engine = match get_engine(c_engine) {
            Some(e) => e,
            None => return,
        };
        engine.reset(ctx);
    }
}

extern "C" fn trampoline_reload_config(c_engine: *mut TypioEngine) -> TypioResult {
    unsafe {
        let engine = match get_engine(c_engine) {
            Some(e) => e,
            None => return TypioResult::TypioErrorInvalidArgument,
        };
        engine.reload_config()
    }
}

/* --- Keyboard trampolines ------------------------------------------------- */

extern "C" fn trampoline_process_key(
    c_engine: *mut TypioEngine,
    ctx: *mut TypioInputContext,
    event: *const c_void,
) -> TypioKeyProcessResult {
    unsafe {
        let engine = match get_engine(c_engine) {
            Some(e) => e,
            None => return TypioKeyProcessResult::TypioKeyNotHandled,
        };
        let kb = match engine.as_keyboard() {
            Some(k) => k,
            None => return TypioKeyProcessResult::TypioKeyNotHandled,
        };
        let event = &*(event as *const TypioKeyEvent);
        kb.process_key(ctx, event)
    }
}

extern "C" fn trampoline_get_mode(
    c_engine: *mut TypioEngine,
    ctx: *mut TypioInputContext,
) -> *const TypioEngineMode {
    unsafe {
        let engine = match get_engine(c_engine) {
            Some(e) => e,
            None => return ptr::null(),
        };
        let kb = match engine.as_keyboard() {
            Some(k) => k,
            None => return ptr::null(),
        };
        kb.get_mode(ctx).unwrap_or(ptr::null())
    }
}

extern "C" fn trampoline_set_mode(
    c_engine: *mut TypioEngine,
    ctx: *mut TypioInputContext,
    mode_id: *const c_char,
) -> TypioResult {
    unsafe {
        let engine = match get_engine(c_engine) {
            Some(e) => e,
            None => return TypioResult::TypioErrorInvalidArgument,
        };
        let kb = match engine.as_keyboard() {
            Some(k) => k,
            None => return TypioResult::TypioErrorInvalidArgument,
        };
        let mode_id = if mode_id.is_null() {
            ""
        } else {
            CStr::from_ptr(mode_id).to_str().unwrap_or("")
        };
        kb.set_mode(ctx, mode_id)
    }
}

/* --- Voice trampolines ---------------------------------------------------- */

extern "C" fn trampoline_is_ready(c_engine: *mut TypioEngine) -> bool {
    unsafe {
        let engine = match get_engine(c_engine) {
            Some(e) => e,
            None => return false,
        };
        let voice = match engine.as_voice() {
            Some(v) => v,
            None => return false,
        };
        voice.is_ready()
    }
}

extern "C" fn trampoline_process_audio(
    c_engine: *mut TypioEngine,
    samples: *const f32,
    n_samples: usize,
) -> *mut c_char {
    unsafe {
        let engine = match get_engine(c_engine) {
            Some(e) => e,
            None => return ptr::null_mut(),
        };
        let voice = match engine.as_voice() {
            Some(v) => v,
            None => return ptr::null_mut(),
        };
        if samples.is_null() {
            return ptr::null_mut();
        }
        let slice = std::slice::from_raw_parts(samples, n_samples);
        match voice.process_audio(slice) {
            Some(text) => match CString::new(text) {
                Ok(cstr) => cstr.into_raw(),
                Err(_) => ptr::null_mut(),
            },
            None => ptr::null_mut(),
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Static vtables                                                             */
/* -------------------------------------------------------------------------- */

pub(crate) static UNIFIED_BASE_OPS: TypioEngineBaseOps = TypioEngineBaseOps {
    init: Some(trampoline_init),
    destroy: Some(trampoline_destroy),
    deactivate: Some(trampoline_deactivate),
    focus_in: Some(trampoline_focus_in),
    focus_out: Some(trampoline_focus_out),
    reset: Some(trampoline_reset),
    reload_config: Some(trampoline_reload_config),
};

static UNIFIED_KEYBOARD_OPS: TypioKeyboardEngineOps = TypioKeyboardEngineOps {
    process_key: Some(trampoline_process_key),
    get_mode: Some(trampoline_get_mode),
    set_mode: Some(trampoline_set_mode),
};

static UNIFIED_VOICE_OPS: TypioVoiceEngineOps = TypioVoiceEngineOps {
    is_ready: Some(trampoline_is_ready),
    process_audio: Some(trampoline_process_audio),
};

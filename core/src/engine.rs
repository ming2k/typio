//! Engine lifecycle and helper dispatch — migrated from C `runtime/engine.c`
//! and `runtime/engine_helpers.c`.

use crate::log::_typio_log;
use crate::string::typio_strdup;
use crate::types::*;
use crate::{TypioInputContext, TypioInstance};
use std::ffi::{c_char, c_void, CStr, CString};
use std::ptr;

#[no_mangle]
pub extern "C" fn typio_engine_new(
    info: *const TypioEngineInfo,
    base_ops: *const TypioEngineBaseOps,
    keyboard: *const TypioKeyboardEngineOps,
    voice: *const TypioVoiceEngineOps,
) -> *mut TypioEngine {
    if info.is_null() || base_ops.is_null() {
        return ptr::null_mut();
    }
    unsafe {
        let engine = libc::calloc(1, std::mem::size_of::<TypioEngine>()) as *mut TypioEngine;
        if engine.is_null() {
            return ptr::null_mut();
        }
        (*engine).info = info;
        (*engine).base_ops = base_ops;
        (*engine).keyboard = keyboard;
        (*engine).voice = voice;
        (*engine).active = false;
        (*engine).initialized = false;
        engine
    }
}

#[no_mangle]
pub extern "C" fn typio_engine_free(engine: *mut TypioEngine) {
    if engine.is_null() {
        return;
    }
    unsafe {
        let e = &mut *engine;
        if e.initialized {
            let base = &*e.base_ops;
            if let Some(destroy) = base.destroy {
                destroy(engine);
            }
            e.initialized = false;
        }
        if !e.config_path.is_null() {
            libc::free(e.config_path as *mut c_void);
            e.config_path = ptr::null_mut();
        }
        libc::free(engine as *mut c_void);
    }
}

#[no_mangle]
pub extern "C" fn typio_engine_get_name(engine: *const TypioEngine) -> *const c_char {
    unsafe {
        if engine.is_null() || (*engine).info.is_null() {
            return ptr::null();
        }
        (*(*engine).info).name
    }
}

#[no_mangle]
pub extern "C" fn typio_engine_get_type(engine: *const TypioEngine) -> TypioEngineType {
    unsafe {
        if engine.is_null() || (*engine).info.is_null() {
            return TypioEngineType::TypioEngineTypeKeyboard;
        }
        (*(*engine).info).type_
    }
}

#[no_mangle]
pub extern "C" fn typio_engine_get_capabilities(engine: *const TypioEngine) -> u32 {
    unsafe {
        if engine.is_null() || (*engine).info.is_null() {
            return 0;
        }
        (*(*engine).info).capabilities
    }
}

#[no_mangle]
pub extern "C" fn typio_engine_has_capability(
    engine: *const TypioEngine,
    cap: TypioEngineCapability,
) -> bool {
    unsafe {
        if engine.is_null() || (*engine).info.is_null() {
            return false;
        }
        ((*(*engine).info).capabilities & (cap as u32)) != 0
    }
}

#[no_mangle]
pub extern "C" fn typio_engine_is_active(engine: *const TypioEngine) -> bool {
    unsafe { !engine.is_null() && (*engine).active }
}

#[no_mangle]
pub extern "C" fn typio_engine_set_user_data(engine: *mut TypioEngine, data: *mut c_void) {
    unsafe {
        if !engine.is_null() {
            (*engine).user_data = data;
        }
    }
}

#[no_mangle]
pub extern "C" fn typio_engine_get_user_data(engine: *const TypioEngine) -> *mut c_void {
    unsafe {
        if engine.is_null() {
            return ptr::null_mut();
        }
        (*engine).user_data
    }
}

#[no_mangle]
pub extern "C" fn typio_engine_get_config_path(engine: *const TypioEngine) -> *const c_char {
    unsafe {
        if engine.is_null() {
            return ptr::null();
        }
        (*engine).config_path
    }
}

#[no_mangle]
pub extern "C" fn typio_engine_set_config_path(engine: *mut TypioEngine, path: *const c_char) {
    unsafe {
        if engine.is_null() {
            return;
        }
        let e = &mut *engine;
        if !e.config_path.is_null() {
            libc::free(e.config_path as *mut c_void);
        }
        e.config_path = if path.is_null() {
            ptr::null_mut()
        } else {
            typio_strdup(path)
        };
    }
}

#[no_mangle]
pub extern "C" fn typio_engine_activate(
    engine: *mut TypioEngine,
    instance: *mut TypioInstance,
) -> TypioResult {
    if engine.is_null() {
        return TypioResult::TypioErrorInvalidArgument;
    }
    unsafe {
        let e = &mut *engine;
        if e.active {
            return TypioResult::TypioOk;
        }
        e.instance = instance;

        if !e.initialized {
            let base = &*e.base_ops;
            if let Some(init) = base.init {
                let result = init(engine, instance);
                if result != TypioResult::TypioOk {
                    let name = if e.info.is_null() {
                        "unknown"
                    } else {
                        CStr::from_ptr((*e.info).name).to_str().unwrap_or("unknown")
                    };
                    let msg = CString::new(format!("Failed to initialize engine: {}", name))
                        .unwrap_or_default();
                    _typio_log(TypioLogLevel::TypioLogError, msg.as_ptr());
                    return result;
                }
                e.initialized = true;
            }
        }

        e.active = true;
        let name = if e.info.is_null() {
            "unknown"
        } else {
            CStr::from_ptr((*e.info).name).to_str().unwrap_or("unknown")
        };
        let msg = CString::new(format!("Engine activated: {}", name)).unwrap_or_default();
        _typio_log(TypioLogLevel::TypioLogInfo, msg.as_ptr());

        TypioResult::TypioOk
    }
}

#[no_mangle]
pub extern "C" fn typio_engine_deactivate(engine: *mut TypioEngine) {
    if engine.is_null() {
        return;
    }
    unsafe {
        let e = &mut *engine;
        if !e.active {
            return;
        }
        let base = &*e.base_ops;
        if let Some(deactivate_fn) = base.deactivate {
            deactivate_fn(engine);
        }
        e.active = false;

        let name = if e.info.is_null() {
            "unknown"
        } else {
            CStr::from_ptr((*e.info).name).to_str().unwrap_or("unknown")
        };
        let msg = CString::new(format!("Engine deactivated: {}", name)).unwrap_or_default();
        _typio_log(TypioLogLevel::TypioLogInfo, msg.as_ptr());
    }
}

/* -------------------------------------------------------------------------- */
/* Helper dispatch — previously in engine_helpers.c                           */
/* -------------------------------------------------------------------------- */

#[no_mangle]
pub extern "C" fn _typio_engine_base_focus_in(
    engine: *mut TypioEngine,
    ctx: *mut TypioInputContext,
) {
    unsafe {
        if engine.is_null() {
            return;
        }
        let e = &*engine;
        if e.base_ops.is_null() {
            return;
        }
        let base = &*e.base_ops;
        if let Some(focus_in) = base.focus_in {
            focus_in(engine, ctx);
        }
    }
}

#[no_mangle]
pub extern "C" fn _typio_engine_base_focus_out(
    engine: *mut TypioEngine,
    ctx: *mut TypioInputContext,
) {
    unsafe {
        if engine.is_null() {
            return;
        }
        let e = &*engine;
        if e.base_ops.is_null() {
            return;
        }
        let base = &*e.base_ops;
        if let Some(focus_out) = base.focus_out {
            focus_out(engine, ctx);
        }
    }
}

#[no_mangle]
pub extern "C" fn _typio_engine_base_reset(
    engine: *mut TypioEngine,
    ctx: *mut TypioInputContext,
) {
    unsafe {
        if engine.is_null() {
            return;
        }
        let e = &*engine;
        if e.base_ops.is_null() {
            return;
        }
        let base = &*e.base_ops;
        if let Some(reset) = base.reset {
            reset(engine, ctx);
        }
    }
}

#[no_mangle]
pub extern "C" fn _typio_engine_keyboard_process_key(
    engine: *mut TypioEngine,
    ctx: *mut TypioInputContext,
    event: *const TypioKeyEvent,
) -> TypioKeyProcessResult {
    unsafe {
        if engine.is_null() {
            return TypioKeyProcessResult::TypioKeyNotHandled;
        }
        let e = &*engine;
        if e.keyboard.is_null() {
            return TypioKeyProcessResult::TypioKeyNotHandled;
        }
        let keyboard = &*e.keyboard;
        if let Some(process_key) = keyboard.process_key {
            process_key(engine, ctx, event as *const std::ffi::c_void)
        } else {
            TypioKeyProcessResult::TypioKeyNotHandled
        }
    }
}



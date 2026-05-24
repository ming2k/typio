//! Rime schema state persistence and deployment

use super::{dup_state_string, set_state_string, TypioInstance, TYPIO_RIME_STATE_FILE, TYPIO_RIME_STATE_KEY};
use crate::config;
use crate::engine_manager;
use crate::string::typio_strdup;
use crate::types::*;
use std::ffi::{c_char, CStr, CString};
use std::ptr;

#[no_mangle]
pub extern "C" fn typio_instance_dup_rime_schema(instance: *mut TypioInstance) -> *mut c_char {
    if instance.is_null() {
        return ptr::null_mut();
    }
    let inst = unsafe { &*instance };
    if let Some(schema) = dup_state_string(inst, TYPIO_RIME_STATE_FILE, TYPIO_RIME_STATE_KEY) {
        return unsafe { typio_strdup(schema.as_ptr()) };
    }
    let legacy = if !inst.config.is_null() {
        let key = CString::new("engines.rime.schema").unwrap();
        config::typio_config_get_string(inst.config, key.as_ptr(), ptr::null())
    } else {
        ptr::null()
    };
    if !legacy.is_null() {
        let s = unsafe { CStr::from_ptr(legacy) }.to_string_lossy();
        if !s.is_empty() {
            return unsafe { typio_strdup(legacy) };
        }
    }
    ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn typio_instance_set_rime_schema(instance: *mut TypioInstance, schema: *const c_char) -> TypioResult {
    if instance.is_null() {
        return TypioResult::TypioErrorInvalidArgument;
    }
    let inst = unsafe { &*instance };
    let value = if schema.is_null() {
        None
    } else {
        let s = unsafe { CStr::from_ptr(schema) }.to_str().ok();
        s
    };
    set_state_string(inst, TYPIO_RIME_STATE_FILE, TYPIO_RIME_STATE_KEY, value)
}

#[no_mangle]
pub extern "C" fn typio_instance_deploy_rime_config(instance: *mut TypioInstance) -> TypioResult {
    if instance.is_null() {
        return TypioResult::TypioErrorInvalidArgument;
    }
    let inst = unsafe { &mut *instance };
    if inst.engine_manager.is_null() {
        return TypioResult::TypioErrorInvalidArgument;
    }

    let name_c = CString::new("rime").unwrap();
    let rime = engine_manager::typio_engine_manager_get_engine(inst.engine_manager, name_c.as_ptr());
    if rime.is_null() {
        return TypioResult::TypioErrorNotFound;
    }
    let engine = unsafe { &mut *rime };
    if engine.base_ops.is_null() {
        return TypioResult::TypioErrorNotFound;
    }
    let ops = unsafe { &*engine.base_ops };
    if ops.reload_config.is_none() {
        return TypioResult::TypioErrorNotFound;
    }

    if !engine.initialized {
        if let Some(init) = ops.init {
            engine.instance = instance;
            let result = init(rime, instance);
            if result != TypioResult::TypioOk {
                return result;
            }
            engine.initialized = true;
        }
    }

    inst.rime_deploy_requested = true;
    let result = ops.reload_config.unwrap()(rime);
    inst.rime_deploy_requested = false;
    result
}

#[no_mangle]
pub extern "C" fn typio_instance_rime_deploy_requested(instance: *mut TypioInstance) -> bool {
    if instance.is_null() {
        return false;
    }
    unsafe { (*instance).rime_deploy_requested }
}

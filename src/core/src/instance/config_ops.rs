//! Configuration operations — reload, save, get/set config text

use super::{build_config_path, TypioInstance};
use crate::config;
use crate::config_schema;
use crate::engine::typio_engine_get_name;
use crate::engine_manager;
use crate::engine_manager::log_msg;
use crate::types::*;
use std::ffi::{c_char, CStr, CString};
use std::ptr;

#[no_mangle]
pub extern "C" fn typio_instance_get_config_dir(instance: *mut TypioInstance) -> *const c_char {
    if instance.is_null() {
        return ptr::null();
    }
    unsafe { (*instance).config_dir.as_ref().map(|s| s.as_ptr()).unwrap_or(ptr::null()) }
}

#[no_mangle]
pub extern "C" fn typio_instance_get_data_dir(instance: *mut TypioInstance) -> *const c_char {
    if instance.is_null() {
        return ptr::null();
    }
    unsafe { (*instance).data_dir.as_ref().map(|s| s.as_ptr()).unwrap_or(ptr::null()) }
}

#[no_mangle]
pub extern "C" fn typio_instance_get_state_dir(instance: *mut TypioInstance) -> *const c_char {
    if instance.is_null() {
        return ptr::null();
    }
    unsafe { (*instance).state_dir.as_ref().map(|s| s.as_ptr()).unwrap_or(ptr::null()) }
}

#[no_mangle]
pub extern "C" fn typio_instance_get_config(instance: *mut TypioInstance) -> *mut config::Config {
    if instance.is_null() {
        return ptr::null_mut();
    }
    unsafe { (*instance).config }
}

#[no_mangle]
pub extern "C" fn typio_instance_get_engine_config(instance: *mut TypioInstance, engine_name: *const c_char) -> *mut config::Config {
    if instance.is_null() || engine_name.is_null() {
        return ptr::null_mut();
    }
    let inst = unsafe { &*instance };
    if inst.config.is_null() {
        return ptr::null_mut();
    }
    let name = unsafe { CStr::from_ptr(engine_name) }.to_string_lossy();
    if name.is_empty() {
        return ptr::null_mut();
    }
    let section = format!("engines.{}", name);
    let section_c = CString::new(section).unwrap();
    config::typio_config_get_section(inst.config, section_c.as_ptr())
}

#[no_mangle]
pub extern "C" fn typio_instance_reload_config(instance: *mut TypioInstance) -> TypioResult {
    if instance.is_null() {
        return TypioResult::TypioErrorInvalidArgument;
    }
    let inst = unsafe { &mut *instance };

    let config_dir = match inst.config_dir.as_ref() {
        Some(d) => d.to_string_lossy(),
        None => return TypioResult::TypioErrorInvalidArgument,
    };
    let config_path = build_config_path(&config_dir, super::TYPIO_CONFIG_FILE_NAME);
    let path_c = CString::new(config_path).unwrap();

    let new_config = config::typio_config_load_file(path_c.as_ptr());
    if !new_config.is_null() {
        config_schema::typio_config_apply_defaults(new_config);
        if !inst.config.is_null() {
            config::typio_config_free(inst.config);
        }
        inst.config = new_config;
    }
    if inst.config.is_null() {
        inst.config = config::typio_config_new();
        if inst.config.is_null() {
            return TypioResult::TypioErrorOutOfMemory;
        }
        config_schema::typio_config_apply_defaults(inst.config);
    }

    let configured_default = {
        let key = CString::new("default_engine").unwrap();
        let val = config::typio_config_get_string(inst.config, key.as_ptr(), ptr::null());
        if !val.is_null() {
            let s = unsafe { CStr::from_ptr(val) }.to_string_lossy();
            if !s.is_empty() { Some(s.to_string()) } else { None }
        } else {
            None
        }
    };

    let active = engine_manager::typio_engine_manager_get_active(inst.engine_manager);
    let current_name = if active.is_null() {
        None
    } else {
        let name = typio_engine_get_name(active);
        if name.is_null() {
            None
        } else {
            Some(unsafe { CStr::from_ptr(name) }.to_string_lossy().to_string())
        }
    };

    if let Some(ref configured) = configured_default {
        if current_name.as_ref() != Some(configured) {
            let name_c = CString::new(configured.as_bytes()).unwrap();
            engine_manager::typio_engine_manager_set_active(inst.engine_manager, name_c.as_ptr());
        }
    }

    let active = engine_manager::typio_engine_manager_get_active(inst.engine_manager);
    if !active.is_null() {
        let engine = unsafe { &*active };
        if !engine.base_ops.is_null() {
            let ops = unsafe { &*engine.base_ops };
            if let Some(reload) = ops.reload_config {
                reload(active);
            }
        }
    }

    let configured_voice = {
        let key = CString::new("default_voice_engine").unwrap();
        let val = config::typio_config_get_string(inst.config, key.as_ptr(), ptr::null());
        if !val.is_null() {
            let s = unsafe { CStr::from_ptr(val) }.to_string_lossy();
            if !s.is_empty() { Some(s.to_string()) } else { None }
        } else {
            None
        }
    };

    if let Some(ref voice_name) = configured_voice {
        let active_voice = engine_manager::typio_engine_manager_get_active_voice(inst.engine_manager);
        let current_voice_name = if active_voice.is_null() {
            None
        } else {
            let name = typio_engine_get_name(active_voice);
            if name.is_null() {
                None
            } else {
                Some(unsafe { CStr::from_ptr(name) }.to_string_lossy().to_string())
            }
        };
        if current_voice_name.as_ref() != Some(voice_name) {
            let name_c = CString::new(voice_name.as_bytes()).unwrap();
            engine_manager::typio_engine_manager_set_active_voice(inst.engine_manager, name_c.as_ptr());
        }
    }

    TypioResult::TypioOk
}

#[no_mangle]
pub extern "C" fn typio_instance_save_config(instance: *mut TypioInstance) -> TypioResult {
    if instance.is_null() {
        return TypioResult::TypioErrorInvalidArgument;
    }
    let inst = unsafe { &*instance };
    inst.save_config()
}

#[no_mangle]
pub extern "C" fn typio_instance_get_config_text(instance: *mut TypioInstance) -> *mut c_char {
    if instance.is_null() {
        return ptr::null_mut();
    }
    let inst = unsafe { &*instance };
    if inst.config.is_null() {
        return ptr::null_mut();
    }
    config::typio_config_to_string(inst.config)
}

#[no_mangle]
pub extern "C" fn typio_instance_set_config_text(instance: *mut TypioInstance, content: *const c_char) -> TypioResult {
    if instance.is_null() || content.is_null() {
        return TypioResult::TypioErrorInvalidArgument;
    }
    let inst = unsafe { &mut *instance };

    let parsed = config::typio_config_load_string(content);
    if parsed.is_null() {
        return TypioResult::TypioError;
    }

    let old_key_count = if inst.config.is_null() { 0 } else { config::typio_config_key_count(inst.config) };
    let new_key_count = config::typio_config_key_count(parsed);
    if old_key_count > 0 && new_key_count == 0 {
        let cstr = unsafe { CStr::from_ptr(content) };
        let mut has_content = false;
        for b in cstr.to_bytes() {
            if !b.is_ascii_whitespace() && *b != b'#' && *b != b';' {
                has_content = true;
                break;
            }
        }
        if !has_content {
            log_msg(TypioLogLevel::TypioLogWarning, &format!(
                "Rejecting empty replacement config while existing config has {} keys",
                old_key_count
            ));
            config::typio_config_free(parsed);
            return TypioResult::TypioErrorInvalidArgument;
        }
    }

    config_schema::typio_config_apply_defaults(parsed);

    let old_config = inst.config;
    inst.config = parsed;
    let save_result = inst.save_config();
    if save_result != TypioResult::TypioOk {
        inst.config = old_config;
        config::typio_config_free(parsed);
        return save_result;
    }
    if !old_config.is_null() {
        config::typio_config_free(old_config);
    }

    super::typio_instance_reload_config(instance)
}

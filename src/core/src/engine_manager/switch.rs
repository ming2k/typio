//! Engine switching — activation, cycling, and focus management

use super::{c_str_to_str, log_msg, TypioEngineManager};
use crate::engine::{typio_engine_activate, typio_engine_deactivate};
use crate::instance::{
    typio_instance_clear_mode, typio_instance_clear_status_icon,
    typio_instance_get_focused_context, typio_instance_notify_engine_changed,
    typio_instance_notify_mode, typio_instance_notify_voice_engine_changed,
};
use crate::types::*;
use std::ffi::{c_char, CStr, CString};
use std::ptr;
use std::time::Instant;

#[no_mangle]
pub extern "C" fn typio_engine_manager_set_active(
    manager: *mut TypioEngineManager,
    name: *const c_char,
) -> TypioResult {
    if manager.is_null() || name.is_null() {
        return TypioResult::TypioErrorInvalidArgument;
    }

    let manager_ref = unsafe { &mut *manager };
    let name_str = unsafe { CStr::from_ptr(name) }.to_string_lossy();

    let idx = match manager_ref.find_entry_index(&name_str) {
        Some(i) => i,
        None => return TypioResult::TypioErrorNotFound,
    };

    let entry = &manager_ref.entries[idx];
    if entry.info.is_null() {
        return TypioResult::TypioErrorEngineLoadFailed;
    }
    let is_voice = unsafe { (*entry.info).type_ } == TypioEngineType::TypioEngineTypeVoice;

    if is_voice {
        if manager_ref.active_voice_index == Some(idx) {
            return TypioResult::TypioOk;
        }

        let current = manager_ref.active_voice_index.map(|i| i);
        if let Some(current_idx) = current {
            let entry = &manager_ref.entries[current_idx];
            if !entry.instance.is_null() {
                unsafe { typio_engine_deactivate(entry.instance) };
            }
        }

        let result = manager_ref.ensure_entry_instance(idx);
        if result != TypioResult::TypioOk {
            if let Some(current_idx) = current {
                manager_ref.try_restore_engine(manager_ref.entries.get(current_idx), "voice");
            }
            return result;
        }

        let instance_ptr = manager_ref.entries[idx].instance;
        let result = unsafe { typio_engine_activate(instance_ptr, manager_ref.instance) };
        if result != TypioResult::TypioOk {
            if let Some(current_idx) = current {
                manager_ref.try_restore_engine(manager_ref.entries.get(current_idx), "voice");
            }
            return result;
        }

        let info = manager_ref.entries[idx].info;
        manager_ref.active_voice_index = Some(idx);
        unsafe { typio_instance_notify_voice_engine_changed(manager_ref.instance, info) };
        log_msg(TypioLogLevel::TypioLogInfo, &format!(
            "Active voice engine: {}",
            c_str_to_str(unsafe { (*info).name }).unwrap_or("?")
        ));
        return TypioResult::TypioOk;
    }

    if manager_ref.active_keyboard_index == Some(idx) {
        return TypioResult::TypioOk;
    }

    let current = manager_ref.active_keyboard_index.map(|i| i);
    if let Some(current_idx) = current {
        let entry = &manager_ref.entries[current_idx];
        if !entry.instance.is_null() {
            unsafe { typio_engine_deactivate(entry.instance) };
        }
    }

    let result = manager_ref.ensure_entry_instance(idx);
    if result != TypioResult::TypioOk {
        if let Some(current_idx) = current {
            manager_ref.try_restore_engine(manager_ref.entries.get(current_idx), "keyboard");
        }
        return result;
    }

    let instance_ptr = manager_ref.entries[idx].instance;
    let result = unsafe { typio_engine_activate(instance_ptr, manager_ref.instance) };
    if result != TypioResult::TypioOk {
        if let Some(current_idx) = current {
            manager_ref.try_restore_engine(manager_ref.entries.get(current_idx), "keyboard");
        }
        return result;
    }

    manager_ref.active_keyboard_index = Some(idx);
    manager_ref.last_switch = Some(Instant::now());

    unsafe { typio_instance_clear_status_icon(manager_ref.instance) };
    unsafe { typio_instance_clear_mode(manager_ref.instance) };

    let current_engine = current.and_then(|i| manager_ref.entries.get(i)).map(|e| e.instance).unwrap_or(ptr::null_mut());
    let new_engine = manager_ref.entries[idx].instance;
    manager_ref.rebind_focused_context(current_engine, new_engine);

    let info = manager_ref.entries[idx].info;
    unsafe { typio_instance_notify_engine_changed(manager_ref.instance, info) };

    TypioResult::TypioOk
}

#[no_mangle]
pub extern "C" fn typio_engine_manager_set_active_voice(
    manager: *mut TypioEngineManager,
    name: *const c_char,
) -> TypioResult {
    if manager.is_null() || name.is_null() {
        return TypioResult::TypioErrorInvalidArgument;
    }

    let manager_ref = unsafe { &*manager };
    let name_str = unsafe { CStr::from_ptr(name) }.to_string_lossy();

    let info = manager_ref.find_entry(&name_str).map(|e| e.info).unwrap_or(ptr::null());
    if info.is_null() {
        return TypioResult::TypioErrorNotFound;
    }
    if unsafe { (*info).type_ } != TypioEngineType::TypioEngineTypeVoice {
        log_msg(TypioLogLevel::TypioLogError, &format!("Engine '{}' is not a voice engine", name_str));
        return TypioResult::TypioErrorInvalidArgument;
    }

    typio_engine_manager_set_active(manager, name)
}

#[no_mangle]
pub extern "C" fn typio_engine_manager_get_active(manager: *mut TypioEngineManager) -> *mut TypioEngine {
    if manager.is_null() {
        return ptr::null_mut();
    }
    let manager_ref = unsafe { &*manager };
    manager_ref.active_keyboard_index
        .and_then(|i| manager_ref.entries.get(i))
        .map(|e| e.instance)
        .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub extern "C" fn typio_engine_manager_get_active_by_type(
    manager: *mut TypioEngineManager,
    type_: TypioEngineType,
) -> *mut TypioEngine {
    if type_ == TypioEngineType::TypioEngineTypeVoice {
        typio_engine_manager_get_active_voice(manager)
    } else {
        typio_engine_manager_get_active(manager)
    }
}

#[no_mangle]
pub extern "C" fn typio_engine_manager_get_active_voice(manager: *mut TypioEngineManager) -> *mut TypioEngine {
    if manager.is_null() {
        return ptr::null_mut();
    }
    let manager_ref = unsafe { &*manager };
    manager_ref.active_voice_index
        .and_then(|i| manager_ref.entries.get(i))
        .map(|e| e.instance)
        .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub extern "C" fn typio_engine_manager_next(manager: *mut TypioEngineManager) -> TypioResult {
    if manager.is_null() {
        return TypioResult::TypioErrorEngineNotAvailable;
    }

    let manager_ref = unsafe { &*manager };
    if manager_ref.entries.is_empty() {
        return TypioResult::TypioErrorEngineNotAvailable;
    }

    let ordered = {
        let mut count: usize = 0;
        let ptr = super::typio_engine_manager_list_ordered_keyboards(manager, &mut count);
        let mut vec = Vec::with_capacity(count);
        if !ptr.is_null() {
            for i in 0..count {
                let name_ptr = unsafe { *ptr.add(i) };
                if !name_ptr.is_null() {
                    if let Some(s) = c_str_to_str(name_ptr) {
                        vec.push(s);
                    }
                }
            }
        }
        vec
    };

    let target = manager_ref.resolve_switch(&ordered, 1);
    match target {
        Some(name) => {
            let c_name = CString::new(name).unwrap();
            typio_engine_manager_set_active(manager, c_name.as_ptr())
        }
        None => TypioResult::TypioErrorEngineNotAvailable,
    }
}

#[no_mangle]
pub extern "C" fn typio_engine_manager_prev(manager: *mut TypioEngineManager) -> TypioResult {
    if manager.is_null() {
        return TypioResult::TypioErrorEngineNotAvailable;
    }

    let manager_ref = unsafe { &*manager };
    if manager_ref.entries.is_empty() {
        return TypioResult::TypioErrorEngineNotAvailable;
    }

    let ordered = {
        let mut count: usize = 0;
        let ptr = super::typio_engine_manager_list_ordered_keyboards(manager, &mut count);
        let mut vec = Vec::with_capacity(count);
        if !ptr.is_null() {
            for i in 0..count {
                let name_ptr = unsafe { *ptr.add(i) };
                if !name_ptr.is_null() {
                    if let Some(s) = c_str_to_str(name_ptr) {
                        vec.push(s);
                    }
                }
            }
        }
        vec
    };

    let target = manager_ref.resolve_switch(&ordered, -1);
    match target {
        Some(name) => {
            let c_name = CString::new(name).unwrap();
            typio_engine_manager_set_active(manager, c_name.as_ptr())
        }
        None => TypioResult::TypioErrorEngineNotAvailable,
    }
}

#[no_mangle]
pub extern "C" fn typio_engine_manager_notify_commit(manager: *mut TypioEngineManager) {
    if manager.is_null() {
        return;
    }
    let manager_ref = unsafe { &mut *manager };
    let name = manager_ref.active_keyboard_index
        .and_then(|idx| manager_ref.entries.get(idx))
        .and_then(|entry| entry.name.to_str().ok().map(|s| s.to_string()));
    if let Some(name) = name {
        manager_ref.update_recent_pair(&name);
    }
}

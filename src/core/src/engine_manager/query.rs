//! Engine query functions — listing, info lookup, lazy instantiation

use super::{c_str_to_str, log_msg, TypioEngineManager};
use crate::config;
use crate::engine::typio_engine_set_config_path;
use crate::types::*;
use std::ffi::{c_char, CStr, CString};
use std::ptr;

#[no_mangle]
pub extern "C" fn typio_engine_manager_list(manager: *mut TypioEngineManager, count: *mut usize) -> *const *const c_char {
    if manager.is_null() {
        if !count.is_null() {
            unsafe { *count = 0; }
        }
        return ptr::null();
    }

    let manager_ref = unsafe { &mut *manager };
    let entry_count = manager_ref.entries.len();

    if manager_ref.name_list_cache.is_empty() || manager_ref.name_list_cache.len() != entry_count + 1 {
        manager_ref.name_list_cache.clear();
        for entry in &manager_ref.entries {
            manager_ref.name_list_cache.push(entry.name.as_ptr());
        }
        manager_ref.name_list_cache.push(ptr::null());
    }

    if !count.is_null() {
        unsafe { *count = entry_count; }
    }
    manager_ref.name_list_cache.as_ptr()
}

#[no_mangle]
pub extern "C" fn typio_engine_manager_list_by_type(
    manager: *mut TypioEngineManager,
    type_: TypioEngineType,
    count: *mut usize,
) -> *const *const c_char {
    if manager.is_null() {
        if !count.is_null() {
            unsafe { *count = 0; }
        }
        return ptr::null();
    }

    let manager_ref = unsafe { &*manager };
    let all_count = manager_ref.entries.len();

    let mut filtered: Vec<*const c_char> = Vec::with_capacity(all_count + 1);
    for entry in &manager_ref.entries {
        if entry.info.is_null() {
            continue;
        }
        let info = unsafe { &*entry.info };
        if info.type_ == type_ {
            filtered.push(entry.name.as_ptr());
        }
    }
    filtered.push(ptr::null());

    let result_count = filtered.len().saturating_sub(1);
    let ptr = filtered.as_mut_ptr();
    std::mem::forget(filtered);

    if !count.is_null() {
        unsafe { *count = result_count; }
    }
    ptr
}

#[no_mangle]
pub extern "C" fn typio_engine_manager_list_ordered_keyboards(
    manager: *mut TypioEngineManager,
    count: *mut usize,
) -> *const *const c_char {
    if manager.is_null() {
        if !count.is_null() {
            unsafe { *count = 0; }
        }
        return ptr::null();
    }

    let manager_ref = unsafe { &mut *manager };
    manager_ref.ordered_keyboard_list_cache.clear();

    let keyboard_names: Vec<&str> = manager_ref.entries.iter()
        .filter_map(|e| {
            if e.info.is_null() {
                return None;
            }
            let info = unsafe { &*e.info };
            if info.type_ == TypioEngineType::TypioEngineTypeKeyboard {
                e.name.to_str().ok()
            } else {
                None
            }
        })
        .collect();

    let mut ordered: Vec<&str> = Vec::with_capacity(keyboard_names.len());

    if !manager_ref.instance.is_null() {
        let cfg = unsafe { super::typio_instance_get_config(manager_ref.instance) };
        if !cfg.is_null() {
            let key = CString::new("engine_order").unwrap();
            let order_count = config::typio_config_get_array_size(cfg, key.as_ptr());
            for i in 0..order_count {
                let name = config::typio_config_get_array_string(cfg, key.as_ptr(), i);
                let name_str = c_str_to_str(name);
                if let Some(name_str) = name_str {
                    if !manager_ref.name_is_keyboard(name_str) {
                        continue;
                    }
                    if manager_ref.find_entry(name_str).is_none() {
                        continue;
                    }
                    if !ordered.contains(&name_str) {
                        ordered.push(name_str);
                    }
                }
            }
        }
    }

    for name in &keyboard_names {
        if !ordered.contains(name) {
            ordered.push(name);
        }
    }

    manager_ref.ordered_keyboard_list_cache = ordered.iter().map(|&n| n.as_ptr() as *const c_char).collect();
    manager_ref.ordered_keyboard_list_cache.push(ptr::null());

    if !count.is_null() {
        unsafe { *count = ordered.len(); }
    }
    manager_ref.ordered_keyboard_list_cache.as_ptr()
}

#[no_mangle]
pub extern "C" fn typio_engine_manager_get_info(
    manager: *mut TypioEngineManager,
    name: *const c_char,
) -> *const TypioEngineInfo {
    if manager.is_null() || name.is_null() {
        return ptr::null();
    }
    let manager_ref = unsafe { &*manager };
    let name_str = unsafe { CStr::from_ptr(name) }.to_string_lossy();
    manager_ref.find_entry(&name_str).map_or(ptr::null(), |e| e.info)
}

#[no_mangle]
pub extern "C" fn typio_engine_manager_get_engine(
    manager: *mut TypioEngineManager,
    name: *const c_char,
) -> *mut TypioEngine {
    if manager.is_null() || name.is_null() {
        return ptr::null_mut();
    }

    let manager_ref = unsafe { &mut *manager };
    let name_str = unsafe { CStr::from_ptr(name) }.to_string_lossy();
    let idx = match manager_ref.find_entry_index(&name_str) {
        Some(i) => i,
        None => return ptr::null_mut(),
    };

    let entry = &mut manager_ref.entries[idx];
    if entry.instance.is_null() {
        if let Some(factory) = entry.factory {
            entry.instance = unsafe { factory() };
            if entry.instance.is_null() {
                log_msg(TypioLogLevel::TypioLogError, &format!("Failed to create engine instance: {}", name_str));
                return ptr::null_mut();
            }
            if let Some(path) = TypioEngineManager::engine_config_path(manager_ref.instance, &name_str) {
                unsafe { typio_engine_set_config_path(entry.instance, path.as_ptr()) };
            }
        }
    }

    entry.instance
}

//! Callback registration and notification dispatch

use super::{engine_mode_equal, engine_mode_store, TypioInstance};
use crate::types::*;
use std::ffi::{c_char, c_void, CStr, CString};
use std::ptr;

#[no_mangle]
pub extern "C" fn typio_instance_set_engine_changed_callback(
    instance: *mut TypioInstance,
    callback: TypioEngineChangedCallback,
    user_data: *mut c_void,
) {
    if instance.is_null() {
        return;
    }
    let inst = unsafe { &mut *instance };
    inst.engine_changed_callback = Some(callback);
    inst.engine_changed_user_data = user_data;
}

#[no_mangle]
pub extern "C" fn typio_instance_set_voice_engine_changed_callback(
    instance: *mut TypioInstance,
    callback: TypioVoiceEngineChangedCallback,
    user_data: *mut c_void,
) {
    if instance.is_null() {
        return;
    }
    let inst = unsafe { &mut *instance };
    inst.voice_engine_changed_callback = Some(callback);
    inst.voice_engine_changed_user_data = user_data;
}

#[no_mangle]
pub extern "C" fn typio_instance_set_status_icon_changed_callback(
    instance: *mut TypioInstance,
    callback: TypioStatusIconChangedCallback,
    user_data: *mut c_void,
) {
    if instance.is_null() {
        return;
    }
    let inst = unsafe { &mut *instance };
    inst.status_icon_changed_callback = Some(callback);
    inst.status_icon_changed_user_data = user_data;
}

#[no_mangle]
pub extern "C" fn typio_instance_notify_status_icon(instance: *mut TypioInstance, icon_name: *const c_char) {
    if instance.is_null() || icon_name.is_null() {
        return;
    }
    let inst = unsafe { &mut *instance };
    let name = unsafe { CStr::from_ptr(icon_name) }.to_string_lossy();
    if inst.last_status_icon.as_ref().map(|s| s.to_str().ok()) == Some(Some(&name)) {
        return;
    }
    inst.last_status_icon = CString::new(name.as_bytes()).ok();
    if let Some(cb) = inst.status_icon_changed_callback {
        cb(instance, icon_name, inst.status_icon_changed_user_data);
    }
}

#[no_mangle]
pub extern "C" fn typio_instance_clear_status_icon(instance: *mut TypioInstance) {
    if instance.is_null() {
        return;
    }
    let inst = unsafe { &mut *instance };
    inst.last_status_icon = None;
}

#[no_mangle]
pub extern "C" fn typio_instance_get_last_status_icon(instance: *mut TypioInstance) -> *const c_char {
    if instance.is_null() {
        return ptr::null();
    }
    unsafe { (*instance).last_status_icon.as_ref().map(|s| s.as_ptr()).unwrap_or(ptr::null()) }
}

#[no_mangle]
pub extern "C" fn typio_instance_set_mode_changed_callback(
    instance: *mut TypioInstance,
    callback: TypioModeChangedCallback,
    user_data: *mut c_void,
) {
    if instance.is_null() {
        return;
    }
    let inst = unsafe { &mut *instance };
    inst.mode_changed_callback = Some(callback);
    inst.mode_changed_user_data = user_data;
}

#[no_mangle]
pub extern "C" fn typio_instance_notify_mode(instance: *mut TypioInstance, mode: *const TypioEngineMode) {
    if instance.is_null() || mode.is_null() {
        return;
    }
    let inst = unsafe { &mut *instance };
    let mode_ref = unsafe { &*mode };

    if inst.has_mode && engine_mode_equal(&inst.last_mode, mode_ref) {
        return;
    }

    engine_mode_store(&mut inst.last_mode, mode_ref);
    inst.has_mode = true;

    if !mode_ref.icon_name.is_null() {
        let icon = unsafe { CStr::from_ptr(mode_ref.icon_name) }.to_string_lossy();
        inst.last_status_icon = CString::new(icon.as_bytes()).ok();
    }

    if let Some(cb) = inst.mode_changed_callback {
        cb(instance, &inst.last_mode, inst.mode_changed_user_data);
    }

    if let Some(cb) = inst.status_icon_changed_callback {
        if !mode_ref.icon_name.is_null() {
            cb(instance, mode_ref.icon_name, inst.status_icon_changed_user_data);
        }
    }
}

#[no_mangle]
pub extern "C" fn typio_instance_clear_mode(instance: *mut TypioInstance) {
    if instance.is_null() {
        return;
    }
    let inst = unsafe { &mut *instance };
    if !inst.has_mode {
        return;
    }
    if !inst.last_mode.mode_id.is_null() {
        unsafe { libc::free(inst.last_mode.mode_id as *mut c_void) };
    }
    if !inst.last_mode.display_label.is_null() {
        unsafe { libc::free(inst.last_mode.display_label as *mut c_void) };
    }
    if !inst.last_mode.icon_name.is_null() {
        unsafe { libc::free(inst.last_mode.icon_name as *mut c_void) };
    }
    inst.last_mode = TypioEngineMode {
        mode_class: TypioModeClass::TypioModeClassNative,
        mode_id: ptr::null(),
        display_label: ptr::null(),
        icon_name: ptr::null(),
    };
    inst.has_mode = false;
}

#[no_mangle]
pub extern "C" fn typio_instance_get_last_mode(instance: *mut TypioInstance) -> *const TypioEngineMode {
    if instance.is_null() {
        return ptr::null();
    }
    let inst = unsafe { &*instance };
    if !inst.has_mode {
        return ptr::null();
    }
    &inst.last_mode
}

#[no_mangle]
pub extern "C" fn typio_instance_notify_engine_changed(instance: *mut TypioInstance, engine: *const TypioEngineInfo) {
    if instance.is_null() {
        return;
    }
    let inst = unsafe { &*instance };
    if let Some(cb) = inst.engine_changed_callback {
        cb(instance, engine, inst.engine_changed_user_data);
    }
}

#[no_mangle]
pub extern "C" fn typio_instance_notify_voice_engine_changed(instance: *mut TypioInstance, engine: *const TypioEngineInfo) {
    if instance.is_null() {
        return;
    }
    let inst = unsafe { &*instance };
    if let Some(cb) = inst.voice_engine_changed_callback {
        cb(instance, engine, inst.voice_engine_changed_user_data);
    }
}

//! Callback registration, capabilities, user data, and properties

use super::{TypioCommitCallback, TypioCompositionCallback, TypioInputContext};
use std::ffi::{c_char, c_void, CStr};
use std::ptr;

#[no_mangle]
pub extern "C" fn typio_input_context_set_commit_callback(
    ctx: *mut TypioInputContext,
    cb: Option<TypioCommitCallback>,
    user_data: *mut c_void
) {
    if ctx.is_null() { return; }
    unsafe {
        (*ctx).commit_callback = cb;
        (*ctx).commit_user_data = user_data;
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_set_composition_callback(
    ctx: *mut TypioInputContext,
    cb: Option<TypioCompositionCallback>,
    user_data: *mut c_void
) {
    if ctx.is_null() { return; }
    unsafe {
        (*ctx).composition_callback = cb;
        (*ctx).composition_user_data = user_data;
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_set_capabilities(ctx: *mut TypioInputContext, caps: u32) {
    if ctx.is_null() { return; }
    unsafe { (*ctx).capabilities = caps };
}

#[no_mangle]
pub extern "C" fn typio_input_context_get_capabilities(ctx: *mut TypioInputContext) -> u32 {
    if ctx.is_null() { return 0; }
    unsafe { (*ctx).capabilities }
}

#[no_mangle]
pub extern "C" fn typio_input_context_set_user_data(ctx: *mut TypioInputContext, data: *mut c_void) {
    if ctx.is_null() { return; }
    unsafe { (*ctx).user_data = data };
}

#[no_mangle]
pub extern "C" fn typio_input_context_get_user_data(ctx: *mut TypioInputContext) -> *mut c_void {
    if ctx.is_null() { return ptr::null_mut(); }
    unsafe { (*ctx).user_data }
}

#[no_mangle]
pub extern "C" fn typio_input_context_set_property(
    ctx: *mut TypioInputContext,
    key: *const c_char,
    value: *mut c_void,
    free_func: Option<extern "C" fn(*mut c_void)>
) {
    if ctx.is_null() || key.is_null() { return; }
    let ctx_ref = unsafe { &mut *ctx };
    let key_str = unsafe { CStr::from_ptr(key) }.to_string_lossy().into_owned();

    if let Some(prop) = ctx_ref.properties.iter_mut().find(|p| p.key == key_str) {
        if let Some(ff) = prop.free_func {
            if !prop.value.is_null() {
                ff(prop.value);
            }
        }
        prop.value = value;
        prop.free_func = free_func;
    } else {
        ctx_ref.properties.push(super::PropertyEntry {
            key: key_str,
            value,
            free_func,
        });
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_get_property(
    ctx: *mut TypioInputContext,
    key: *const c_char
) -> *mut c_void {
    if ctx.is_null() || key.is_null() { return ptr::null_mut(); }
    let ctx_ref = unsafe { &mut *ctx };
    let key_str = unsafe { CStr::from_ptr(key) }.to_string_lossy();

    if let Some(prop) = ctx_ref.properties.iter().find(|p| p.key == key_str) {
        prop.value
    } else {
        ptr::null_mut()
    }
}

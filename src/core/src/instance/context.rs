//! Input context lifecycle management

use super::TypioInstance;
use crate::input_context;
use std::ptr;

#[no_mangle]
pub extern "C" fn typio_instance_get_engine_manager(instance: *mut TypioInstance) -> *mut crate::engine_manager::TypioEngineManager {
    if instance.is_null() {
        return ptr::null_mut();
    }
    unsafe { (*instance).engine_manager }
}

#[no_mangle]
pub extern "C" fn typio_instance_create_context(instance: *mut TypioInstance) -> *mut input_context::TypioInputContext {
    if instance.is_null() {
        return ptr::null_mut();
    }
    let inst = unsafe { &mut *instance };
    let ctx = input_context::typio_input_context_new(instance);
    if ctx.is_null() {
        return ptr::null_mut();
    }
    inst.contexts.push(ctx);
    ctx
}

#[no_mangle]
pub extern "C" fn typio_instance_destroy_context(instance: *mut TypioInstance, ctx: *mut input_context::TypioInputContext) {
    if instance.is_null() || ctx.is_null() {
        return;
    }
    let inst = unsafe { &mut *instance };
    inst.contexts.retain(|&c| c != ctx);
    if inst.focused_context == ctx {
        inst.focused_context = ptr::null_mut();
    }
    input_context::typio_input_context_free(ctx);
}

#[no_mangle]
pub extern "C" fn typio_instance_get_focused_context(instance: *mut TypioInstance) -> *mut input_context::TypioInputContext {
    if instance.is_null() {
        return ptr::null_mut();
    }
    unsafe { (*instance).focused_context }
}

#[no_mangle]
pub extern "C" fn typio_instance_set_focused_context(instance: *mut TypioInstance, ctx: *mut input_context::TypioInputContext) {
    if instance.is_null() {
        return;
    }
    unsafe { (*instance).focused_context = ctx; }
}

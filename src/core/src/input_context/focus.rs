//! Focus management and key event forwarding

use super::TypioInputContext;
use crate::engine::{
    _typio_engine_base_focus_in, _typio_engine_base_focus_out, _typio_engine_base_reset,
    _typio_engine_keyboard_process_key,
};
use crate::engine_manager::typio_engine_manager_get_active;
use crate::instance::{typio_instance_get_engine_manager, typio_instance_set_focused_context};
use crate::TypioKeyEvent;
use crate::types::*;
use std::ptr;

#[no_mangle]
pub extern "C" fn typio_input_context_focus_in(ctx: *mut TypioInputContext) {
    if ctx.is_null() { return; }
    let ctx_ref = unsafe { &mut *ctx };

    if ctx_ref.focused { return; }
    ctx_ref.focused = true;

    typio_instance_set_focused_context(ctx_ref.instance, ctx);
    let manager = typio_instance_get_engine_manager(ctx_ref.instance);
    let engine = typio_engine_manager_get_active(manager);
    if !engine.is_null() {
        _typio_engine_base_focus_in(engine, ctx);
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_focus_out(ctx: *mut TypioInputContext) {
    if ctx.is_null() { return; }
    let ctx_ref = unsafe { &mut *ctx };

    if !ctx_ref.focused { return; }

    let manager = typio_instance_get_engine_manager(ctx_ref.instance);
    let engine = typio_engine_manager_get_active(manager);
    if !engine.is_null() {
        _typio_engine_base_focus_out(engine, ctx);
    }
    typio_instance_set_focused_context(ctx_ref.instance, ptr::null_mut());

    ctx_ref.focused = false;
}

#[no_mangle]
pub extern "C" fn typio_input_context_is_focused(ctx: *mut TypioInputContext) -> bool {
    if ctx.is_null() { return false; }
    unsafe { (*ctx).focused }
}

#[no_mangle]
pub extern "C" fn typio_input_context_reset(ctx: *mut TypioInputContext) {
    if ctx.is_null() { return; }

    super::typio_input_context_clear_preedit(ctx);
    super::typio_input_context_clear_candidates(ctx);

    unsafe {
        let manager = typio_instance_get_engine_manager((*ctx).instance);
        let engine = typio_engine_manager_get_active(manager);
        if !engine.is_null() {
            _typio_engine_base_reset(engine, ctx);
        }
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_process_key(ctx: *mut TypioInputContext, event: *const TypioKeyEvent) -> bool {
    if ctx.is_null() || event.is_null() { return false; }

    let result = unsafe {
        let manager = typio_instance_get_engine_manager((*ctx).instance);
        let engine = typio_engine_manager_get_active(manager);
        if engine.is_null() {
            TypioKeyProcessResult::TypioKeyNotHandled
        } else {
            _typio_engine_keyboard_process_key(engine, ctx, event)
        }
    };

    result != TypioKeyProcessResult::TypioKeyNotHandled
}

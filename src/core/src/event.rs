//! Event handling — Rust implementation of event.c

use crate::types::*;
use std::ffi::{c_int, c_void};
use std::ptr;

/* Key symbol definitions (XKB compatible) */
pub const TYPIO_KEY_BackSpace: u32 = 0xff08;
pub const TYPIO_KEY_Tab: u32 = 0xff09;
pub const TYPIO_KEY_Return: u32 = 0xff0d;
pub const TYPIO_KEY_KP_Enter: u32 = 0xff8d;
pub const TYPIO_KEY_Escape: u32 = 0xff1b;
pub const TYPIO_KEY_Delete: u32 = 0xffff;
pub const TYPIO_KEY_Home: u32 = 0xff50;
pub const TYPIO_KEY_Left: u32 = 0xff51;
pub const TYPIO_KEY_Up: u32 = 0xff52;
pub const TYPIO_KEY_Right: u32 = 0xff53;
pub const TYPIO_KEY_Down: u32 = 0xff54;
pub const TYPIO_KEY_Page_Up: u32 = 0xff55;
pub const TYPIO_KEY_Page_Down: u32 = 0xff56;
pub const TYPIO_KEY_End: u32 = 0xff57;
pub const TYPIO_KEY_space: u32 = 0x0020;

pub const TYPIO_KEY_Shift_L: u32 = 0xffe1;
pub const TYPIO_KEY_Shift_R: u32 = 0xffe2;
pub const TYPIO_KEY_Control_L: u32 = 0xffe3;
pub const TYPIO_KEY_Control_R: u32 = 0xffe4;
pub const TYPIO_KEY_Alt_L: u32 = 0xffe9;
pub const TYPIO_KEY_Alt_R: u32 = 0xffea;
pub const TYPIO_KEY_Super_L: u32 = 0xffeb;
pub const TYPIO_KEY_Super_R: u32 = 0xffec;

/* Function keys */
pub const TYPIO_KEY_F1: u32 = 0xffbe;
pub const TYPIO_KEY_F2: u32 = 0xffbf;
pub const TYPIO_KEY_F3: u32 = 0xffc0;
pub const TYPIO_KEY_F4: u32 = 0xffc1;
pub const TYPIO_KEY_F5: u32 = 0xffc2;
pub const TYPIO_KEY_F6: u32 = 0xffc3;
pub const TYPIO_KEY_F7: u32 = 0xffc4;
pub const TYPIO_KEY_F8: u32 = 0xffc5;
pub const TYPIO_KEY_F9: u32 = 0xffc6;
pub const TYPIO_KEY_F10: u32 = 0xffc7;
pub const TYPIO_KEY_F11: u32 = 0xffc8;
pub const TYPIO_KEY_F12: u32 = 0xffc9;

#[no_mangle]
pub extern "C" fn typio_key_event_new(
    type_: TypioEventType,
    keycode: u32,
    keysym: u32,
    modifiers: u32,
) -> *mut TypioKeyEvent {
    let event = Box::new(TypioKeyEvent {
        type_,
        keycode,
        keysym,
        modifiers,
        unicode: 0,
        time: 0,
        is_repeat: false,
    });
    Box::into_raw(event)
}

#[no_mangle]
pub extern "C" fn typio_key_event_free(event: *mut TypioKeyEvent) {
    if !event.is_null() {
        unsafe { drop(Box::from_raw(event)) };
    }
}

#[no_mangle]
pub extern "C" fn typio_key_event_is_press(event: *const TypioKeyEvent) -> bool {
    if event.is_null() {
        return false;
    }
    unsafe { (*event).type_ == TypioEventType::TypioEventKeyPress }
}

#[no_mangle]
pub extern "C" fn typio_key_event_is_release(event: *const TypioKeyEvent) -> bool {
    if event.is_null() {
        return false;
    }
    unsafe { (*event).type_ == TypioEventType::TypioEventKeyRelease }
}

#[no_mangle]
pub extern "C" fn typio_key_event_has_modifier(event: *const TypioKeyEvent, mod_: TypioModifier) -> bool {
    if event.is_null() {
        return false;
    }
    unsafe { ((*event).modifiers & (mod_ as u32)) != 0 }
}

#[no_mangle]
pub extern "C" fn typio_key_event_is_modifier_only(event: *const TypioKeyEvent) -> bool {
    if event.is_null() {
        return false;
    }
    let keysym = unsafe { (*event).keysym };
    matches!(keysym,
        TYPIO_KEY_Shift_L | TYPIO_KEY_Shift_R |
        TYPIO_KEY_Control_L | TYPIO_KEY_Control_R |
        TYPIO_KEY_Alt_L | TYPIO_KEY_Alt_R |
        TYPIO_KEY_Super_L | TYPIO_KEY_Super_R
    )
}

#[no_mangle]
pub extern "C" fn typio_key_event_get_unicode(event: *const TypioKeyEvent) -> u32 {
    if event.is_null() {
        return 0;
    }
    let ev = unsafe { &*event };
    if ev.unicode != 0 {
        return ev.unicode;
    }
    if ev.keysym >= 0x20 && ev.keysym <= 0x7e {
        return ev.keysym;
    }
    0
}

#[no_mangle]
pub extern "C" fn typio_key_event_is_backspace(event: *const TypioKeyEvent) -> bool {
    !event.is_null() && unsafe { (*event).keysym == TYPIO_KEY_BackSpace }
}

#[no_mangle]
pub extern "C" fn typio_key_event_is_enter(event: *const TypioKeyEvent) -> bool {
    !event.is_null() && unsafe { (*event).keysym == TYPIO_KEY_Return }
}

#[no_mangle]
pub extern "C" fn typio_key_event_is_escape(event: *const TypioKeyEvent) -> bool {
    !event.is_null() && unsafe { (*event).keysym == TYPIO_KEY_Escape }
}

#[no_mangle]
pub extern "C" fn typio_key_event_is_space(event: *const TypioKeyEvent) -> bool {
    !event.is_null() && unsafe { (*event).keysym == TYPIO_KEY_space }
}

#[no_mangle]
pub extern "C" fn typio_key_event_is_tab(event: *const TypioKeyEvent) -> bool {
    !event.is_null() && unsafe { (*event).keysym == TYPIO_KEY_Tab }
}

#[no_mangle]
pub extern "C" fn typio_key_event_is_arrow(event: *const TypioKeyEvent) -> bool {
    if event.is_null() {
        return false;
    }
    let keysym = unsafe { (*event).keysym };
    matches!(keysym,
        TYPIO_KEY_Left | TYPIO_KEY_Right | TYPIO_KEY_Up | TYPIO_KEY_Down
    )
}

#[no_mangle]
pub extern "C" fn typio_key_event_is_page(event: *const TypioKeyEvent) -> bool {
    if event.is_null() {
        return false;
    }
    let keysym = unsafe { (*event).keysym };
    matches!(keysym,
        TYPIO_KEY_Page_Up | TYPIO_KEY_Page_Down
    )
}

#[no_mangle]
pub extern "C" fn typio_voice_event_new(type_: TypioEventType) -> *mut TypioVoiceEvent {
    let event = Box::new(TypioVoiceEvent {
        type_,
        audio_data: ptr::null(),
        audio_size: 0,
        sample_rate: 0,
        channels: 0,
        bits_per_sample: 0,
    });
    Box::into_raw(event)
}

#[no_mangle]
pub extern "C" fn typio_voice_event_free(event: *mut TypioVoiceEvent) {
    if !event.is_null() {
        unsafe { drop(Box::from_raw(event)) };
    }
}

#[no_mangle]
pub extern "C" fn typio_voice_event_set_data(
    event: *mut TypioVoiceEvent,
    data: *const c_void,
    size: usize,
    sample_rate: c_int,
    channels: c_int,
    bits_per_sample: c_int,
) {
    if event.is_null() {
        return;
    }
    let ev = unsafe { &mut *event };
    ev.audio_data = data;
    ev.audio_size = size;
    ev.sample_rate = sample_rate;
    ev.channels = channels;
    ev.bits_per_sample = bits_per_sample;
}

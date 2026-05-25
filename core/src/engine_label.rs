//! Engine label helpers — Rust implementation of engine_label.c

use std::ffi::{c_char, CStr};

use crate::types::TypioEngineInfo;

#[no_mangle]
pub extern "C" fn typio_engine_label_fallback(engine_name: *const c_char) -> *const c_char {
    if engine_name.is_null() {
        return c"".as_ptr();
    }
    let name = unsafe { CStr::from_ptr(engine_name) }.to_string_lossy();
    match name.as_ref() {
        "basic" => c"Basic".as_ptr(),
        "rime" => c"Rime".as_ptr(),
        "mozc" => c"Mozc".as_ptr(),
        "whisper" => c"Whisper".as_ptr(),
        "sherpa-onnx" => c"Sherpa ONNX".as_ptr(),
        _ => engine_name,
    }
}

#[no_mangle]
pub extern "C" fn typio_engine_label_from_info(info: *const TypioEngineInfo) -> *const c_char {
    if info.is_null() {
        return typio_engine_label_fallback(std::ptr::null());
    }
    let info_ref = unsafe { &*info };
    if !info_ref.display_name.is_null() {
        let display = unsafe { CStr::from_ptr(info_ref.display_name) }.to_string_lossy();
        if !display.is_empty() {
            return info_ref.display_name;
        }
    }
    typio_engine_label_fallback(info_ref.name)
}

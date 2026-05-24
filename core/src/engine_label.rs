//! Engine label helpers — Rust implementation of engine_label.c

use std::ffi::{c_char, CStr};

use crate::types::TypioEngineInfo;

#[no_mangle]
pub extern "C" fn typio_engine_label_fallback(engine_name: *const c_char) -> *const c_char {
    if engine_name.is_null() {
        return "".as_ptr() as *const c_char;
    }
    let name = unsafe { CStr::from_ptr(engine_name) }.to_string_lossy();
    match name.as_ref() {
        "basic" => "Basic\0".as_ptr() as *const c_char,
        "rime" => "Rime\0".as_ptr() as *const c_char,
        "mozc" => "Mozc\0".as_ptr() as *const c_char,
        "whisper" => "Whisper\0".as_ptr() as *const c_char,
        "sherpa-onnx" => "Sherpa ONNX\0".as_ptr() as *const c_char,
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

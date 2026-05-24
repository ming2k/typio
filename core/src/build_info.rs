//! Build information — Rust implementation of build_info.c

use std::ffi::{c_char, CString};
use std::sync::LazyLock;

static VERSION: LazyLock<CString> = LazyLock::new(|| {
    let v = option_env!("TYPIO_VERSION").unwrap_or(env!("CARGO_PKG_VERSION"));
    CString::new(v).unwrap()
});

static SOURCE_LABEL: LazyLock<CString> = LazyLock::new(|| {
    let v = option_env!("TYPIO_BUILD_SOURCE_LABEL").unwrap_or("source");
    CString::new(v).unwrap()
});

static DISPLAY_STRING: LazyLock<CString> = LazyLock::new(|| {
    CString::new(format!(
        "Typio {} ({})",
        VERSION.to_str().unwrap(),
        SOURCE_LABEL.to_str().unwrap()
    )).unwrap()
});

#[no_mangle]
pub extern "C" fn typio_build_version() -> *const c_char {
    VERSION.as_ptr()
}

#[no_mangle]
pub extern "C" fn typio_build_source_label() -> *const c_char {
    SOURCE_LABEL.as_ptr()
}

#[no_mangle]
pub extern "C" fn typio_build_display_string() -> *const c_char {
    DISPLAY_STRING.as_ptr()
}

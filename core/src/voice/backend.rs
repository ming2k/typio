//! FFI declarations for the C voice backend plugins.
//!
//! These thin C wrappers (voice_sherpa.c / voice_whisper.c) remain compiled
//! as object files and linked into libtypio-core.so.  The Rust voice module
//! calls them through raw `extern "C"` blocks.  They are NOT business logic —
//! they are pure FFI glue to sherpa-onnx-c-api and whisper.cpp.

use std::ffi::c_char;

/// Opaque C backend handle — vtable-based.
#[repr(C)]
pub struct TypioVoiceBackend {
    pub ops: *const TypioVoiceBackendOps,
}

#[repr(C)]
pub struct TypioVoiceBackendOps {
    pub process:
        Option<extern "C" fn(*mut TypioVoiceBackend, *const f32, usize) -> *mut c_char>,
    pub destroy: Option<extern "C" fn(*mut TypioVoiceBackend)>,
}

/// Run inference through the backend vtable.
///
/// # Safety
/// `b` must be null or a valid `TypioVoiceBackend` with a live `ops` vtable;
/// `samples` must point to `n_samples` readable `f32`s.
pub unsafe fn typio_voice_backend_process(
    b: *mut TypioVoiceBackend,
    samples: *const f32,
    n_samples: usize,
) -> *mut c_char {
    if b.is_null() || (*b).ops.is_null() {
        return std::ptr::null_mut();
    }
    let ops = &*(*b).ops;
    if let Some(proc) = ops.process {
        proc(b, samples, n_samples)
    } else {
        std::ptr::null_mut()
    }
}

/// Destroy the backend through the vtable.
///
/// # Safety
/// `b` must be null or a valid `TypioVoiceBackend` with a live `ops` vtable;
/// it must not be used after this call.
pub unsafe fn typio_voice_backend_destroy(b: *mut TypioVoiceBackend) {
    if b.is_null() || (*b).ops.is_null() {
        return;
    }
    let ops = &*(*b).ops;
    if let Some(dest) = ops.destroy {
        dest(b);
    }
}

extern "C" {
    #[cfg(feature = "have_sherpa_onnx")]
    pub fn typio_voice_backend_sherpa_new(
        data_dir: *const c_char,
        language: *const c_char,
        model: *const c_char,
    ) -> *mut TypioVoiceBackend;

    #[cfg(feature = "have_whisper")]
    pub fn typio_voice_backend_whisper_new(
        data_dir: *const c_char,
        language: *const c_char,
        model: *const c_char,
    ) -> *mut TypioVoiceBackend;
}

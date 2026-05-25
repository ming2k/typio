//! Reference-counted proxy around a voice backend.
//!
//! Replaces `voice_proxy.c`.  Rust's `Arc` and `Mutex` provide the same
//! safety properties (deferred destruction, safe concurrent reload) without
//! a manual refcount queue.

use crate::voice::backend::{typio_voice_backend_destroy, typio_voice_backend_process, TypioVoiceBackend};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

/// Safe wrapper around a raw C backend pointer.
pub struct CBackend {
    ptr: *mut TypioVoiceBackend,
}

// SAFETY: The backend is created once and only accessed through Arc.
unsafe impl Send for CBackend {}
unsafe impl Sync for CBackend {}

impl CBackend {
    /// # Safety
    /// `ptr` must be a valid, non-null `TypioVoiceBackend` allocated by the
    /// C plugin constructors.
    pub unsafe fn new(ptr: *mut TypioVoiceBackend) -> Option<Self> {
        if ptr.is_null() {
            None
        } else {
            Some(Self { ptr })
        }
    }

    pub fn process(&self, samples: &[f32]) -> Option<String> {
        unsafe {
            let result = typio_voice_backend_process(self.ptr, samples.as_ptr(), samples.len());
            if result.is_null() {
                None
            } else {
                let s = std::ffi::CStr::from_ptr(result)
                    .to_string_lossy()
                    .into_owned();
                libc::free(result as *mut libc::c_void);
                Some(s)
            }
        }
    }
}

impl Drop for CBackend {
    fn drop(&mut self) {
        unsafe {
            typio_voice_backend_destroy(self.ptr);
        }
    }
}

/// Thread-safe proxy that holds an optional backend.
///
/// - `process()` snapshots the backend under the mutex, then drops the lock
///   before running inference.
/// - `reload_begin()` / `reload_end()` coordinate asynchronous replacement.
/// - `destroy()` marks the proxy as dead; future `process()` calls return None.
pub struct VoiceBackendProxy {
    backend: Mutex<Option<Arc<CBackend>>>,
    reload_running: AtomicBool,
    destroy_pending: AtomicBool,
}

impl Default for VoiceBackendProxy {
    fn default() -> Self {
        Self::new()
    }
}

impl VoiceBackendProxy {
    pub fn new() -> Self {
        Self {
            backend: Mutex::new(None),
            reload_running: AtomicBool::new(false),
            destroy_pending: AtomicBool::new(false),
        }
    }

    pub fn is_ready(&self) -> bool {
        !self.destroy_pending.load(Ordering::SeqCst)
            && self.backend.lock().unwrap().is_some()
    }

    pub fn clear_impl(&self) {
        let mut guard = self.backend.lock().unwrap();
        if !self.destroy_pending.load(Ordering::SeqCst) {
            *guard = None;
        }
    }

    pub fn set_impl(&self, backend: Option<Arc<CBackend>>) {
        let mut guard = self.backend.lock().unwrap();
        if !self.destroy_pending.load(Ordering::SeqCst) {
            *guard = backend;
        }
    }

    /// Try to claim the reload slot.  Returns `true` if no reload is
    /// currently in progress.
    pub fn reload_begin(&self) -> bool {
        self.reload_running
            .compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst)
            .is_ok()
    }

    pub fn reload_end(&self, backend: Option<Arc<CBackend>>) {
        self.reload_running.store(false, Ordering::SeqCst);
        if self.destroy_pending.load(Ordering::SeqCst) {
            drop(backend);
        } else {
            *self.backend.lock().unwrap() = backend;
        }
    }

    pub fn process(&self, samples: &[f32]) -> Option<String> {
        if self.destroy_pending.load(Ordering::SeqCst) {
            return None;
        }
        let backend = self.backend.lock().unwrap().clone();
        backend.and_then(|b| b.process(samples))
    }

    pub fn destroy(&self) {
        self.destroy_pending.store(true, Ordering::SeqCst);
        *self.backend.lock().unwrap() = None;
    }
}

/* ── C ABI compatibility layer ─────────────────────────────────────────── */

/// Opaque handle exposed to C.
#[repr(C)]
pub struct TypioVoiceProxy {
    _opaque: [u8; 0],
}

#[no_mangle]
pub extern "C" fn typio_voice_proxy_new(initial_impl: *mut TypioVoiceBackend) -> *mut TypioVoiceProxy {
    let proxy = Arc::new(VoiceBackendProxy::new());
    if !initial_impl.is_null() {
        if let Some(backend) = unsafe { CBackend::new(initial_impl) } {
            proxy.set_impl(Some(Arc::new(backend)));
        }
    }
    Arc::into_raw(proxy) as *mut TypioVoiceProxy
}

#[no_mangle]
pub extern "C" fn typio_voice_proxy_as_backend(_proxy: *mut TypioVoiceProxy) -> *mut TypioVoiceBackend {
    // In the new architecture the proxy is used directly by Rust adapters,
    // so this function is a no-op placeholder for transitional compatibility.
    std::ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn typio_voice_proxy_is_ready(proxy: *mut TypioVoiceProxy) -> bool {
    if proxy.is_null() {
        return false;
    }
    let proxy = unsafe { &*(proxy as *const VoiceBackendProxy) };
    proxy.is_ready()
}

#[no_mangle]
pub extern "C" fn typio_voice_proxy_destroy(proxy: *mut TypioVoiceProxy) {
    if proxy.is_null() {
        return;
    }
    let proxy = unsafe { Arc::from_raw(proxy as *const VoiceBackendProxy) };
    proxy.destroy();
    // Arc drops here; if no other references exist, memory is freed.
}

#[no_mangle]
pub extern "C" fn typio_voice_proxy_clear_impl(proxy: *mut TypioVoiceProxy) {
    if proxy.is_null() {
        return;
    }
    let proxy = unsafe { &*(proxy as *const VoiceBackendProxy) };
    proxy.clear_impl();
}

#[no_mangle]
pub extern "C" fn typio_voice_proxy_set_impl(
    proxy: *mut TypioVoiceProxy,
    new_impl: *mut TypioVoiceBackend,
) {
    if proxy.is_null() {
        if !new_impl.is_null() {
            unsafe { typio_voice_backend_destroy(new_impl) };
        }
        return;
    }
    let proxy = unsafe { &*(proxy as *const VoiceBackendProxy) };
    let backend = if new_impl.is_null() {
        None
    } else {
        unsafe { CBackend::new(new_impl).map(Arc::new) }
    };
    proxy.set_impl(backend);
}

#[no_mangle]
pub extern "C" fn typio_voice_proxy_reload_begin(proxy: *mut TypioVoiceProxy) -> bool {
    if proxy.is_null() {
        return false;
    }
    let proxy = unsafe { &*(proxy as *const VoiceBackendProxy) };
    proxy.reload_begin()
}

#[no_mangle]
pub extern "C" fn typio_voice_proxy_reload_end(
    proxy: *mut TypioVoiceProxy,
    new_impl: *mut TypioVoiceBackend,
) {
    if proxy.is_null() {
        if !new_impl.is_null() {
            unsafe { typio_voice_backend_destroy(new_impl) };
        }
        return;
    }
    let proxy = unsafe { &*(proxy as *const VoiceBackendProxy) };
    let backend = if new_impl.is_null() {
        None
    } else {
        unsafe { CBackend::new(new_impl).map(Arc::new) }
    };
    proxy.reload_end(backend);
}

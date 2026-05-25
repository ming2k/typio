//! Voice session — state machine, threading, audio buffering.
//!
//! Replaces `voice_session.c`.  All business logic is now Rust-native;
//! only the backend inference calls cross the FFI boundary.

use crate::types::{TypioEngine, TypioLogLevel, TypioVoiceSession};
use crate::instance::TypioInstance;
use crate::voice::types::{TypioVoiceSessionEvent, TypioVoiceSessionEventCallback, TypioVoiceSessionEventType, VoiceState};
use nix::sys::eventfd::{EventFd, EfdFlags};
use nix::sys::timerfd::{ClockId, TimerFd, TimerFlags};
use std::ffi::{c_char, c_void, CStr, CString};
use std::os::fd::AsRawFd;
use std::sync::atomic::{AtomicBool, AtomicPtr, AtomicU32, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;

const INITIAL_BUFFER_SAMPLES: usize = 16000 * 30; // 30 seconds at 16kHz
const SAMPLE_RATE: usize = 16000;
const TRIM_THRESHOLD: f32 = 0.003f32;
const TRIM_PADDING_SAMPLES: usize = SAMPLE_RATE / 10;
const MIN_ACTIVE_SAMPLES: usize = SAMPLE_RATE / 5;

pub struct VoiceSession {
    _instance: AtomicPtr<TypioInstance>,
    voice_engine: AtomicPtr<TypioEngine>,
    audio_source: AtomicPtr<TypioAudioSource>,
    state: Mutex<VoiceState>,
    reload_pending: AtomicBool,
    audio_buffer: Mutex<Vec<f32>>,
    infer_handle: Mutex<Option<thread::JoinHandle<Option<String>>>>,
    event_fd: Mutex<Option<nix::sys::eventfd::EventFd>>,
    _result: Mutex<Option<String>>,
    idle_timer_fd: Mutex<Option<TimerFd>>,
    idle_shutdown_tx: Mutex<Option<std::sync::mpsc::Sender<()>>>,
    idle_handle: Mutex<Option<thread::JoinHandle<()>>>,
    idle_timeout_ms: AtomicU32,
    idle_armed: AtomicBool,
    callback: Mutex<Option<TypioVoiceSessionEventCallback>>,
    callback_user_data: AtomicPtr<c_void>,
    auto_start_on_load: AtomicBool,
}

// SAFETY: VoiceSession is designed to be shared across threads.
// The raw pointers inside are only accessed under Mutex or AtomicPtr.
unsafe impl Send for VoiceSession {}
unsafe impl Sync for VoiceSession {}

/// Audio source abstraction (injected by frontend).
#[repr(C)]
pub struct TypioAudioSource {
    pub ops: *const TypioAudioSourceOps,
}

#[repr(C)]
pub struct TypioAudioSourceOps {
    pub start: Option<extern "C" fn(*mut TypioAudioSource) -> bool>,
    pub stop: Option<extern "C" fn(*mut TypioAudioSource)>,
    pub free: Option<extern "C" fn(*mut TypioAudioSource)>,
    pub get_fd: Option<extern "C" fn(*mut TypioAudioSource) -> i32>,
    pub dispatch: Option<extern "C" fn(*mut TypioAudioSource)>,
}

impl VoiceSession {
    fn fire_event(&self, event: TypioVoiceSessionEvent) {
        if let Some(cb) = *self.callback.lock().unwrap() {
            cb(&event, self.callback_user_data.load(Ordering::SeqCst));
        }
        // Free heap-allocated text if present.
        if !event.text.is_null() {
            unsafe { libc::free(event.text as *mut c_void) };
        }
    }

    fn fire_state_change(&self, state: VoiceState) {
        let event = TypioVoiceSessionEvent {
            type_: TypioVoiceSessionEventType::StateChange,
            state,
            text: std::ptr::null_mut(),
            error: std::ptr::null(),
        };
        self.fire_event(event);
    }
}

/* ── Helpers ───────────────────────────────────────────────────────────── */

fn engine_has_voice(engine: *const TypioEngine) -> bool {
    unsafe {
        if engine.is_null() {
            return false;
        }
        let e = match crate::engine::rust_adapter::get_engine(engine as *mut TypioEngine) {
            Some(e) => e,
            None => return false,
        };
        let voice = match e.as_voice() {
            Some(v) => v,
            None => return false,
        };
        voice.is_ready()
    }
}

fn prepare_audio(audio: &mut Vec<f32>) -> Vec<f32> {
    if audio.is_empty() {
        return Vec::new();
    }
    let peak = audio.iter().map(|v| v.abs()).fold(0.0f32, f32::max);
    let abs_sum: f64 = audio.iter().map(|v| v.abs() as f64).sum();
    let mean_abs = abs_sum / audio.len() as f64;
    crate::voice::log_msg(
        TypioLogLevel::TypioLogInfo,
        &format!(
            "Voice audio level: duration={:.2}s peak={:.5} mean_abs={:.5}",
            audio.len() as f64 / SAMPLE_RATE as f64,
            peak,
            mean_abs,
        ),
    );

    let mut first_active = audio.len();
    let mut last_active = 0usize;
    for (i, &sample) in audio.iter().enumerate() {
        if sample.abs() >= TRIM_THRESHOLD {
            if first_active == audio.len() {
                first_active = i;
            }
            last_active = i;
        }
    }

    if first_active == audio.len()
        || last_active <= first_active
        || last_active - first_active + 1 < MIN_ACTIVE_SAMPLES
    {
        crate::voice::log_msg(
            TypioLogLevel::TypioLogWarning,
            "Voice audio discarded: no usable microphone signal detected",
        );
        return Vec::new();
    }

    let start = first_active.saturating_sub(TRIM_PADDING_SAMPLES);
    let end = (last_active + TRIM_PADDING_SAMPLES + 1).min(audio.len());
    if start > 0 || end < audio.len() {
        crate::voice::log_msg(
            TypioLogLevel::TypioLogInfo,
            &format!(
                "Voice audio trimmed: {:.2}s -> {:.2}s",
                audio.len() as f64 / SAMPLE_RATE as f64,
                (end - start) as f64 / SAMPLE_RATE as f64,
            ),
        );
        audio[start..end].to_vec()
    } else {
        std::mem::take(audio)
    }
}

fn try_begin_recording(session: &VoiceSession) -> bool {
    let source = session.audio_source.load(Ordering::SeqCst);
    if source.is_null() {
        return false;
    }
    unsafe {
        let ops = &*(*source).ops;
        if let Some(start_fn) = ops.start {
            if !start_fn(source) {
                return false;
            }
        } else {
            return false;
        }
    }
    true
}

/* ── Idle timeout thread ───────────────────────────────────────────────── */

fn idle_thread_main(session: Arc<VoiceSession>, rx: std::sync::mpsc::Receiver<()>) {
    loop {
        let timeout_ms = session.idle_timeout_ms.load(Ordering::SeqCst) as u64;
        let wait = if session.idle_armed.load(Ordering::SeqCst) && timeout_ms > 0 {
            std::time::Duration::from_millis(timeout_ms)
        } else {
            std::time::Duration::from_millis(100)
        };
        match rx.recv_timeout(wait) {
            Ok(()) => break,                    // shutdown requested
            Err(std::sync::mpsc::RecvTimeoutError::Disconnected) => break,
            Err(std::sync::mpsc::RecvTimeoutError::Timeout) => {
                if timeout_ms > 0
                    && session.idle_armed.load(Ordering::SeqCst)
                    && *session.state.lock().unwrap() == VoiceState::Idle
                {
                    let engine = session.voice_engine.load(Ordering::SeqCst);
                    if !engine.is_null() {
                        unsafe {
                            let base_ops = &*(*engine).base_ops;
                            if let Some(deactivate) = base_ops.deactivate {
                                deactivate(engine);
                            }
                        }
                        crate::voice::log_msg(
                            TypioLogLevel::TypioLogInfo,
                            &format!("Voice model unloaded after {} ms idle", timeout_ms),
                        );
                    }
                    session.idle_armed.store(false, Ordering::SeqCst);
                }
            }
        }
    }
}

/* ── C ABI ─────────────────────────────────────────────────────────────── */

#[no_mangle]
pub extern "C" fn typio_voice_session_new(instance: *mut TypioInstance) -> *mut TypioVoiceSession {
    if instance.is_null() {
        return std::ptr::null_mut();
    }
    let event_fd = match EventFd::from_value_and_flags(0, EfdFlags::EFD_CLOEXEC | EfdFlags::EFD_NONBLOCK) {
        Ok(efd) => efd,
        Err(_) => {
            crate::voice::log_msg(TypioLogLevel::TypioLogError, "Failed to create eventfd");
            return std::ptr::null_mut();
        }
    };
    let idle_timer_fd = match TimerFd::new(ClockId::CLOCK_MONOTONIC, TimerFlags::TFD_NONBLOCK | TimerFlags::TFD_CLOEXEC) {
        Ok(fd) => fd,
        Err(_) => {
            crate::voice::log_msg(TypioLogLevel::TypioLogError, "Failed to create timerfd");
            unsafe { let _ = libc::close(event_fd.as_raw_fd()); }
            return std::ptr::null_mut();
        }
    };

    let voice_engine = {
        let mgr = crate::instance::typio_instance_get_engine_manager(instance);
        crate::engine_manager::typio_engine_manager_get_active_voice(mgr)
    };
    let session = Arc::new(VoiceSession {
        _instance: AtomicPtr::new(instance),
        voice_engine: AtomicPtr::new(voice_engine),
        audio_source: AtomicPtr::new(std::ptr::null_mut()),
        state: Mutex::new(VoiceState::Idle),
        reload_pending: AtomicBool::new(false),
        audio_buffer: Mutex::new(Vec::with_capacity(INITIAL_BUFFER_SAMPLES)),
        infer_handle: Mutex::new(None),
        event_fd: Mutex::new(Some(event_fd)),
        _result: Mutex::new(None),
        idle_timer_fd: Mutex::new(Some(idle_timer_fd)),
        idle_shutdown_tx: Mutex::new(None),
        idle_handle: Mutex::new(None),
        idle_timeout_ms: AtomicU32::new(0),
        idle_armed: AtomicBool::new(false),
        callback: Mutex::new(None),
        callback_user_data: AtomicPtr::new(std::ptr::null_mut()),
        auto_start_on_load: AtomicBool::new(false),
    });

    // Spawn idle timeout thread.
    let (tx, rx) = std::sync::mpsc::channel();
    *session.idle_shutdown_tx.lock().unwrap() = Some(tx);
    let session_clone = Arc::clone(&session);
    let handle = thread::spawn(move || {
        idle_thread_main(session_clone, rx);
    });
    *session.idle_handle.lock().unwrap() = Some(handle);

    Arc::into_raw(session) as *mut TypioVoiceSession
}

#[no_mangle]
pub extern "C" fn typio_voice_session_free(session: *mut TypioVoiceSession) {
    if session.is_null() {
        return;
    }
    let session = unsafe { Arc::from_raw(session as *const VoiceSession) };
    // Stop audio source.
    let source = session.audio_source.load(Ordering::SeqCst);
    if !source.is_null() {
        unsafe {
            let ops = &*(*source).ops;
            if let Some(stop_fn) = ops.stop {
                stop_fn(source);
            }
            if let Some(free_fn) = ops.free {
                free_fn(source);
            }
        }
    }
    // Wait for inference thread if running.
    let mut infer = session.infer_handle.lock().unwrap();
    if let Some(handle) = infer.take() {
        let _ = handle.join();
    }
    // Shutdown idle thread.
    if let Some(tx) = session.idle_shutdown_tx.lock().unwrap().take() {
        let _ = tx.send(());
    }
    let mut idle = session.idle_handle.lock().unwrap();
    if let Some(handle) = idle.take() {
        let _ = handle.join();
    }
    let _ = session.event_fd.lock().unwrap().take();
    let _ = session.idle_timer_fd.lock().unwrap().take();
}

#[no_mangle]
pub extern "C" fn typio_voice_session_set_audio_source(
    session: *mut TypioVoiceSession,
    source: *mut TypioAudioSource,
) {
    if session.is_null() {
        return;
    }
    let session = unsafe { &*(session as *const VoiceSession) };
    session.audio_source.store(source, Ordering::SeqCst);
}

#[no_mangle]
pub extern "C" fn typio_voice_session_set_callback(
    session: *mut TypioVoiceSession,
    callback: TypioVoiceSessionEventCallback,
    user_data: *mut c_void,
) {
    if session.is_null() {
        return;
    }
    let session = unsafe { &*(session as *const VoiceSession) };
    *session.callback.lock().unwrap() = Some(callback);
    session.callback_user_data.store(user_data, Ordering::SeqCst);
}

#[no_mangle]
pub extern "C" fn typio_voice_session_start(session: *mut TypioVoiceSession) -> bool {
    if session.is_null() {
        return false;
    }
    let session = unsafe { &*(session as *const VoiceSession) };

    // Disarm idle timer.
    session.idle_armed.store(false, Ordering::SeqCst);

    let mut state = session.state.lock().unwrap();

    if *state == VoiceState::Loading {
        return true;
    }

    // Load the model on demand via focus_in. This is synchronous: when it
    // returns the model is ready (or load failed), so we proceed straight to
    // recording or report failure below — no intermediate "loading" state.
    unsafe {
        if !session.voice_engine.load(Ordering::SeqCst).is_null()
            && !(*session.voice_engine.load(Ordering::SeqCst)).base_ops.is_null()
            && !engine_has_voice(session.voice_engine.load(Ordering::SeqCst))
        {
            let base_ops = &*(*session.voice_engine.load(Ordering::SeqCst)).base_ops;
            if let Some(focus_in) = base_ops.focus_in {
                drop(state);
                focus_in(session.voice_engine.load(Ordering::SeqCst), std::ptr::null_mut());
                state = session.state.lock().unwrap();
            }
        }
    }

    if !engine_has_voice(session.voice_engine.load(Ordering::SeqCst))
        || *state != VoiceState::Idle
        || session.audio_source.load(Ordering::SeqCst).is_null()
    {
        drop(state);
        session.idle_armed.store(true, Ordering::SeqCst);
        return false;
    }

    session.audio_buffer.lock().unwrap().clear();
    drop(state);

    if !try_begin_recording(session) {
        session.idle_armed.store(true, Ordering::SeqCst);
        return false;
    }

    *session.state.lock().unwrap() = VoiceState::Recording;
    crate::voice::log_msg(TypioLogLevel::TypioLogInfo, "Voice recording started");
    session.fire_state_change(VoiceState::Recording);
    true
}

#[no_mangle]
pub extern "C" fn typio_voice_session_stop(session: *mut TypioVoiceSession) {
    if session.is_null() {
        return;
    }
    let session = unsafe { &*(session as *const VoiceSession) };

    let mut state = session.state.lock().unwrap();

    if *state == VoiceState::Loading {
        session.auto_start_on_load.store(false, Ordering::SeqCst);
        *state = VoiceState::Idle;
        drop(state);
        session.fire_state_change(VoiceState::Idle);
        return;
    }

    if *state != VoiceState::Recording {
        drop(state);
        return;
    }

    *state = VoiceState::Processing;
    let sample_count = session.audio_buffer.lock().unwrap().len();
    drop(state);

    let source = session.audio_source.load(Ordering::SeqCst);
    if !source.is_null() {
        unsafe {
            let ops = &*(*source).ops;
            if let Some(stop_fn) = ops.stop {
                stop_fn(source);
            }
        }
    }

    crate::voice::log_msg(
        TypioLogLevel::TypioLogInfo,
        &format!(
            "Voice recording stopped, starting inference ({} samples)",
            sample_count
        ),
    );
    session.fire_state_change(VoiceState::Processing);

    // Launch inference thread.
    let session_arc = unsafe { Arc::from_raw(session as *const VoiceSession) };
    // Leak the Arc back so the C side keeps ownership; the thread clones its own Arc.
    let _ = Arc::into_raw(session_arc.clone());

    let handle = thread::spawn(move || {
        let audio = {
            let mut buf = session_arc.audio_buffer.lock().unwrap();
            std::mem::take(&mut *buf)
        };
        let audio = prepare_audio(&mut audio.clone());
        let result = if audio.is_empty() {
            crate::voice::log_msg(
                TypioLogLevel::TypioLogWarning,
                "Voice inference: no audio captured or usable",
            );
            None
        } else {
            let engine = session_arc.voice_engine.load(Ordering::SeqCst);
            if engine.is_null() {
                crate::voice::log_msg(TypioLogLevel::TypioLogWarning, "Voice inference: no engine available");
                None
            } else {
                crate::voice::log_msg(
                    TypioLogLevel::TypioLogInfo,
                    &format!("Voice inference: processing {} samples", audio.len()),
                );
                unsafe {
                    match crate::engine::rust_adapter::get_engine(engine) {
                        Some(e) => match e.as_voice() {
                            Some(v) => v.process_audio(&audio),
                            None => {
                                crate::voice::log_msg(TypioLogLevel::TypioLogWarning, "Voice inference: engine is not a voice engine");
                                None
                            }
                        },
                        None => {
                            crate::voice::log_msg(TypioLogLevel::TypioLogWarning, "Voice inference: no Rust engine available");
                            None
                        }
                    }
                }
            }
        };

        // Wake the event loop so dispatch() joins this thread and delivers the
        // result. Without this signal the session stays stuck in Processing and
        // the indicator never clears.
        let fd = session_arc
            .event_fd
            .lock()
            .unwrap()
            .as_ref()
            .map(|efd| efd.as_raw_fd())
            .unwrap_or(-1);
        if fd >= 0 {
            let val: u64 = 1;
            unsafe {
                let _ = libc::write(fd, &val as *const _ as *const libc::c_void, 8);
            }
        }

        result
    });

    *session.infer_handle.lock().unwrap() = Some(handle);
}

#[no_mangle]
pub extern "C" fn typio_voice_session_get_fd(session: *mut TypioVoiceSession) -> i32 {
    if session.is_null() {
        return -1;
    }
    let session = unsafe { &*(session as *const VoiceSession) };
    session.event_fd.lock().unwrap().as_ref().map(|efd| efd.as_raw_fd()).unwrap_or(-1)
}

#[no_mangle]
pub extern "C" fn typio_voice_session_dispatch(session: *mut TypioVoiceSession) {
    if session.is_null() {
        return;
    }
    let session = unsafe { &*(session as *const VoiceSession) };

    // Read and clear eventfd.
    let mut buf = [0u8; 8];
    let event_fd = session.event_fd.lock().unwrap().as_ref().map(|efd| efd.as_raw_fd()).unwrap_or(-1);
    if event_fd >= 0 {
        unsafe {
            let _ = libc::read(event_fd, buf.as_mut_ptr() as *mut libc::c_void, 8);
        }
    }

    // Handle async model load completion.
    {
        let state = session.state.lock().unwrap();
        if *state == VoiceState::Loading {
            let should_auto_start = session.auto_start_on_load.load(Ordering::SeqCst);
            session.auto_start_on_load.store(false, Ordering::SeqCst);
            let voice_engine = session.voice_engine.load(Ordering::SeqCst);
            let has_engine = engine_has_voice(voice_engine);
            drop(state);

            if has_engine && should_auto_start && try_begin_recording(session) {
                *session.state.lock().unwrap() = VoiceState::Recording;
                crate::voice::log_msg(TypioLogLevel::TypioLogInfo, "Voice recording started (auto after load)");
                session.fire_state_change(VoiceState::Recording);
                return;
            }
            *session.state.lock().unwrap() = VoiceState::Idle;
            if !has_engine {
                let event = TypioVoiceSessionEvent {
                    type_: TypioVoiceSessionEventType::Error,
                    state: VoiceState::Idle,
                    text: std::ptr::null_mut(),
                    error: c"Voice model failed to load".as_ptr(),
                };
                session.fire_event(event);
            } else {
                session.fire_state_change(VoiceState::Idle);
            }
            session.idle_armed.store(true, Ordering::SeqCst);
            return;
        }
    }

    // Join inference thread.
    let text = {
        let mut infer = session.infer_handle.lock().unwrap();
        if let Some(handle) = infer.take() {
            handle.join().unwrap_or_default()
        } else {
            None
        }
    };

    let mut state = session.state.lock().unwrap();
    *state = VoiceState::Idle;
    let reload_pending = session.reload_pending.load(Ordering::SeqCst);
    session.reload_pending.store(false, Ordering::SeqCst);
    drop(state);

    if reload_pending {
        do_reload_engine(session);
    }

    // Arm idle timeout now that we're back in idle.
    session.idle_armed.store(true, Ordering::SeqCst);

    if let Some(text) = text.filter(|t| !t.is_empty()) {
        crate::voice::log_msg(TypioLogLevel::TypioLogInfo, &format!("Voice raw: \"{}\"", text));
        let filtered = filter_tags(&text);
        let trimmed = filtered.trim().to_string();
        if !trimmed.is_empty() {
            crate::voice::log_msg(TypioLogLevel::TypioLogInfo, &format!("Voice result: \"{}\"", trimmed));
        }
        let c_text = CString::new(trimmed).unwrap_or_else(|_| CString::new("").unwrap());
        let event = TypioVoiceSessionEvent {
            type_: TypioVoiceSessionEventType::Result,
            state: VoiceState::Idle,
            text: c_text.into_raw(),
            error: std::ptr::null(),
        };
        session.fire_event(event);
    } else {
        session.fire_state_change(VoiceState::Idle);
    }
}

#[no_mangle]
pub extern "C" fn typio_voice_session_is_available(session: *const TypioVoiceSession) -> bool {
    if session.is_null() {
        return false;
    }
    let session = unsafe { &*(session as *const VoiceSession) };
    let has_engine = !session.voice_engine.load(Ordering::SeqCst).is_null();
    let has_source = !session.audio_source.load(Ordering::SeqCst).is_null();
    let voice_engine = session.voice_engine.load(Ordering::SeqCst);
    let engine_ready = engine_has_voice(voice_engine);
    let can_lazy_load = unsafe {
        !voice_engine.is_null()
            && !(*voice_engine).base_ops.is_null()
            && (*(*voice_engine).base_ops).focus_in.is_some()
    };
    has_engine && has_source && (engine_ready || can_lazy_load)
}

#[no_mangle]
pub extern "C" fn typio_voice_session_get_unavail_reason(
    session: *const TypioVoiceSession,
) -> *const c_char {
    if session.is_null() {
        return c"voice session not created".as_ptr();
    }
    let session = unsafe { &*(session as *const VoiceSession) };
    let has_engine = !session.voice_engine.load(Ordering::SeqCst).is_null();
    let has_source = !session.audio_source.load(Ordering::SeqCst).is_null();
    if !has_engine {
        return c"no voice engine active".as_ptr();
    }
    if !has_source {
        return c"audio source unavailable".as_ptr();
    }
    let can_lazy_load = unsafe {
        !session.voice_engine.load(Ordering::SeqCst).is_null()
            && !(*session.voice_engine.load(Ordering::SeqCst)).base_ops.is_null()
            && (*(*session.voice_engine.load(Ordering::SeqCst)).base_ops).focus_in.is_some()
    };
    if !can_lazy_load {
        return c"voice engine missing focus_in (cannot lazy-load)".as_ptr();
    }
    c"".as_ptr()
}

fn do_reload_engine(session: &VoiceSession) {
    let engine = session.voice_engine.load(Ordering::SeqCst);
    if engine.is_null() {
        return;
    }
    unsafe {
        if let Some(e) = crate::engine::rust_adapter::get_engine(engine) {
            let r = e.reload_config();
            crate::voice::log_msg(
                TypioLogLevel::TypioLogInfo,
                &format!("Voice engine reload result: {:?}", r),
            );
        } else {
            let base_ops = &*(*engine).base_ops;
            if let Some(reload) = base_ops.reload_config {
                let r = reload(engine);
                crate::voice::log_msg(
                    TypioLogLevel::TypioLogInfo,
                    &format!("Voice engine reload result: {:?}", r),
                );
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn typio_voice_session_reload_engine(session: *mut TypioVoiceSession) {
    if session.is_null() {
        return;
    }
    let session = unsafe { &*(session as *const VoiceSession) };
    let state = session.state.lock().unwrap();
    if *state != VoiceState::Idle {
        session.reload_pending.store(true, Ordering::SeqCst);
        drop(state);
        crate::voice::log_msg(TypioLogLevel::TypioLogInfo, "Voice reload deferred: session busy");
        return;
    }
    drop(state);
    do_reload_engine(session);
}

#[no_mangle]
pub extern "C" fn typio_voice_session_set_idle_timeout_ms(
    session: *mut TypioVoiceSession,
    timeout_ms: u32,
) {
    if session.is_null() {
        return;
    }
    let session = unsafe { &*(session as *const VoiceSession) };
    session.idle_timeout_ms.store(timeout_ms, Ordering::SeqCst);
}

#[no_mangle]
pub extern "C" fn typio_voice_session_feed_audio(
    session: *mut TypioVoiceSession,
    samples: *const f32,
    count: usize,
) {
    if session.is_null() || samples.is_null() || count == 0 {
        return;
    }
    let session = unsafe { &*(session as *const VoiceSession) };
    let state = session.state.lock().unwrap();
    if *state != VoiceState::Recording {
        drop(state);
        return;
    }
    let mut buf = session.audio_buffer.lock().unwrap();
    let slice = unsafe { std::slice::from_raw_parts(samples, count) };
    buf.extend_from_slice(slice);
    drop(buf);
    drop(state);
}

#[no_mangle]
pub extern "C" fn typio_voice_filter_tags_inplace(text: *mut c_char) {
    if text.is_null() {
        return;
    }
    unsafe {
        let s = CStr::from_ptr(text).to_string_lossy().into_owned();
        let filtered = filter_tags(&s);
        let bytes = filtered.as_bytes();
        let len = bytes.len();
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), text as *mut u8, len);
        *text.add(len) = 0;
    }
}

fn filter_tags(text: &str) -> String {
    let mut result = String::with_capacity(text.len());
    let mut chars = text.chars().peekable();
    while let Some(ch) = chars.next() {
        if ch == '[' {
            let mut tag_end = None;
            for (i, c) in chars.by_ref().enumerate() {
                if c == ']' {
                    tag_end = Some(i);
                    break;
                }
            }
            if tag_end.is_some() {
                // Skip spaces after tag.
                while chars.peek() == Some(&' ') {
                    chars.next();
                }
                if !result.is_empty() && chars.peek().is_some() && !result.ends_with(' ') {
                    result.push(' ');
                }
            } else {
                result.push(ch);
            }
        } else {
            result.push(ch);
        }
    }
    result
}

//! Logging — migrated from C `utils/log.c`

use crate::types::{TypioLogCallback, TypioLogLevel};
use std::collections::VecDeque;
use std::ffi::{c_char, c_void, CStr, CString};
use std::fs::File;
use std::io::Write;
use std::path::Path;
use std::sync::Mutex;

const TYPIO_LOG_RECENT_CAPACITY: usize = 256;

struct LogState {
    level: TypioLogLevel,
    callback: Option<TypioLogCallback>,
    user_data: *mut c_void,
    recent: VecDeque<String>,
    dump_path: Option<String>,
}

unsafe impl Send for LogState {}

lazy_static::lazy_static! {
    static ref LOG_STATE: Mutex<LogState> = Mutex::new(LogState {
        level: TypioLogLevel::TypioLogInfo,
        callback: None,
        user_data: std::ptr::null_mut(),
        recent: VecDeque::with_capacity(TYPIO_LOG_RECENT_CAPACITY),
        dump_path: None,
    });
}

fn level_name(level: TypioLogLevel) -> &'static str {
    match level {
        TypioLogLevel::TypioLogDebug => "DEBUG",
        TypioLogLevel::TypioLogInfo => "INFO",
        TypioLogLevel::TypioLogWarning => "WARN",
        TypioLogLevel::TypioLogError => "ERROR",
    }
}

fn store_recent_line(state: &mut LogState, line: &str) {
    if state.recent.len() >= TYPIO_LOG_RECENT_CAPACITY {
        state.recent.pop_front();
    }
    state.recent.push_back(line.to_string());
}

fn format_time() -> String {
    unsafe {
        let now = libc::time(std::ptr::null_mut());
        let tm_info = libc::localtime(&now);
        if tm_info.is_null() {
            return String::from("????-??-?? ??:??:??");
        }
        let mut buf = [0u8; 32];
        libc::strftime(
            buf.as_mut_ptr() as *mut c_char,
            buf.len(),
            "%Y-%m-%d %H:%M:%S\0".as_ptr() as *const c_char,
            tm_info,
        );
        CStr::from_ptr(buf.as_ptr() as *const c_char)
            .to_string_lossy()
            .into_owned()
    }
}

/// Core log function (non-variadic) — called by the C `typio_log` inline wrapper.
#[no_mangle]
pub extern "C" fn _typio_log(level: TypioLogLevel, msg: *const c_char) {
    if msg.is_null() {
        return;
    }
    let mut state = LOG_STATE.lock().unwrap();
    if (level as i32) < (state.level as i32) {
        return;
    }
    let message = unsafe { CStr::from_ptr(msg).to_string_lossy() };
    let time_str = format_time();
    let rendered = format!("[{}] [{}] {}", time_str, level_name(level), message);
    store_recent_line(&mut state, &rendered);

    if let Some(cb) = state.callback {
        cb(level, msg, state.user_data);
    } else {
        eprintln!("{}", rendered);
    }
}

#[no_mangle]
pub extern "C" fn typio_log_set_level(level: TypioLogLevel) {
    let mut state = LOG_STATE.lock().unwrap();
    state.level = level;
}

#[no_mangle]
pub extern "C" fn typio_log_get_level() -> TypioLogLevel {
    let state = LOG_STATE.lock().unwrap();
    state.level
}

#[no_mangle]
pub extern "C" fn typio_log_set_callback(callback: TypioLogCallback, user_data: *mut c_void) {
    let mut state = LOG_STATE.lock().unwrap();
    state.callback = Some(callback);
    state.user_data = user_data;
}

#[no_mangle]
pub extern "C" fn typio_log_set_recent_dump_path(path: *const c_char) {
    let mut state = LOG_STATE.lock().unwrap();
    if path.is_null() {
        state.dump_path = None;
    } else {
        state.dump_path = Some(
            unsafe { CStr::from_ptr(path).to_string_lossy().into_owned() },
        );
    }
}

#[no_mangle]
pub extern "C" fn typio_log_dump_recent(path: *const c_char) -> bool {
    if path.is_null() {
        return false;
    }
    let path_str = unsafe { CStr::from_ptr(path).to_string_lossy() };
    let state = LOG_STATE.lock().unwrap();

    let parent = Path::new(path_str.as_ref()).parent();
    if let Some(p) = parent {
        if !p.exists() {
            if std::fs::create_dir_all(p).is_err() {
                return false;
            }
        }
    }

    let mut file = match File::create(path_str.as_ref()) {
        Ok(f) => f,
        Err(_) => return false,
    };

    for line in &state.recent {
        if writeln!(file, "{}", line).is_err() {
            return false;
        }
    }
    true
}

#[no_mangle]
pub extern "C" fn typio_log_dump_recent_to_configured_path() -> bool {
    let path = {
        let state = LOG_STATE.lock().unwrap();
        state.dump_path.clone()
    };
    match path {
        Some(ref p) if !p.is_empty() => {
            let cpath = match CString::new(p.as_str()) {
                Ok(c) => c,
                Err(_) => return false,
            };
            typio_log_dump_recent(cpath.as_ptr())
        }
        _ => false,
    }
}

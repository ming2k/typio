//! Instance — Rust implementation of instance.c
//!
//! Manages the Typio instance lifecycle: directories, config, engine manager,
//! input contexts, callbacks, and runtime notifications.

mod callbacks;
mod config_ops;
mod context;
mod rime_state;

pub use callbacks::*;
pub use config_ops::*;
pub use context::*;
pub use rime_state::*;

use crate::config;
use crate::config_schema;

use crate::engine_manager;
use crate::engine_manager::log_msg;
use crate::input_context;
use crate::log::typio_log_set_callback;
use crate::string::typio_strdup;
use crate::types::*;
use std::ffi::{c_void, CStr, CString};
use std::ptr;

const TYPIO_CONFIG_FILE_NAME: &str = "typio.toml";
pub(super) const TYPIO_RIME_STATE_FILE: &str = "rime-state.toml";
pub(super) const TYPIO_RIME_STATE_KEY: &str = "schema";

#[cfg(feature = "build_basic_engine")]
#[allow(improper_ctypes)]
#[allow(dead_code)]
extern "C" {
    fn typio_engine_get_info_basic() -> *const TypioEngineInfo;
    fn typio_engine_create_basic() -> *mut TypioEngine;
}

#[allow(improper_ctypes)]
extern "C" {
    pub(crate) fn typio_voice_session_free(session: *mut TypioVoiceSession);
}

/* -------------------------------------------------------------------------- */
/* Internal helpers                                                           */
/* -------------------------------------------------------------------------- */

pub(super) fn get_default_config_dir() -> String {
    if let Ok(config_home) = std::env::var("XDG_CONFIG_HOME") {
        if !config_home.is_empty() {
            return format!("{}/typio", config_home);
        }
    }
    if let Ok(home) = std::env::var("HOME") {
        if !home.is_empty() {
            return format!("{}/.config/typio", home);
        }
    }
    "/tmp/typio".to_string()
}

pub(super) fn get_default_data_dir() -> String {
    if let Ok(data_home) = std::env::var("XDG_DATA_HOME") {
        if !data_home.is_empty() {
            return format!("{}/typio", data_home);
        }
    }
    if let Ok(home) = std::env::var("HOME") {
        if !home.is_empty() {
            return format!("{}/.local/share/typio", home);
        }
    }
    "/tmp/typio/data".to_string()
}

pub(super) fn get_default_state_dir() -> String {
    if let Ok(state_home) = std::env::var("XDG_STATE_HOME") {
        if !state_home.is_empty() {
            return format!("{}/typio", state_home);
        }
    }
    if let Ok(home) = std::env::var("HOME") {
        if !home.is_empty() {
            return format!("{}/.local/state/typio", home);
        }
    }
    "/tmp/typio/state".to_string()
}

pub(super) fn ensure_directory(path: &str) {
    let _ = std::fs::create_dir_all(path);
}

pub(super) fn build_config_path(config_dir: &str, file_name: &str) -> String {
    format!("{}/{}", config_dir, file_name)
}

pub(super) fn build_state_path(instance: &TypioInstance, file_name: &str) -> Option<String> {
    let state_dir = instance.state_dir.as_ref()?;
    if file_name.is_empty() {
        return None;
    }
    let state_dir_str = state_dir.to_str().ok()?;
    Some(format!("{}/{}", state_dir_str, file_name))
}

pub(super) fn dup_state_string(instance: &TypioInstance, file_name: &str, key: &str) -> Option<CString> {
    if file_name.is_empty() || key.is_empty() {
        return None;
    }
    let path = build_state_path(instance, file_name)?;
    let state = config::typio_config_load_file(
        CString::new(path).ok()?.as_ptr()
    );
    if state.is_null() {
        return None;
    }
    let key_c = CString::new(key).unwrap();
    let value = config::typio_config_get_string(state, key_c.as_ptr(), ptr::null());
    let result = if !value.is_null() {
        let s = unsafe { CStr::from_ptr(value) }.to_string_lossy();
        if !s.is_empty() {
            CString::new(s.as_bytes()).ok()
        } else {
            None
        }
    } else {
        None
    };
    config::typio_config_free(state);
    result
}

pub(super) fn set_state_string(instance: &TypioInstance, file_name: &str, key: &str, value: Option<&str>) -> TypioResult {
    if file_name.is_empty() || key.is_empty() {
        return TypioResult::TypioErrorInvalidArgument;
    }
    let path = match build_state_path(instance, file_name) {
        Some(p) => p,
        None => return TypioResult::TypioErrorOutOfMemory,
    };
    let path_c = CString::new(path).unwrap();
    let state = config::typio_config_load_file(path_c.as_ptr());
    let state = if state.is_null() {
        config::typio_config_new()
    } else {
        state
    };
    if state.is_null() {
        return TypioResult::TypioErrorOutOfMemory;
    }
    let key_c = CString::new(key).unwrap();
    if let Some(v) = value {
        if !v.is_empty() {
            let val_c = CString::new(v).unwrap();
            config::typio_config_set_string(state, key_c.as_ptr(), val_c.as_ptr());
        } else {
            config::typio_config_remove(state, key_c.as_ptr());
        }
    } else {
        config::typio_config_remove(state, key_c.as_ptr());
    }
    let result = config::typio_config_save_file(state, path_c.as_ptr());
    config::typio_config_free(state);
    result
}

pub(super) fn engine_mode_equal(a: &TypioEngineMode, b: &TypioEngineMode) -> bool {
    if a.mode_class != b.mode_class {
        return false;
    }
    let a_id = unsafe { a.mode_id.as_ref() }.and_then(|p| unsafe { CStr::from_ptr(p) }.to_str().ok());
    let b_id = unsafe { b.mode_id.as_ref() }.and_then(|p| unsafe { CStr::from_ptr(p) }.to_str().ok());
    a_id == b_id
}

pub(super) fn engine_mode_store(dst: &mut TypioEngineMode, src: &TypioEngineMode) {
    if !dst.mode_id.is_null() {
        unsafe { libc::free(dst.mode_id as *mut c_void) };
    }
    if !dst.display_label.is_null() {
        unsafe { libc::free(dst.display_label as *mut c_void) };
    }
    if !dst.icon_name.is_null() {
        unsafe { libc::free(dst.icon_name as *mut c_void) };
    }

    dst.mode_class = src.mode_class;
    dst.mode_id = if src.mode_id.is_null() {
        ptr::null()
    } else {
        typio_strdup(src.mode_id)
    };
    dst.display_label = if src.display_label.is_null() {
        ptr::null()
    } else {
        typio_strdup(src.display_label)
    };
    dst.icon_name = if src.icon_name.is_null() {
        ptr::null()
    } else {
        typio_strdup(src.icon_name)
    };
}

/* -------------------------------------------------------------------------- */
/* TypioInstance                                                              */
/* -------------------------------------------------------------------------- */

#[allow(dead_code)]
pub struct TypioInstance {
    pub(crate) engine_manager: *mut engine_manager::TypioEngineManager,
    pub(crate) config: *mut config::Config,

    pub(crate) config_dir: Option<CString>,
    pub(crate) data_dir: Option<CString>,
    pub(crate) state_dir: Option<CString>,
    pub(crate) engine_dir: Option<CString>,
    pub(crate) default_engine: Option<CString>,

    pub(crate) contexts: Vec<*mut input_context::TypioInputContext>,
    pub(crate) focused_context: *mut input_context::TypioInputContext,

    pub(crate) engine_changed_callback: Option<TypioEngineChangedCallback>,
    pub(crate) engine_changed_user_data: *mut c_void,
    pub(crate) voice_engine_changed_callback: Option<TypioVoiceEngineChangedCallback>,
    pub(crate) voice_engine_changed_user_data: *mut c_void,

    pub(crate) status_icon_changed_callback: Option<TypioStatusIconChangedCallback>,
    pub(crate) status_icon_changed_user_data: *mut c_void,
    pub(crate) last_status_icon: Option<CString>,

    pub(crate) mode_changed_callback: Option<TypioModeChangedCallback>,
    pub(crate) mode_changed_user_data: *mut c_void,
    pub(crate) last_mode: TypioEngineMode,
    pub(crate) has_mode: bool,

    pub(crate) log_callback: Option<TypioLogCallback>,
    pub(crate) log_user_data: *mut c_void,

    pub(crate) rime_deploy_requested: bool,
    pub(crate) initialized: bool,
    pub(crate) voice_session: *mut TypioVoiceSession,
}

impl Drop for TypioInstance {
    fn drop(&mut self) {
        if self.initialized {
            self.shutdown();
        }

        for &ctx in &self.contexts {
            if !ctx.is_null() {
                input_context::typio_input_context_free(ctx);
            }
        }
        self.contexts.clear();

        if !self.engine_manager.is_null() {
            engine_manager::typio_engine_manager_free(self.engine_manager);
        }

        if !self.config.is_null() {
            config::typio_config_free(self.config);
        }

        if !self.last_mode.mode_id.is_null() {
            unsafe { libc::free(self.last_mode.mode_id as *mut c_void) };
        }
        if !self.last_mode.display_label.is_null() {
            unsafe { libc::free(self.last_mode.display_label as *mut c_void) };
        }
        if !self.last_mode.icon_name.is_null() {
            unsafe { libc::free(self.last_mode.icon_name as *mut c_void) };
        }

        if !self.voice_session.is_null() {
            unsafe { typio_voice_session_free(self.voice_session) };
            self.voice_session = ptr::null_mut();
        }
    }
}

impl TypioInstance {
    pub(crate) fn shutdown(&mut self) {
        log_msg(TypioLogLevel::TypioLogInfo, "Shutting down Typio instance");
        self.save_config();
        self.initialized = false;
    }

    pub(crate) fn ensure_config(&mut self) -> TypioResult {
        if self.config.is_null() {
            self.config = config::typio_config_new();
        }
        if self.config.is_null() {
            return TypioResult::TypioErrorOutOfMemory;
        }
        config_schema::typio_config_apply_defaults(self.config);
        TypioResult::TypioOk
    }

    pub(crate) fn register_builtin_engines(&mut self) {
        // The guard lives inside the cfg block so the function body is empty
        // (no dangling `return`) when no built-in engines are compiled in.
        #[cfg(feature = "build_basic_engine")]
        {
            if self.engine_manager.is_null() {
                return;
            }
            let result = engine_manager::typio_engine_manager_register(
                self.engine_manager,
                typio_engine_create_basic,
                typio_engine_get_info_basic,
            );
            if result != TypioResult::TypioOk && result != TypioResult::TypioErrorAlreadyExists {
                log_msg(TypioLogLevel::TypioLogWarning, "Failed to register built-in basic engine");
            }
        }
    }

    pub(crate) fn save_config(&self) -> TypioResult {
        if self.config.is_null() {
            return TypioResult::TypioErrorInvalidArgument;
        }
        let config_dir = match self.config_dir.as_ref() {
            Some(d) => d.to_string_lossy(),
            None => return TypioResult::TypioErrorInvalidArgument,
        };
        let path = build_config_path(&config_dir, TYPIO_CONFIG_FILE_NAME);
        let path_c = CString::new(path).unwrap();
        config::typio_config_save_file(self.config, path_c.as_ptr())
    }
}

/* -------------------------------------------------------------------------- */
/* Exported C API — lifecycle                                                 */
/* -------------------------------------------------------------------------- */

#[no_mangle]
pub extern "C" fn typio_instance_new() -> *mut TypioInstance {
    typio_instance_new_with_config(ptr::null())
}

#[no_mangle]
pub extern "C" fn typio_instance_new_with_config(config: *const TypioInstanceConfig) -> *mut TypioInstance {
    let config_dir = if !config.is_null() {
        unsafe { (*config).config_dir.as_ref() }.and_then(|p| unsafe { CStr::from_ptr(p) }.to_str().ok().map(|s| CString::new(s).unwrap()))
    } else {
        None
    }.or_else(|| Some(CString::new(get_default_config_dir()).unwrap()));

    let data_dir = if !config.is_null() {
        unsafe { (*config).data_dir.as_ref() }.and_then(|p| unsafe { CStr::from_ptr(p) }.to_str().ok().map(|s| CString::new(s).unwrap()))
    } else {
        None
    }.or_else(|| Some(CString::new(get_default_data_dir()).unwrap()));

    let state_dir = if !config.is_null() {
        unsafe { (*config).state_dir.as_ref() }.and_then(|p| unsafe { CStr::from_ptr(p) }.to_str().ok().map(|s| CString::new(s).unwrap()))
    } else {
        None
    }.or_else(|| Some(CString::new(get_default_state_dir()).unwrap()));

    let engine_dir = if !config.is_null() {
        unsafe { (*config).engine_dir.as_ref() }.and_then(|p| unsafe { CStr::from_ptr(p) }.to_str().ok().map(|s| CString::new(s).unwrap()))
    } else {
        None
    };

    let engine_dir = engine_dir.or_else(|| {
        if let Ok(dir) = std::env::var("TYPIO_ENGINE_DIR") {
            if !dir.is_empty() {
                return Some(CString::new(dir).unwrap());
            }
        }
        let data = data_dir.as_ref()?.to_str().ok()?;
        Some(CString::new(format!("{}/engines", data)).unwrap())
    });

    let default_engine = if !config.is_null() {
        unsafe { (*config).default_engine.as_ref() }.and_then(|p| unsafe { CStr::from_ptr(p) }.to_str().ok().map(|s| CString::new(s).unwrap()))
    } else {
        None
    };

    let log_callback = if !config.is_null() {
        let cfg = unsafe { &*config };
        if let Some(cb) = cfg.log_callback {
            typio_log_set_callback(cb, cfg.log_user_data);
            Some(cb)
        } else {
            None
        }
    } else {
        None
    };
    let log_user_data = if !config.is_null() {
        unsafe { (*config).log_user_data }
    } else {
        ptr::null_mut()
    };

    let instance = Box::new(TypioInstance {
        engine_manager: ptr::null_mut(),
        config: ptr::null_mut(),
        config_dir,
        data_dir,
        state_dir,
        engine_dir,
        default_engine,
        contexts: Vec::with_capacity(8),
        focused_context: ptr::null_mut(),
        engine_changed_callback: None,
        engine_changed_user_data: ptr::null_mut(),
        voice_engine_changed_callback: None,
        voice_engine_changed_user_data: ptr::null_mut(),
        status_icon_changed_callback: None,
        status_icon_changed_user_data: ptr::null_mut(),
        last_status_icon: None,
        mode_changed_callback: None,
        mode_changed_user_data: ptr::null_mut(),
        last_mode: TypioEngineMode {
            mode_class: TypioModeClass::TypioModeClassNative,
            mode_id: ptr::null(),
            display_label: ptr::null(),
            icon_name: ptr::null(),
        },
        has_mode: false,
        log_callback,
        log_user_data,
        rime_deploy_requested: false,
        initialized: false,
        voice_session: ptr::null_mut(),
    });

    Box::into_raw(instance)
}

#[no_mangle]
pub extern "C" fn typio_instance_free(instance: *mut TypioInstance) {
    if instance.is_null() {
        return;
    }
    unsafe {
        drop(Box::from_raw(instance));
    }
}

#[no_mangle]
pub extern "C" fn typio_instance_init(instance: *mut TypioInstance) -> TypioResult {
    if instance.is_null() {
        return TypioResult::TypioErrorInvalidArgument;
    }

    let inst = unsafe { &mut *instance };

    if inst.initialized {
        return TypioResult::TypioOk;
    }

    log_msg(TypioLogLevel::TypioLogInfo, "Initializing Typio instance");

    if let Some(ref dir) = inst.config_dir {
        ensure_directory(&dir.to_string_lossy());
    }
    if let Some(ref dir) = inst.data_dir {
        ensure_directory(&dir.to_string_lossy());
    }
    if let Some(ref dir) = inst.state_dir {
        ensure_directory(&dir.to_string_lossy());
    }
    if let Some(ref dir) = inst.engine_dir {
        ensure_directory(&dir.to_string_lossy());
    }

    let config_path = match inst.config_dir.as_ref() {
        Some(d) => build_config_path(&d.to_string_lossy(), TYPIO_CONFIG_FILE_NAME),
        None => return TypioResult::TypioErrorInvalidArgument,
    };
    let path_c = CString::new(config_path).unwrap();
    inst.config = config::typio_config_load_file(path_c.as_ptr());
    if inst.config.is_null() {
        inst.config = config::typio_config_new();
    }
    let result = inst.ensure_config();
    if result != TypioResult::TypioOk {
        log_msg(TypioLogLevel::TypioLogError, "Failed to initialize configuration");
        return result;
    }

    inst.engine_manager = engine_manager::typio_engine_manager_new(instance);
    if inst.engine_manager.is_null() {
        log_msg(TypioLogLevel::TypioLogError, "Failed to create engine manager");
        return TypioResult::TypioError;
    }

    inst.register_builtin_engines();

    let engine_dir = match inst.engine_dir.as_ref() {
        Some(d) => d.to_string_lossy().to_string(),
        None => return TypioResult::TypioErrorInvalidArgument,
    };
    let engine_dir_c = CString::new(engine_dir.clone()).unwrap();
    let loaded = engine_manager::typio_engine_manager_load_dir(inst.engine_manager, engine_dir_c.as_ptr());
    log_msg(TypioLogLevel::TypioLogInfo, &format!("Loaded {} engines from {}", loaded, engine_dir));

    // Also scan the system-wide install directory baked in at compile time.
    // Without this, a daemon launched from the .desktop file (which inherits
    // no TYPIO_ENGINE_DIR) only sees the per-user XDG dir — typically empty
    // on a fresh install — and silently runs with no rime/mozc/etc. The
    // user dir is loaded first so user-installed engines shadow the system
    // ones; load_dir refuses duplicates and just logs.
    let system_engine_dir: Option<&str> = option_env!("TYPIO_DEFAULT_ENGINE_DIR")
        .filter(|s| !s.is_empty())
        .map(|s| s.trim_matches('"')); // defend against quoting differences between Makefile and Ninja generators
    if let Some(system_dir) = system_engine_dir {
        if system_dir != engine_dir {
            let system_dir_c = CString::new(system_dir).unwrap();
            let extra = engine_manager::typio_engine_manager_load_dir(
                inst.engine_manager, system_dir_c.as_ptr());
            log_msg(TypioLogLevel::TypioLogInfo, &format!(
                "Loaded {} engines from system dir {}", extra, system_dir));
        }
    }

    let default_engine = inst.default_engine.as_ref()
        .and_then(|s| s.to_str().ok())
        .map(|s| s.to_string())
        .or_else(|| {
            if !inst.config.is_null() {
                let key = CString::new("default_engine").unwrap();
                let val = config::typio_config_get_string(inst.config, key.as_ptr(), ptr::null());
                if !val.is_null() {
                    Some(unsafe { CStr::from_ptr(val) }.to_string_lossy().to_string())
                } else {
                    None
                }
            } else {
                None
            }
        });

    if let Some(ref name) = default_engine {
        let name_c = CString::new(name.as_bytes()).unwrap();
        let result = engine_manager::typio_engine_manager_set_active(inst.engine_manager, name_c.as_ptr());
        if result != TypioResult::TypioOk {
            log_msg(TypioLogLevel::TypioLogWarning, &format!("Failed to activate default engine: {}", name));
        }
    }

    if engine_manager::typio_engine_manager_get_active(inst.engine_manager).is_null() {
        let mut count: usize = 0;
        let engines = engine_manager::typio_engine_manager_list(inst.engine_manager, &mut count);
        if !engines.is_null() && count > 0 {
            let first = unsafe { *engines };
            if !first.is_null() {
                let result = engine_manager::typio_engine_manager_set_active(inst.engine_manager, first);
                if result != TypioResult::TypioOk {
                    let name = unsafe { CStr::from_ptr(first) }.to_string_lossy();
                    log_msg(TypioLogLevel::TypioLogWarning, &format!("Failed to activate first available engine: {}", name));
                }
            }
        }
    }

    inst.initialized = true;
    log_msg(TypioLogLevel::TypioLogInfo, "Typio instance initialized");
    TypioResult::TypioOk
}

#[no_mangle]
pub extern "C" fn typio_instance_shutdown(instance: *mut TypioInstance) {
    if instance.is_null() {
        return;
    }
    let inst = unsafe { &mut *instance };
    inst.shutdown();
}

#[no_mangle]
pub extern "C" fn typio_instance_get_voice_session(
    instance: *mut TypioInstance,
) -> *mut TypioVoiceSession {
    if instance.is_null() {
        return ptr::null_mut();
    }
    unsafe { (*instance).voice_session }
}

#[no_mangle]
pub extern "C" fn typio_instance_set_voice_session(
    instance: *mut TypioInstance,
    session: *mut TypioVoiceSession,
) {
    if instance.is_null() {
        return;
    }
    unsafe {
        (*instance).voice_session = session;
    }
}

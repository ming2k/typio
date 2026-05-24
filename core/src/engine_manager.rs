//! Engine manager — Rust implementation of engine_manager.c
//!
//! Manages the registry of input engines (both plugin-loaded and built-in),
//! active keyboard/voice slots, engine switching, and state persistence.

mod load;
mod query;
mod switch;

pub use load::*;
pub use query::*;
pub use switch::*;

use crate::config;
use crate::engine::{
    typio_engine_activate, typio_engine_free,
    typio_engine_set_config_path,
};
use crate::log::_typio_log;
use crate::types::*;
use crate::TypioInstance;
use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::ptr;
use std::time::Instant;

const TYPIO_ENGINE_CONFIG_SUFFIX: &str = ".toml";
const TYPIO_ENGINE_STATE_FILE: &str = "engine-state.toml";
const TYPIO_ENGINE_STATE_PRIMARY_KEY: &str = "recent.primary";
const TYPIO_ENGINE_STATE_SECONDARY_KEY: &str = "recent.secondary";
const TYPIO_ABI_MIN_VERSION: c_int = 1;
const TYPIO_ABI_MAX_VERSION: c_int = 1;
const TYPIO_ENGINE_INFO_SIZE: usize = std::mem::size_of::<TypioEngineInfo>();

#[cfg(target_os = "linux")]
pub(crate) const RTLD_NOW: c_int = 0x00002;
#[cfg(target_os = "linux")]
pub(crate) const RTLD_LOCAL: c_int = 0x00000;

/* -------------------------------------------------------------------------- */
/* FFI to C runtime                                                           */
/* -------------------------------------------------------------------------- */

// Re-export intra-crate so submodules `use super::typio_instance_*` keeps working.
pub(crate) use crate::instance::{
    typio_instance_clear_mode, typio_instance_clear_status_icon,
    typio_instance_get_config, typio_instance_get_config_dir,
    typio_instance_get_focused_context, typio_instance_get_state_dir,
    typio_instance_notify_mode,
};

#[allow(dead_code)]
extern "C" {
    pub(crate) fn dlopen(filename: *const c_char, flag: c_int) -> *mut c_void;
    pub(crate) fn dlsym(handle: *mut c_void, symbol: *const c_char) -> *mut c_void;
    pub(crate) fn dlclose(handle: *mut c_void) -> c_int;
    pub(crate) fn dlerror() -> *mut c_char;
}

/* -------------------------------------------------------------------------- */
/* Internal types                                                             */
/* -------------------------------------------------------------------------- */

#[allow(dead_code)]
pub(crate) struct EngineEntry {
    pub(crate) name: CString,
    pub(crate) library_path: Option<CString>,
    pub(crate) library_handle: Option<LibraryHandle>,
    pub(crate) factory: Option<TypioEngineFactory>,
    pub(crate) info_func: Option<TypioEngineInfoFunc>,
    pub(crate) info: *const TypioEngineInfo,
    pub(crate) instance: *mut TypioEngine,
    pub(crate) is_builtin: bool,
}

impl Drop for EngineEntry {
    fn drop(&mut self) {
        if !self.instance.is_null() {
            typio_engine_free(self.instance);
        }
        if let Some(handle) = self.library_handle.take() {
            unsafe { dlclose(handle.0) };
        }
    }
}

pub(crate) struct LibraryHandle(pub(crate) *mut c_void);

pub struct TypioEngineManager {
    pub(crate) instance: *mut TypioInstance,
    pub(crate) entries: Vec<EngineEntry>,
    pub(crate) active_keyboard_index: Option<usize>,
    pub(crate) active_voice_index: Option<usize>,
    pub(crate) last_switch: Option<Instant>,
    pub(crate) recent_primary_name: Option<CString>,
    pub(crate) recent_secondary_name: Option<CString>,
    pub(crate) name_list_cache: Vec<*const c_char>,
    pub(crate) ordered_keyboard_list_cache: Vec<*const c_char>,
}

/* -------------------------------------------------------------------------- */
/* Internal helpers                                                           */
/* -------------------------------------------------------------------------- */

pub(crate) fn c_str_to_str(ptr: *const c_char) -> Option<&'static str> {
    if ptr.is_null() {
        return None;
    }
    unsafe { CStr::from_ptr(ptr) }.to_str().ok()
}

pub(crate) fn log_msg(level: TypioLogLevel, msg: &str) {
    if let Ok(cmsg) = CString::new(msg) {
        _typio_log(level, cmsg.as_ptr());
    }
}

pub(crate) fn switch_threshold_ms(instance: *mut TypioInstance) -> u64 {
    if let Ok(val) = std::env::var("TYPIO_SWITCH_STABLE_THRESHOLD_MS") {
        if let Ok(v) = val.parse::<u64>() {
            if v > 0 {
                return v;
            }
        }
    }

    if !instance.is_null() {
        let cfg = typio_instance_get_config(instance);
        if !cfg.is_null() {
            let key = CString::new("engine.switch_stable_threshold_ms").unwrap();
            let val = config::typio_config_get_int(cfg, key.as_ptr(), 1000);
            if val > 0 {
                return val as u64;
            }
        }
    }
    1000
}

impl TypioEngineManager {
    pub(crate) fn new(instance: *mut TypioInstance) -> *mut TypioEngineManager {
        let mut manager = Box::new(TypioEngineManager {
            instance,
            entries: Vec::with_capacity(8),
            active_keyboard_index: None,
            active_voice_index: None,
            last_switch: None,
            recent_primary_name: None,
            recent_secondary_name: None,
            name_list_cache: Vec::new(),
            ordered_keyboard_list_cache: Vec::new(),
        });
        manager.load_recent_state();
        Box::into_raw(manager)
    }

    pub(crate) unsafe fn free(&mut self) {
        self.entries.clear();
    }

    pub(crate) fn find_entry_index(&self, name: &str) -> Option<usize> {
        self.entries.iter().position(|e| {
            e.name.to_str().map(|n| n == name).unwrap_or(false)
        })
    }

    pub(crate) fn find_entry(&self, name: &str) -> Option<&EngineEntry> {
        self.find_entry_index(name).map(|i| &self.entries[i])
    }

    pub(crate) fn name_is_keyboard(&self, name: &str) -> bool {
        self.find_entry(name).map_or(false, |e| {
            !e.info.is_null() && unsafe { (*e.info).type_ } == TypioEngineType::TypioEngineTypeKeyboard
        })
    }

    pub(crate) fn invalidate_caches(&mut self) {
        self.name_list_cache.clear();
        self.ordered_keyboard_list_cache.clear();
    }

    pub(crate) fn build_state_path(&self) -> Option<CString> {
        if self.instance.is_null() {
            return None;
        }
        let state_dir = typio_instance_get_state_dir(self.instance);
        if state_dir.is_null() {
            return None;
        }
        let dir = unsafe { CStr::from_ptr(state_dir) }.to_string_lossy();
        if dir.is_empty() {
            return None;
        }
        let path = format!("{}/{}", dir, TYPIO_ENGINE_STATE_FILE);
        CString::new(path).ok()
    }

    pub(crate) fn load_recent_state(&mut self) {
        let path = match self.build_state_path() {
            Some(p) => p,
            None => return,
        };
        let state = config::typio_config_load_file(path.as_ptr());
        if state.is_null() {
            return;
        }

        let primary_key = CString::new(TYPIO_ENGINE_STATE_PRIMARY_KEY).unwrap();
        let secondary_key = CString::new(TYPIO_ENGINE_STATE_SECONDARY_KEY).unwrap();

        let primary = config::typio_config_get_string(state, primary_key.as_ptr(), ptr::null());
        let secondary = config::typio_config_get_string(state, secondary_key.as_ptr(), ptr::null());

        self.recent_primary_name = c_str_to_str(primary)
            .filter(|s| !s.is_empty())
            .and_then(|s| CString::new(s).ok());
        self.recent_secondary_name = c_str_to_str(secondary)
            .filter(|s| !s.is_empty())
            .and_then(|s| CString::new(s).ok());

        config::typio_config_free(state);
    }

    pub(crate) fn save_recent_state(&self) {
        let path = match self.build_state_path() {
            Some(p) => p,
            None => return,
        };
        let state = config::typio_config_new();
        if state.is_null() {
            return;
        }

        if let Some(ref name) = self.recent_primary_name {
            let key = CString::new(TYPIO_ENGINE_STATE_PRIMARY_KEY).unwrap();
            config::typio_config_set_string(state, key.as_ptr(), name.as_ptr());
        }
        if let Some(ref name) = self.recent_secondary_name {
            let key = CString::new(TYPIO_ENGINE_STATE_SECONDARY_KEY).unwrap();
            config::typio_config_set_string(state, key.as_ptr(), name.as_ptr());
        }

        config::typio_config_save_file(state, path.as_ptr());
        config::typio_config_free(state);
    }

    pub(crate) fn update_recent_pair(&mut self, stable_name: &str) {
        if stable_name.is_empty() {
            return;
        }
        if self.recent_primary_name.as_ref().map(|s| s.to_str().ok()) == Some(Some(stable_name)) {
            return;
        }

        let new_primary = CString::new(stable_name).unwrap();
        let new_secondary = self.recent_primary_name.as_ref()
            .filter(|s| s.to_str().map(|n| n != stable_name).unwrap_or(false))
            .cloned()
            .or_else(|| {
                self.recent_secondary_name.as_ref()
                    .filter(|s| s.to_str().map(|n| n != stable_name).unwrap_or(false))
                    .cloned()
            });

        let changed = self.recent_primary_name.as_ref().map(|s| s.to_str().ok()) != Some(Some(stable_name))
            || self.recent_secondary_name.as_ref().map(|s| s.to_str().ok()) != new_secondary.as_ref().map(|s| s.to_str().ok());

        self.recent_primary_name = Some(new_primary);
        self.recent_secondary_name = new_secondary;

        if changed {
            self.save_recent_state();
        }
    }

    pub(crate) fn recent_partner_index(&self) -> Option<usize> {
        let current_idx = self.active_keyboard_index?;
        let current_name = self.entries.get(current_idx)?.name.to_str().ok()?;

        let partner_name = if self.recent_primary_name.as_ref().and_then(|s| s.to_str().ok()) == Some(current_name) {
            self.recent_secondary_name.as_ref().and_then(|s| s.to_str().ok())
        } else if self.recent_secondary_name.as_ref().and_then(|s| s.to_str().ok()) == Some(current_name) {
            self.recent_primary_name.as_ref().and_then(|s| s.to_str().ok())
        } else {
            None
        };

        let partner_name = partner_name?;
        if !self.name_is_keyboard(partner_name) {
            return None;
        }
        self.find_entry_index(partner_name)
    }

    pub(crate) fn engine_config_path(instance: *mut TypioInstance, engine_name: &str) -> Option<CString> {
        if instance.is_null() {
            return None;
        }
        let config_dir = typio_instance_get_config_dir(instance);
        if config_dir.is_null() {
            return None;
        }
        let dir = unsafe { CStr::from_ptr(config_dir) }.to_string_lossy();
        if dir.is_empty() {
            return None;
        }
        let path = format!("{}/engines/{}{}", dir, engine_name, TYPIO_ENGINE_CONFIG_SUFFIX);
        CString::new(path).ok()
    }

    pub(crate) fn ensure_entry_instance(&mut self, index: usize) -> TypioResult {
        let entry = match self.entries.get_mut(index) {
            Some(e) => e,
            None => return TypioResult::TypioErrorInvalidArgument,
        };

        if !entry.instance.is_null() {
            return TypioResult::TypioOk;
        }

        let factory = match entry.factory {
            Some(f) => f,
            None => {
                log_msg(TypioLogLevel::TypioLogError, &format!("Engine {}: no factory", entry.name.to_string_lossy()));
                return TypioResult::TypioErrorEngineLoadFailed;
            }
        };

        entry.instance = unsafe { factory() };
        if entry.instance.is_null() {
            log_msg(TypioLogLevel::TypioLogError, &format!("Failed to create engine instance: {}", entry.name.to_string_lossy()));
            return TypioResult::TypioErrorEngineLoadFailed;
        }

        let info = if entry.info.is_null() {
            log_msg(TypioLogLevel::TypioLogError, &format!("Engine {}: missing info", entry.name.to_string_lossy()));
            typio_engine_free(entry.instance);
            entry.instance = ptr::null_mut();
            return TypioResult::TypioErrorInvalidArgument;
        } else {
            unsafe { &*entry.info }
        };

        let engine = unsafe { &mut *entry.instance };

        if engine.base_ops.is_null() {
            log_msg(TypioLogLevel::TypioLogError, &format!("Engine {}: missing base_ops", entry.name.to_string_lossy()));
            typio_engine_free(entry.instance);
            entry.instance = ptr::null_mut();
            return TypioResult::TypioErrorInvalidArgument;
        }

        match info.type_ {
            TypioEngineType::TypioEngineTypeKeyboard => {
                if engine.keyboard.is_null() {
                    log_msg(TypioLogLevel::TypioLogError, &format!("Engine {}: keyboard engine missing keyboard ops", entry.name.to_string_lossy()));
                    typio_engine_free(entry.instance);
                    entry.instance = ptr::null_mut();
                    return TypioResult::TypioErrorInvalidArgument;
                }
                let kb = unsafe { &*engine.keyboard };
                if kb.process_key.is_none() {
                    log_msg(TypioLogLevel::TypioLogError, &format!("Engine {}: keyboard engine missing process_key", entry.name.to_string_lossy()));
                    typio_engine_free(entry.instance);
                    entry.instance = ptr::null_mut();
                    return TypioResult::TypioErrorInvalidArgument;
                }
            }
            TypioEngineType::TypioEngineTypeVoice => {
                if engine.voice.is_null() {
                    log_msg(TypioLogLevel::TypioLogError, &format!("Engine {}: voice engine missing voice ops", entry.name.to_string_lossy()));
                    typio_engine_free(entry.instance);
                    entry.instance = ptr::null_mut();
                    return TypioResult::TypioErrorInvalidArgument;
                }
                let voice = unsafe { &*engine.voice };
                if voice.process_audio.is_none() {
                    log_msg(TypioLogLevel::TypioLogError, &format!("Engine {}: voice engine missing process_audio", entry.name.to_string_lossy()));
                    typio_engine_free(entry.instance);
                    entry.instance = ptr::null_mut();
                    return TypioResult::TypioErrorInvalidArgument;
                }
            }
            _ => {}
        }

        let engine_name = entry.name.to_string_lossy().to_string();
        let instance_ptr = self.instance;
        if let Some(path) = Self::engine_config_path(instance_ptr, &engine_name) {
            typio_engine_set_config_path(entry.instance, path.as_ptr());
        }

        TypioResult::TypioOk
    }

    pub(crate) fn try_restore_engine(&self, entry: Option<&EngineEntry>, slot_name: &str) {
        if let Some(entry) = entry {
            if !entry.instance.is_null() {
                let result = typio_engine_activate(entry.instance, self.instance);
                if result != TypioResult::TypioOk {
                    log_msg(TypioLogLevel::TypioLogError, &format!(
                        "Failed to restore previous {} engine '{}' after switch failure: {:?}",
                        slot_name, entry.name.to_string_lossy(), result
                    ));
                }
            }
        }
    }

    pub(crate) fn rebind_focused_context(&self, old_engine: *mut TypioEngine, new_engine: *mut TypioEngine) {
        if self.instance.is_null() {
            return;
        }
        let ctx = typio_instance_get_focused_context(self.instance);
        if ctx.is_null() {
            return;
        }

        if !old_engine.is_null() {
            let engine = unsafe { &*old_engine };
            if !engine.base_ops.is_null() {
                let ops = unsafe { &*engine.base_ops };
                if let Some(focus_out) = ops.focus_out {
                    focus_out(old_engine, ctx);
                }
            }
        }

        typio_instance_clear_status_icon(self.instance);
        typio_instance_clear_mode(self.instance);

        if !new_engine.is_null() {
            let engine = unsafe { &*new_engine };
            if !engine.base_ops.is_null() {
                let ops = unsafe { &*engine.base_ops };
                if let Some(reset) = ops.reset {
                    reset(new_engine, ctx);
                }
                if let Some(focus_in) = ops.focus_in {
                    focus_in(new_engine, ctx);
                }
            }
            if !engine.keyboard.is_null() {
                let kb = unsafe { &*engine.keyboard };
                if let Some(get_mode) = kb.get_mode {
                    let mode = get_mode(new_engine, ctx);
                    if !mode.is_null() {
                        typio_instance_notify_mode(self.instance, mode);
                    }
                }
            }
        }
    }

    pub(crate) fn resolve_switch(&self, ordered: &[&str], direction: i32) -> Option<String> {
        let active_idx = self.active_keyboard_index?;
        let active_name = self.entries.get(active_idx)?.name.to_str().ok()?;

        let now = Instant::now();
        let elapsed = self.last_switch.map(|t| now.duration_since(t).as_millis() as u64).unwrap_or(u64::MAX);

        if elapsed > switch_threshold_ms(self.instance) {
            if let Some(partner_idx) = self.recent_partner_index() {
                return self.entries.get(partner_idx).and_then(|e| e.name.to_str().ok()).map(|s| s.to_string());
            }
        }

        let current_ordered = ordered.iter().position(|&n| n == active_name);
        match current_ordered {
            Some(pos) => {
                let count = ordered.len();
                if direction > 0 {
                    ordered.get((pos + 1) % count).map(|&s| s.to_string())
                } else {
                    ordered.get(if pos == 0 { count - 1 } else { pos - 1 }).map(|&s| s.to_string())
                }
            }
            None => ordered.first().map(|&s| s.to_string()).or_else(|| ordered.last().map(|&s| s.to_string())),
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Exported C API — lifecycle                                                 */
/* -------------------------------------------------------------------------- */

#[no_mangle]
pub extern "C" fn typio_engine_manager_new(instance: *mut TypioInstance) -> *mut TypioEngineManager {
    TypioEngineManager::new(instance)
}

#[no_mangle]
pub extern "C" fn typio_engine_manager_free(manager: *mut TypioEngineManager) {
    if manager.is_null() {
        return;
    }
    unsafe {
        (*manager).free();
        drop(Box::from_raw(manager));
    }
}

#[no_mangle]
pub extern "C" fn typio_engine_manager_register(
    manager: *mut TypioEngineManager,
    factory: TypioEngineFactory,
    info_func: TypioEngineInfoFunc,
) -> TypioResult {
    if manager.is_null() {
        return TypioResult::TypioErrorInvalidArgument;
    }

    let info = unsafe { info_func() };
    if info.is_null() || unsafe { (*info).name }.is_null() {
        return TypioResult::TypioErrorInvalidArgument;
    }

    let info_ref = unsafe { &*info };
    let name = match c_str_to_str(info_ref.name) {
        Some(n) => CString::new(n).unwrap(),
        None => return TypioResult::TypioErrorInvalidArgument,
    };

    let manager_ref = unsafe { &mut *manager };

    if manager_ref.find_entry(&name.to_string_lossy()).is_some() {
        log_msg(TypioLogLevel::TypioLogWarning, &format!("Engine already registered: {}", name.to_string_lossy()));
        return TypioResult::TypioErrorAlreadyExists;
    }

    let entry = EngineEntry {
        name,
        library_path: None,
        library_handle: None,
        factory: Some(factory),
        info_func: Some(info_func),
        info,
        instance: ptr::null_mut(),
        is_builtin: true,
    };

    log_msg(TypioLogLevel::TypioLogInfo, &format!(
        "Registered built-in engine: {}",
        c_str_to_str(info_ref.name).unwrap_or("?")
    ));

    manager_ref.entries.push(entry);
    manager_ref.invalidate_caches();
    TypioResult::TypioOk
}

#[no_mangle]
pub extern "C" fn typio_engine_manager_unload(manager: *mut TypioEngineManager, name: *const c_char) -> TypioResult {
    if manager.is_null() || name.is_null() {
        return TypioResult::TypioErrorInvalidArgument;
    }

    let name_str = unsafe { CStr::from_ptr(name) }.to_string_lossy();
    let manager_ref = unsafe { &mut *manager };

    let idx = match manager_ref.find_entry_index(&name_str) {
        Some(i) => i,
        None => return TypioResult::TypioErrorNotFound,
    };

    if let Some(active_kb) = manager_ref.active_keyboard_index {
        if idx == active_kb {
            manager_ref.active_keyboard_index = None;
        } else if active_kb > idx {
            manager_ref.active_keyboard_index = Some(active_kb - 1);
        }
    }

    if let Some(active_voice) = manager_ref.active_voice_index {
        if idx == active_voice {
            manager_ref.active_voice_index = None;
        } else if active_voice > idx {
            manager_ref.active_voice_index = Some(active_voice - 1);
        }
    }

    manager_ref.entries.remove(idx);
    manager_ref.invalidate_caches();
    TypioResult::TypioOk
}

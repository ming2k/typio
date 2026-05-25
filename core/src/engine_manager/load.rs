//! Engine loading — plugin discovery and dlopen

use super::{c_str_to_str, log_msg, EngineEntry, LibraryHandle, TypioEngineManager, RTLD_NOW, RTLD_LOCAL, TYPIO_ABI_MIN_VERSION, TYPIO_ABI_MAX_VERSION, TYPIO_ENGINE_INFO_SIZE};
use crate::types::*;
use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::ptr;

#[allow(dead_code)]
extern "C" {
    fn dlopen(filename: *const c_char, flag: c_int) -> *mut c_void;
    fn dlsym(handle: *mut c_void, symbol: *const c_char) -> *mut c_void;
    fn dlclose(handle: *mut c_void) -> c_int;
    fn dlerror() -> *mut c_char;
}

#[no_mangle]
pub extern "C" fn typio_engine_manager_load_dir(manager: *mut TypioEngineManager, path: *const c_char) -> c_int {
    if manager.is_null() || path.is_null() {
        return -1;
    }
    let path_str = unsafe { CStr::from_ptr(path) }.to_string_lossy();
    let dir = match std::fs::read_dir(&*path_str) {
        Ok(d) => d,
        Err(_) => {
            log_msg(TypioLogLevel::TypioLogDebug, &format!("Cannot open engine directory: {}", path_str));
            return 0;
        }
    };

    let mut count = 0;
    for entry in dir.flatten() {
        let name = entry.file_name();
        let name_str = name.to_string_lossy();
        if name_str.ends_with(".so") {
            let full_path = entry.path();
            if let Ok(c_path) = CString::new(full_path.to_string_lossy().as_bytes()) {
                if super::typio_engine_manager_load(manager, c_path.as_ptr()) == TypioResult::TypioOk {
                    count += 1;
                }
            }
        }
    }
    count
}

#[no_mangle]
pub extern "C" fn typio_engine_manager_load(manager: *mut TypioEngineManager, path: *const c_char) -> TypioResult {
    if manager.is_null() || path.is_null() {
        return TypioResult::TypioErrorInvalidArgument;
    }

    let path_str = unsafe { CStr::from_ptr(path) }.to_string_lossy();
    log_msg(TypioLogLevel::TypioLogDebug, &format!("Loading engine from: {}", path_str));

    let handle = unsafe { dlopen(path, RTLD_NOW | RTLD_LOCAL) };
    if handle.is_null() {
        let err = unsafe { CStr::from_ptr(dlerror()) }.to_string_lossy();
        log_msg(TypioLogLevel::TypioLogError, &format!("Failed to load engine library: {} ({})", path_str, err));
        return TypioResult::TypioErrorEngineLoadFailed;
    }

    let info_sym = CString::new("typio_engine_get_info").unwrap();
    let info_ptr = unsafe { dlsym(handle, info_sym.as_ptr()) };
    if info_ptr.is_null() {
        log_msg(TypioLogLevel::TypioLogError, &format!("Engine library missing typio_engine_get_info: {}", path_str));
        unsafe { dlclose(handle) };
        return TypioResult::TypioErrorEngineLoadFailed;
    }
    let info_func: TypioEngineInfoFunc = unsafe { std::mem::transmute(info_ptr) };

    let factory_sym = CString::new("typio_engine_create").unwrap();
    let factory_ptr = unsafe { dlsym(handle, factory_sym.as_ptr()) };
    if factory_ptr.is_null() {
        log_msg(TypioLogLevel::TypioLogError, &format!("Engine library missing typio_engine_create: {}", path_str));
        unsafe { dlclose(handle) };
        return TypioResult::TypioErrorEngineLoadFailed;
    }
    let factory: TypioEngineFactory = unsafe { std::mem::transmute(factory_ptr) };

    let info = unsafe { info_func() };
    if info.is_null() || unsafe { (*info).name }.is_null() {
        log_msg(TypioLogLevel::TypioLogError, &format!("Engine returned invalid info: {}", path_str));
        unsafe { dlclose(handle) };
        return TypioResult::TypioErrorEngineLoadFailed;
    }

    let info_ref = unsafe { &*info };
    if info_ref.api_version < TYPIO_ABI_MIN_VERSION || info_ref.api_version > TYPIO_ABI_MAX_VERSION {
        log_msg(TypioLogLevel::TypioLogError, &format!(
            "Engine ABI version mismatch: {} (need {}–{}, engine has {})",
            c_str_to_str(info_ref.name).unwrap_or("?"),
            TYPIO_ABI_MIN_VERSION, TYPIO_ABI_MAX_VERSION,
            info_ref.api_version
        ));
        unsafe { dlclose(handle) };
        return TypioResult::TypioErrorEngineLoadFailed;
    }

    if info_ref.struct_size != 0 && info_ref.struct_size != TYPIO_ENGINE_INFO_SIZE {
        log_msg(TypioLogLevel::TypioLogError, &format!(
            "Engine struct size mismatch: {} (engine {}, daemon {}). Rebuild the engine against the current typio headers.",
            c_str_to_str(info_ref.name).unwrap_or("?"),
            info_ref.struct_size, TYPIO_ENGINE_INFO_SIZE
        ));
        unsafe { dlclose(handle) };
        return TypioResult::TypioErrorEngineLoadFailed;
    }

    let name = match c_str_to_str(info_ref.name) {
        Some(n) => CString::new(n).unwrap(),
        None => {
            unsafe { dlclose(handle) };
            return TypioResult::TypioErrorEngineLoadFailed;
        }
    };

    let manager_ref = unsafe { &mut *manager };

    if manager_ref.find_entry(&name.to_string_lossy()).is_some() {
        log_msg(TypioLogLevel::TypioLogWarning, &format!("Engine already registered: {}", name.to_string_lossy()));
        unsafe { dlclose(handle) };
        return TypioResult::TypioErrorAlreadyExists;
    }

    let entry = EngineEntry {
        name,
        library_path: Some(CString::new(path_str.as_bytes()).unwrap()),
        library_handle: Some(LibraryHandle(handle)),
        factory: Some(factory),
        info_func: Some(info_func),
        info,
        c_engine: ptr::null_mut(),
        is_builtin: false,
    };

    log_msg(TypioLogLevel::TypioLogInfo, &format!(
        "Loaded engine: {} ({})",
        c_str_to_str(info_ref.name).unwrap_or("?"),
        c_str_to_str(info_ref.display_name).unwrap_or("?")
    ));

    manager_ref.entries.push(entry);
    manager_ref.invalidate_caches();
    TypioResult::TypioOk
}

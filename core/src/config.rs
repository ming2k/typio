//! Configuration management — Rust implementation of `typio/config.h`

mod getters;
mod setters;
mod parse;
mod serialize;

pub use getters::*;
pub use setters::*;

use crate::types::*;
use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::{CStr, CString, c_char};
use std::fs;
use std::io::Write;
use std::path::Path;
use std::ptr;

/* -------------------------------------------------------------------------- */
/* Internal representation                                                    */
/* -------------------------------------------------------------------------- */

#[derive(Clone, Debug)]
pub enum ConfigValue {
    String(CString),
    Int(i32),
    Bool(bool),
    Float(f64),
    Array(Vec<ConfigValue>),
    Object(Box<Config>),
}

/// Cache for C-compatible views. All heap allocations inside the cache are
/// owned by the Config and freed when the cache is cleared or dropped.
struct CConfigCache {
    /// C-compatible values for each key. String pointers point into the
    /// corresponding ConfigValue::String CString; array pointers point into
    /// `arrays`; object pointers point into `objects`.
    values: HashMap<String, TypioConfigValue>,
    /// Storage for array item slices. The TypioConfigValue items inside each
    /// vector contain string pointers into the corresponding array elements.
    arrays: HashMap<String, Vec<TypioConfigValue>>,
    /// Storage for cloned object configs.
    objects: HashMap<String, Box<Config>>,
}

impl CConfigCache {
    fn new() -> Self {
        CConfigCache {
            values: HashMap::new(),
            arrays: HashMap::new(),
            objects: HashMap::new(),
        }
    }

    fn clear(&mut self) {
        self.values.clear();
        self.arrays.clear();
        self.objects.clear();
    }
}

/// Opaque configuration object.
pub struct Config {
    pub(crate) entries: HashMap<String, ConfigValue>,
    c_cache: RefCell<CConfigCache>,
}

impl Clone for Config {
    fn clone(&self) -> Self {
        Config {
            entries: self.entries.clone(),
            c_cache: RefCell::new(CConfigCache::new()),
        }
    }
}

impl std::fmt::Debug for Config {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Config")
            .field("entries", &self.entries)
            .finish()
    }
}

impl Config {
    pub(crate) fn new() -> Self {
        Config {
            entries: HashMap::new(),
            c_cache: RefCell::new(CConfigCache::new()),
        }
    }

    pub(crate) fn clear_cache(&self) {
        self.c_cache.borrow_mut().clear();
    }

    fn get_c_value(&self, key: &str) -> Option<*const TypioConfigValue> {
        {
            let cache = self.c_cache.borrow();
            if let Some(val) = cache.values.get(key) {
                return Some(val as *const TypioConfigValue);
            }
        }

        let val = self.entries.get(key)?;
        let mut cache = self.c_cache.borrow_mut();
        let c_val = match val {
            ConfigValue::String(s) => TypioConfigValue {
                type_: TypioConfigType::TypioConfigString,
                data: TypioConfigValueData {
                    string_val: s.as_ptr() as *mut c_char,
                },
            },
            ConfigValue::Int(i) => TypioConfigValue {
                type_: TypioConfigType::TypioConfigInt,
                data: TypioConfigValueData { int_val: *i },
            },
            ConfigValue::Bool(b) => TypioConfigValue {
                type_: TypioConfigType::TypioConfigBool,
                data: TypioConfigValueData { bool_val: *b },
            },
            ConfigValue::Float(v) => TypioConfigValue {
                type_: TypioConfigType::TypioConfigFloat,
                data: TypioConfigValueData { float_val: *v },
            },
            ConfigValue::Array(arr) => {
                let items: Vec<TypioConfigValue> = arr
                    .iter()
                    .map(|item| match item {
                        ConfigValue::String(s) => TypioConfigValue {
                            type_: TypioConfigType::TypioConfigString,
                            data: TypioConfigValueData {
                                string_val: s.as_ptr() as *mut c_char,
                            },
                        },
                        ConfigValue::Int(i) => TypioConfigValue {
                            type_: TypioConfigType::TypioConfigInt,
                            data: TypioConfigValueData { int_val: *i },
                        },
                        ConfigValue::Bool(b) => TypioConfigValue {
                            type_: TypioConfigType::TypioConfigBool,
                            data: TypioConfigValueData { bool_val: *b },
                        },
                        ConfigValue::Float(v) => TypioConfigValue {
                            type_: TypioConfigType::TypioConfigFloat,
                            data: TypioConfigValueData { float_val: *v },
                        },
                        _ => TypioConfigValue {
                            type_: TypioConfigType::TypioConfigString,
                            data: TypioConfigValueData { string_val: ptr::null_mut() },
                        },
                    })
                    .collect();
                let ptr = items.as_ptr() as *mut TypioConfigValue;
                let count = items.len();
                cache.arrays.insert(key.to_string(), items);
                TypioConfigValue {
                    type_: TypioConfigType::TypioConfigArray,
                    data: TypioConfigValueData {
                        array_val: TypioArray { items: ptr, count },
                    },
                }
            }
            ConfigValue::Object(obj) => {
                let mut boxed = Box::new((**obj).clone());
                let ptr = boxed.as_mut() as *mut Config;
                cache.objects.insert(key.to_string(), boxed);
                TypioConfigValue {
                    type_: TypioConfigType::TypioConfigObject,
                    data: TypioConfigValueData { object_val: ptr },
                }
            }
        };
        cache.values.insert(key.to_string(), c_val);
        Some(cache.values.get(key).unwrap() as *const TypioConfigValue)
    }

    pub(crate) fn set_value(&mut self, key: String, value: ConfigValue) {
        self.clear_cache();
        self.entries.insert(key, value);
    }

    fn from_toml(value: &toml::Value, prefix: &str) -> Self {
        let mut config = Config::new();
        config.populate_from_toml(value, prefix);
        config
    }

    fn populate_from_toml(&mut self, value: &toml::Value, prefix: &str) {
        match value {
            toml::Value::Table(table) => {
                for (k, v) in table.iter() {
                    let full_key = if prefix.is_empty() {
                        k.clone()
                    } else {
                        format!("{}.{}", prefix, k)
                    };
                    match v {
                        toml::Value::Table(_) => {
                            self.populate_from_toml(v, &full_key);
                        }
                        _ => {
                            if let Some(cv) = parse::toml_to_config_value(v) {
                                self.entries.insert(full_key, cv);
                            }
                        }
                    }
                }
            }
            _ => {
                if let Some(cv) = parse::toml_to_config_value(value) {
                    let key = if prefix.is_empty() {
                        "value".to_string()
                    } else {
                        prefix.to_string()
                    };
                    self.entries.insert(key, cv);
                }
            }
        }
    }
}

/* -------------------------------------------------------------------------- */
/* C FFI — Constructors & lifecycle                                           */
/* -------------------------------------------------------------------------- */

#[no_mangle]
pub extern "C" fn typio_config_new() -> *mut Config {
    Box::into_raw(Box::new(Config::new()))
}

#[no_mangle]
pub extern "C" fn typio_config_load_file(path: *const c_char) -> *mut Config {
    if path.is_null() {
        return ptr::null_mut();
    }
    let path_str = unsafe { CStr::from_ptr(path).to_string_lossy() };
    let content = match fs::read_to_string(Path::new(&*path_str)) {
        Ok(c) => c,
        Err(_) => return ptr::null_mut(),
    };
    typio_config_load_string(CString::new(content).unwrap().into_raw())
}

#[no_mangle]
pub extern "C" fn typio_config_load_string(content: *const c_char) -> *mut Config {
    if content.is_null() {
        return ptr::null_mut();
    }
    let s = unsafe { CStr::from_ptr(content).to_string_lossy() };
    let parsed: toml::Value = match s.parse() {
        Ok(v) => v,
        Err(_) => {
            return parse::parse_ini_like(&s);
        }
    };
    Box::into_raw(Box::new(Config::from_toml(&parsed, "")))
}

#[no_mangle]
pub extern "C" fn typio_config_free(config: *mut Config) {
    if !config.is_null() {
        unsafe { drop(Box::from_raw(config)) };
    }
}

#[no_mangle]
pub extern "C" fn typio_config_save_file(config: *const Config, path: *const c_char) -> TypioResult {
    if config.is_null() || path.is_null() {
        return TypioResult::TypioErrorInvalidArgument;
    }
    let cfg = unsafe { &*config };
    let path_str = unsafe { CStr::from_ptr(path).to_string_lossy() };
    let path_obj = Path::new(&*path_str);

    let content = serialize::config_to_string_internal(cfg);

    let tmp_path = path_obj.with_extension("tmp");
    match fs::File::create(&tmp_path) {
        Ok(mut file) => {
            if file.write_all(content.as_bytes()).is_err() {
                let _ = fs::remove_file(&tmp_path);
                return TypioResult::TypioError;
            }
            if file.sync_all().is_err() {
                let _ = fs::remove_file(&tmp_path);
                return TypioResult::TypioError;
            }
            drop(file);
            if fs::rename(&tmp_path, path_obj).is_err() {
                let _ = fs::remove_file(&tmp_path);
                return TypioResult::TypioError;
            }
            TypioResult::TypioOk
        }
        Err(_) => TypioResult::TypioError,
    }
}

#[no_mangle]
pub extern "C" fn typio_config_to_string(config: *const Config) -> *mut c_char {
    if config.is_null() {
        return ptr::null_mut();
    }
    let cfg = unsafe { &*config };
    let content = serialize::config_to_string_internal(cfg);
    match CString::new(content) {
        Ok(s) => s.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

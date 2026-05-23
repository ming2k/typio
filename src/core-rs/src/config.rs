//! Configuration management — Rust implementation of `typio/config.h`

use crate::types::*;
use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::{CStr, CString, c_char, c_double, c_int};
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
    fn new() -> Self {
        Config {
            entries: HashMap::new(),
            c_cache: RefCell::new(CConfigCache::new()),
        }
    }

    fn clear_cache(&self) {
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
                type_: TypioConfigType::String,
                data: TypioConfigValueData {
                    string_val: s.as_ptr() as *mut c_char,
                },
            },
            ConfigValue::Int(i) => TypioConfigValue {
                type_: TypioConfigType::Int,
                data: TypioConfigValueData { int_val: *i },
            },
            ConfigValue::Bool(b) => TypioConfigValue {
                type_: TypioConfigType::Bool,
                data: TypioConfigValueData { bool_val: *b },
            },
            ConfigValue::Float(v) => TypioConfigValue {
                type_: TypioConfigType::Float,
                data: TypioConfigValueData { float_val: *v },
            },
            ConfigValue::Array(arr) => {
                let items: Vec<TypioConfigValue> = arr
                    .iter()
                    .map(|item| match item {
                        ConfigValue::String(s) => TypioConfigValue {
                            type_: TypioConfigType::String,
                            data: TypioConfigValueData {
                                string_val: s.as_ptr() as *mut c_char,
                            },
                        },
                        ConfigValue::Int(i) => TypioConfigValue {
                            type_: TypioConfigType::Int,
                            data: TypioConfigValueData { int_val: *i },
                        },
                        ConfigValue::Bool(b) => TypioConfigValue {
                            type_: TypioConfigType::Bool,
                            data: TypioConfigValueData { bool_val: *b },
                        },
                        ConfigValue::Float(v) => TypioConfigValue {
                            type_: TypioConfigType::Float,
                            data: TypioConfigValueData { float_val: *v },
                        },
                        _ => TypioConfigValue {
                            type_: TypioConfigType::String,
                            data: TypioConfigValueData { string_val: ptr::null_mut() },
                        },
                    })
                    .collect();
                let ptr = items.as_ptr() as *mut TypioConfigValue;
                let count = items.len();
                cache.arrays.insert(key.to_string(), items);
                TypioConfigValue {
                    type_: TypioConfigType::Array,
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
                    type_: TypioConfigType::Object,
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
                            if let Some(cv) = toml_to_config_value(v) {
                                self.entries.insert(full_key, cv);
                            }
                        }
                    }
                }
            }
            _ => {
                if let Some(cv) = toml_to_config_value(value) {
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

fn toml_to_config_value(v: &toml::Value) -> Option<ConfigValue> {
    match v {
        toml::Value::String(s) => Some(ConfigValue::String(CString::new(s.clone()).ok()?)),
        toml::Value::Integer(i) => Some(ConfigValue::Int(*i as i32)),
        toml::Value::Float(f) => Some(ConfigValue::Float(*f)),
        toml::Value::Boolean(b) => Some(ConfigValue::Bool(*b)),
        toml::Value::Array(arr) => {
            let items: Vec<ConfigValue> = arr.iter().filter_map(toml_to_config_value).collect();
            Some(ConfigValue::Array(items))
        }
        toml::Value::Table(table) => {
            let mut cfg = Config::new();
            for (k, v) in table.iter() {
                if let Some(cv) = toml_to_config_value(v) {
                    cfg.entries.insert(k.clone(), cv);
                }
            }
            Some(ConfigValue::Object(Box::new(cfg)))
        }
        toml::Value::Datetime(_) => None,
    }
}

fn parse_ini_value(value: &str) -> ConfigValue {
    let trimmed = value.trim();

    // Array
    if trimmed.starts_with('[') && trimmed.ends_with(']') {
        let inner = &trimmed[1..trimmed.len() - 1];
        let items: Vec<ConfigValue> = inner
            .split(',')
            .map(|s| parse_ini_value(s.trim()))
            .collect();
        return ConfigValue::Array(items);
    }

    // Bool
    if trimmed.eq_ignore_ascii_case("true") {
        return ConfigValue::Bool(true);
    }
    if trimmed.eq_ignore_ascii_case("false") {
        return ConfigValue::Bool(false);
    }

    // String with quotes
    if (trimmed.starts_with('"') && trimmed.ends_with('"'))
        || (trimmed.starts_with('\'') && trimmed.ends_with('\''))
    {
        return ConfigValue::String(CString::new(&trimmed[1..trimmed.len() - 1]).unwrap_or_else(|_| CString::default()));
    }

    // Number
    if let Ok(i) = trimmed.parse::<i32>() {
        return ConfigValue::Int(i);
    }
    if let Ok(f) = trimmed.parse::<f64>() {
        return ConfigValue::Float(f);
    }

    // Default to string
    ConfigValue::String(CString::new(trimmed).unwrap_or_else(|_| CString::default()))
}

/* -------------------------------------------------------------------------- */
/* Serialization helpers                                                      */
/* -------------------------------------------------------------------------- */

fn write_escaped_string(f: &mut dyn Write, s: &str) -> std::io::Result<()> {
    f.write_all(b"\"")?;
    for ch in s.chars() {
        match ch {
            '\\' => f.write_all(b"\\\\")?,
            '"' => f.write_all(b"\\\"")?,
            '\n' => f.write_all(b"\\n")?,
            '\r' => f.write_all(b"\\r")?,
            '\t' => f.write_all(b"\\t")?,
            c => {
                let mut buf = [0u8; 4];
                f.write_all(c.encode_utf8(&mut buf).as_bytes())?;
            }
        }
    }
    f.write_all(b"\"")
}

fn write_value(f: &mut dyn Write, v: &ConfigValue) -> std::io::Result<()> {
    match v {
        ConfigValue::String(s) => {
            if let Ok(s_str) = s.to_str() {
                write_escaped_string(f, s_str)
            } else {
                f.write_all(b"\"\"")
            }
        }
        ConfigValue::Int(i) => write!(f, "{}", i),
        ConfigValue::Bool(b) => write!(f, "{}", if *b { "true" } else { "false" }),
        ConfigValue::Float(v) => write!(f, "{}", v),
        ConfigValue::Array(arr) => {
            f.write_all(b"[")?;
            for (i, item) in arr.iter().enumerate() {
                if i > 0 {
                    f.write_all(b", ")?;
                }
                write_value(f, item)?;
            }
            f.write_all(b"]")
        }
        ConfigValue::Object(_) => Ok(()),
    }
}

fn config_to_string_internal(cfg: &Config) -> String {
    let mut output = Vec::new();
    let f = &mut output;

    f.write_all(b"# Typio configuration file (TOML-compatible subset)\n\n").unwrap();

    let mut top_level: Vec<(&String, &ConfigValue)> = Vec::new();
    let mut sections: std::collections::BTreeMap<String, Vec<(&str, &ConfigValue)>> =
        std::collections::BTreeMap::new();

    for (key, value) in cfg.entries.iter() {
        if let Some(dot_pos) = key.find('.') {
            let section = key[..dot_pos].to_string();
            let subkey = &key[dot_pos + 1..];
            sections.entry(section).or_default().push((subkey, value));
        } else {
            top_level.push((key, value));
        }
    }

    for (key, value) in top_level {
        f.write_all(key.as_bytes()).unwrap();
        f.write_all(b" = ").unwrap();
        write_value(f, value).unwrap();
        f.write_all(b"\n").unwrap();
    }

    for (section, entries) in sections {
        f.write_all(b"\n[").unwrap();
        f.write_all(section.as_bytes()).unwrap();
        f.write_all(b"]\n").unwrap();
        for (subkey, value) in entries {
            f.write_all(subkey.as_bytes()).unwrap();
            f.write_all(b" = ").unwrap();
            write_value(f, value).unwrap();
            f.write_all(b"\n").unwrap();
        }
    }

    String::from_utf8(output).unwrap()
}

/* -------------------------------------------------------------------------- */
/* C FFI                                                                      */
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
            return parse_ini_like(&s);
        }
    };
    Box::into_raw(Box::new(Config::from_toml(&parsed, "")))
}

fn parse_ini_like(content: &str) -> *mut Config {
    let mut config = Config::new();
    let mut section = String::new();

    for line in content.lines() {
        let trimmed = line.trim();
        if trimmed.is_empty() || trimmed.starts_with('#') || trimmed.starts_with(';') {
            continue;
        }

        if trimmed.starts_with('[') && trimmed.ends_with(']') {
            section = trimmed[1..trimmed.len() - 1].trim().to_string();
            continue;
        }

        if let Some(eq_pos) = trimmed.find('=') {
            let key = trimmed[..eq_pos].trim();
            let value = trimmed[eq_pos + 1..].trim();

            let full_key = if section.is_empty() {
                key.to_string()
            } else {
                format!("{}.{}", section, key)
            };

            let cv = parse_ini_value(value);
            config.entries.insert(full_key, cv);
        }
    }

    Box::into_raw(Box::new(config))
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
        return TypioResult::InvalidArgument;
    }
    let cfg = unsafe { &*config };
    let path_str = unsafe { CStr::from_ptr(path).to_string_lossy() };
    let path_obj = Path::new(&*path_str);

    let content = config_to_string_internal(cfg);

    let tmp_path = path_obj.with_extension("tmp");
    match fs::File::create(&tmp_path) {
        Ok(mut file) => {
            if file.write_all(content.as_bytes()).is_err() {
                let _ = fs::remove_file(&tmp_path);
                return TypioResult::Error;
            }
            if file.sync_all().is_err() {
                let _ = fs::remove_file(&tmp_path);
                return TypioResult::Error;
            }
            drop(file);
            if fs::rename(&tmp_path, path_obj).is_err() {
                let _ = fs::remove_file(&tmp_path);
                return TypioResult::Error;
            }
            TypioResult::Ok
        }
        Err(_) => TypioResult::Error,
    }
}

#[no_mangle]
pub extern "C" fn typio_config_to_string(config: *const Config) -> *mut c_char {
    if config.is_null() {
        return ptr::null_mut();
    }
    let cfg = unsafe { &*config };
    let content = config_to_string_internal(cfg);
    match CString::new(content) {
        Ok(s) => s.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

/* -------------------------------------------------------------------------- */
/* Getters                                                                    */
/* -------------------------------------------------------------------------- */

#[no_mangle]
pub extern "C" fn typio_config_get_string(
    config: *const Config,
    key: *const c_char,
    default_val: *const c_char,
) -> *const c_char {
    if config.is_null() || key.is_null() {
        return default_val;
    }
    let cfg = unsafe { &*config };
    let key_str = unsafe { CStr::from_ptr(key).to_string_lossy() };

    match cfg.entries.get(&*key_str) {
        Some(ConfigValue::String(s)) => s.as_ptr(),
        _ => default_val,
    }
}

#[no_mangle]
pub extern "C" fn typio_config_get_int(
    config: *const Config,
    key: *const c_char,
    default_val: c_int,
) -> c_int {
    if config.is_null() || key.is_null() {
        return default_val;
    }
    let cfg = unsafe { &*config };
    let key_str = unsafe { CStr::from_ptr(key).to_string_lossy() };

    match cfg.entries.get(&*key_str) {
        Some(ConfigValue::Int(i)) => *i,
        Some(ConfigValue::Float(f)) => *f as c_int,
        _ => default_val,
    }
}

#[no_mangle]
pub extern "C" fn typio_config_get_bool(
    config: *const Config,
    key: *const c_char,
    default_val: bool,
) -> bool {
    if config.is_null() || key.is_null() {
        return default_val;
    }
    let cfg = unsafe { &*config };
    let key_str = unsafe { CStr::from_ptr(key).to_string_lossy() };

    match cfg.entries.get(&*key_str) {
        Some(ConfigValue::Bool(b)) => *b,
        _ => default_val,
    }
}

#[no_mangle]
pub extern "C" fn typio_config_get_float(
    config: *const Config,
    key: *const c_char,
    default_val: c_double,
) -> c_double {
    if config.is_null() || key.is_null() {
        return default_val;
    }
    let cfg = unsafe { &*config };
    let key_str = unsafe { CStr::from_ptr(key).to_string_lossy() };

    match cfg.entries.get(&*key_str) {
        Some(ConfigValue::Float(f)) => *f,
        Some(ConfigValue::Int(i)) => *i as c_double,
        _ => default_val,
    }
}

#[no_mangle]
pub extern "C" fn typio_config_get(
    config: *const Config,
    key: *const c_char,
) -> *const TypioConfigValue {
    if config.is_null() || key.is_null() {
        return ptr::null();
    }
    let cfg = unsafe { &*config };
    let key_str = unsafe { CStr::from_ptr(key).to_string_lossy() };
    cfg.get_c_value(&key_str).unwrap_or(ptr::null())
}

/* -------------------------------------------------------------------------- */
/* Setters                                                                    */
/* -------------------------------------------------------------------------- */

#[no_mangle]
pub extern "C" fn typio_config_set_string(
    config: *mut Config,
    key: *const c_char,
    value: *const c_char,
) -> TypioResult {
    if config.is_null() || key.is_null() {
        return TypioResult::InvalidArgument;
    }
    let cfg = unsafe { &mut *config };
    let key_str = unsafe { CStr::from_ptr(key).to_string_lossy().into_owned() };
    let val_str = if value.is_null() {
        CString::default()
    } else {
        unsafe { CStr::from_ptr(value).to_owned() }
    };
    cfg.set_value(key_str, ConfigValue::String(val_str));
    TypioResult::Ok
}

#[no_mangle]
pub extern "C" fn typio_config_set_int(
    config: *mut Config,
    key: *const c_char,
    value: c_int,
) -> TypioResult {
    if config.is_null() || key.is_null() {
        return TypioResult::InvalidArgument;
    }
    let cfg = unsafe { &mut *config };
    let key_str = unsafe { CStr::from_ptr(key).to_string_lossy().into_owned() };
    cfg.set_value(key_str, ConfigValue::Int(value));
    TypioResult::Ok
}

#[no_mangle]
pub extern "C" fn typio_config_set_bool(
    config: *mut Config,
    key: *const c_char,
    value: bool,
) -> TypioResult {
    if config.is_null() || key.is_null() {
        return TypioResult::InvalidArgument;
    }
    let cfg = unsafe { &mut *config };
    let key_str = unsafe { CStr::from_ptr(key).to_string_lossy().into_owned() };
    cfg.set_value(key_str, ConfigValue::Bool(value));
    TypioResult::Ok
}

#[no_mangle]
pub extern "C" fn typio_config_set_float(
    config: *mut Config,
    key: *const c_char,
    value: c_double,
) -> TypioResult {
    if config.is_null() || key.is_null() {
        return TypioResult::InvalidArgument;
    }
    let cfg = unsafe { &mut *config };
    let key_str = unsafe { CStr::from_ptr(key).to_string_lossy().into_owned() };
    cfg.set_value(key_str, ConfigValue::Float(value));
    TypioResult::Ok
}

#[no_mangle]
pub extern "C" fn typio_config_set_string_array(
    config: *mut Config,
    key: *const c_char,
    values: *const *const c_char,
    count: usize,
) -> TypioResult {
    if config.is_null() || key.is_null() || (values.is_null() && count > 0) {
        return TypioResult::InvalidArgument;
    }
    let cfg = unsafe { &mut *config };
    let key_str = unsafe { CStr::from_ptr(key).to_string_lossy().into_owned() };

    let mut items = Vec::with_capacity(count);
    for i in 0..count {
        let ptr = unsafe { *values.add(i) };
        let s = if ptr.is_null() {
            CString::default()
        } else {
            unsafe { CStr::from_ptr(ptr).to_owned() }
        };
        items.push(ConfigValue::String(s));
    }

    cfg.set_value(key_str, ConfigValue::Array(items));
    TypioResult::Ok
}

/* -------------------------------------------------------------------------- */
/* Sections                                                                   */
/* -------------------------------------------------------------------------- */

#[no_mangle]
pub extern "C" fn typio_config_get_section(
    config: *const Config,
    section: *const c_char,
) -> *mut Config {
    if config.is_null() || section.is_null() {
        return ptr::null_mut();
    }
    let cfg = unsafe { &*config };
    let section_str = unsafe { CStr::from_ptr(section).to_string_lossy() };
    let prefix = format!("{}.", section_str);

    let mut sub = Config::new();
    for (key, value) in cfg.entries.iter() {
        if key.starts_with(&prefix) {
            let subkey = &key[prefix.len()..];
            sub.entries.insert(subkey.to_string(), value.clone());
        }
    }

    Box::into_raw(Box::new(sub))
}

#[no_mangle]
pub extern "C" fn typio_config_set_section(
    config: *mut Config,
    section: *const c_char,
    sub_config: *const Config,
) -> TypioResult {
    if config.is_null() || section.is_null() || sub_config.is_null() {
        return TypioResult::InvalidArgument;
    }
    let cfg = unsafe { &mut *config };
    let section_str = unsafe { CStr::from_ptr(section).to_string_lossy() };
    let sub = unsafe { &*sub_config };

    for (key, value) in sub.entries.iter() {
        let full_key = format!("{}.{}", section_str, key);
        cfg.set_value(full_key, value.clone());
    }

    TypioResult::Ok
}

/* -------------------------------------------------------------------------- */
/* Array access                                                               */
/* -------------------------------------------------------------------------- */

#[no_mangle]
pub extern "C" fn typio_config_get_array_size(config: *const Config, key: *const c_char) -> usize {
    if config.is_null() || key.is_null() {
        return 0;
    }
    let cfg = unsafe { &*config };
    let key_str = unsafe { CStr::from_ptr(key).to_string_lossy() };

    match cfg.entries.get(&*key_str) {
        Some(ConfigValue::Array(arr)) => arr.len(),
        _ => 0,
    }
}

#[no_mangle]
pub extern "C" fn typio_config_get_array_string(
    config: *const Config,
    key: *const c_char,
    index: usize,
) -> *const c_char {
    if config.is_null() || key.is_null() {
        return ptr::null();
    }
    let cfg = unsafe { &*config };
    let key_str = unsafe { CStr::from_ptr(key).to_string_lossy() };

    match cfg.entries.get(&*key_str) {
        Some(ConfigValue::Array(arr)) if index < arr.len() => match &arr[index] {
            ConfigValue::String(s) => s.as_ptr(),
            _ => ptr::null(),
        },
        _ => ptr::null(),
    }
}

#[no_mangle]
pub extern "C" fn typio_config_get_array_int(
    config: *const Config,
    key: *const c_char,
    index: usize,
) -> c_int {
    if config.is_null() || key.is_null() {
        return 0;
    }
    let cfg = unsafe { &*config };
    let key_str = unsafe { CStr::from_ptr(key).to_string_lossy() };

    match cfg.entries.get(&*key_str) {
        Some(ConfigValue::Array(arr)) if index < arr.len() => match &arr[index] {
            ConfigValue::Int(i) => *i,
            _ => 0,
        },
        _ => 0,
    }
}

/* -------------------------------------------------------------------------- */
/* Key enumeration                                                            */
/* -------------------------------------------------------------------------- */

#[no_mangle]
pub extern "C" fn typio_config_key_count(config: *const Config) -> usize {
    if config.is_null() {
        return 0;
    }
    let cfg = unsafe { &*config };
    cfg.entries.len()
}

#[no_mangle]
pub extern "C" fn typio_config_key_at(config: *const Config, index: usize) -> *const c_char {
    if config.is_null() {
        return ptr::null();
    }
    let cfg = unsafe { &*config };
    let keys: Vec<&String> = cfg.entries.keys().collect();
    if index >= keys.len() {
        return ptr::null();
    }
    match CString::new(keys[index].clone()) {
        Ok(cs) => cs.into_raw(),
        Err(_) => ptr::null(),
    }
}

#[no_mangle]
pub extern "C" fn typio_config_has_key(config: *const Config, key: *const c_char) -> bool {
    if config.is_null() || key.is_null() {
        return false;
    }
    let cfg = unsafe { &*config };
    let key_str = unsafe { CStr::from_ptr(key).to_string_lossy() };
    cfg.entries.contains_key(&*key_str)
}

/* -------------------------------------------------------------------------- */
/* Remove / Merge                                                             */
/* -------------------------------------------------------------------------- */

#[no_mangle]
pub extern "C" fn typio_config_remove(config: *mut Config, key: *const c_char) -> TypioResult {
    if config.is_null() || key.is_null() {
        return TypioResult::InvalidArgument;
    }
    let cfg = unsafe { &mut *config };
    let key_str = unsafe { CStr::from_ptr(key).to_string_lossy().into_owned() };
    if cfg.entries.remove(&key_str).is_some() {
        cfg.clear_cache();
        TypioResult::Ok
    } else {
        TypioResult::NotFound
    }
}

#[no_mangle]
pub extern "C" fn typio_config_merge(dest: *mut Config, src: *const Config) -> TypioResult {
    if dest.is_null() || src.is_null() {
        return TypioResult::InvalidArgument;
    }
    let dst = unsafe { &mut *dest };
    let s = unsafe { &*src };

    for (key, value) in s.entries.iter() {
        dst.set_value(key.clone(), value.clone());
    }

    TypioResult::Ok
}

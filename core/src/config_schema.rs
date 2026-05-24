//! Static config schema registry — Rust implementation of `typio/config_schema.h`

use crate::config::Config;
use crate::types::*;
use std::ffi::{CStr, CString, c_char};
use std::os::raw::{c_double, c_int};
use std::ptr;

/* -------------------------------------------------------------------------- */
/* Schema definition                                                          */
/* -------------------------------------------------------------------------- */

static POPUP_THEME_OPTIONS: &[&str] = &["auto", "light", "dark"];
static CANDIDATE_LAYOUT_OPTIONS: &[&str] = &["horizontal", "vertical"];
static BASIC_PRINTABLE_KEY_MODE_OPTIONS: &[&str] = &["forward", "commit"];

/// Wrapper to make raw-pointer Vecs usable with lazy_static.
struct SyncCStrVec(Vec<*const c_char>);
unsafe impl Sync for SyncCStrVec {}
unsafe impl Send for SyncCStrVec {}

fn build_options(options: &[&str]) -> SyncCStrVec {
    let mut v: Vec<*const c_char> = options
        .iter()
        .map(|s| CString::new(*s).unwrap().into_raw() as *const c_char)
        .collect();
    v.push(ptr::null());
    SyncCStrVec(v)
}

lazy_static::lazy_static! {
    static ref POPUP_THEME_OPTS: SyncCStrVec = build_options(POPUP_THEME_OPTIONS);
    static ref CANDIDATE_LAYOUT_OPTS: SyncCStrVec = build_options(CANDIDATE_LAYOUT_OPTIONS);
    static ref BASIC_PRINTABLE_KEY_MODE_OPTS: SyncCStrVec = build_options(BASIC_PRINTABLE_KEY_MODE_OPTIONS);
}

/// Schema entry definition — Rust-native form.
struct SchemaEntry {
    key: &'static str,
    type_: TypioFieldType,
    def: SchemaDefault,
    ui_label: Option<&'static str>,
    ui_section: Option<&'static str>,
    ui_min: c_int,
    ui_max: c_int,
    ui_step: c_int,
    ui_options: Option<&'static SyncCStrVec>,
    runtime_property: Option<&'static str>,
}

enum SchemaDefault {
    String(&'static str),
    Int(c_int),
    Bool(bool),
    #[allow(dead_code)]
    Float(c_double),
}

lazy_static::lazy_static! {
    static ref SCHEMA: Vec<SchemaEntry> = vec![
    SchemaEntry { key: "default_engine", type_: TypioFieldType::TypioFieldString, def: SchemaDefault::String(""), ui_label: None, ui_section: None, ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: Some("ActiveKeyboardEngine") },
    SchemaEntry { key: "keyboard.per_app_preferences", type_: TypioFieldType::TypioFieldBool, def: SchemaDefault::Bool(true), ui_label: Some("Per-app preferences"), ui_section: Some("keyboard"), ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: None },
    SchemaEntry { key: "display.popup_theme", type_: TypioFieldType::TypioFieldString, def: SchemaDefault::String("auto"), ui_label: Some("Theme"), ui_section: Some("display"), ui_min: 0, ui_max: 0, ui_step: 0, ui_options: Some(&POPUP_THEME_OPTS), runtime_property: None },
    SchemaEntry { key: "display.candidate_layout", type_: TypioFieldType::TypioFieldString, def: SchemaDefault::String("horizontal"), ui_label: Some("Layout"), ui_section: Some("display"), ui_min: 0, ui_max: 0, ui_step: 0, ui_options: Some(&CANDIDATE_LAYOUT_OPTS), runtime_property: None },
    SchemaEntry { key: "display.font_size", type_: TypioFieldType::TypioFieldInt, def: SchemaDefault::Int(11), ui_label: Some("Font size"), ui_section: Some("display"), ui_min: 6, ui_max: 72, ui_step: 1, ui_options: None, runtime_property: None },
    SchemaEntry { key: "display.font_family", type_: TypioFieldType::TypioFieldString, def: SchemaDefault::String(""), ui_label: Some("Font family"), ui_section: Some("display"), ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: None },
    SchemaEntry { key: "display.popup_mode_indicator", type_: TypioFieldType::TypioFieldBool, def: SchemaDefault::Bool(false), ui_label: Some("Mode indicator"), ui_section: Some("display"), ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: None },
    SchemaEntry { key: "notifications.enable", type_: TypioFieldType::TypioFieldBool, def: SchemaDefault::Bool(true), ui_label: Some("Enable"), ui_section: Some("notifications"), ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: None },
    SchemaEntry { key: "notifications.startup_checks", type_: TypioFieldType::TypioFieldBool, def: SchemaDefault::Bool(true), ui_label: Some("Startup checks"), ui_section: Some("notifications"), ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: None },
    SchemaEntry { key: "notifications.runtime", type_: TypioFieldType::TypioFieldBool, def: SchemaDefault::Bool(true), ui_label: Some("Runtime alerts"), ui_section: Some("notifications"), ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: None },
    SchemaEntry { key: "notifications.voice", type_: TypioFieldType::TypioFieldBool, def: SchemaDefault::Bool(true), ui_label: Some("Voice alerts"), ui_section: Some("notifications"), ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: None },
    SchemaEntry { key: "notifications.cooldown_ms", type_: TypioFieldType::TypioFieldInt, def: SchemaDefault::Int(15000), ui_label: Some("Cooldown (ms)"), ui_section: Some("notifications"), ui_min: 0, ui_max: 300000, ui_step: 1000, ui_options: None, runtime_property: None },
    SchemaEntry { key: "engines.basic.printable_key_mode", type_: TypioFieldType::TypioFieldString, def: SchemaDefault::String("forward"), ui_label: Some("Printable keys"), ui_section: Some("basic"), ui_min: 0, ui_max: 0, ui_step: 0, ui_options: Some(&BASIC_PRINTABLE_KEY_MODE_OPTS), runtime_property: None },
    SchemaEntry { key: "engines.basic.compose", type_: TypioFieldType::TypioFieldBool, def: SchemaDefault::Bool(false), ui_label: Some("Enable compose sequences"), ui_section: Some("basic"), ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: None },
    SchemaEntry { key: "engines.rime.shared_data_dir", type_: TypioFieldType::TypioFieldString, def: SchemaDefault::String(""), ui_label: None, ui_section: None, ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: None },
    SchemaEntry { key: "engines.rime.user_data_dir", type_: TypioFieldType::TypioFieldString, def: SchemaDefault::String(""), ui_label: None, ui_section: None, ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: None },
    SchemaEntry { key: "default_voice_engine", type_: TypioFieldType::TypioFieldString, def: SchemaDefault::String(""), ui_label: Some("Voice Backend"), ui_section: Some("voice"), ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: Some("ActiveVoiceEngine") },
    SchemaEntry { key: "shortcuts.switch_engine", type_: TypioFieldType::TypioFieldString, def: SchemaDefault::String("Ctrl+Shift"), ui_label: Some("Switch engine"), ui_section: Some("shortcuts"), ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: None },
    SchemaEntry { key: "shortcuts.emergency_exit", type_: TypioFieldType::TypioFieldString, def: SchemaDefault::String("Ctrl+Shift+Escape"), ui_label: Some("Emergency exit"), ui_section: Some("shortcuts"), ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: None },
    SchemaEntry { key: "shortcuts.voice_ptt", type_: TypioFieldType::TypioFieldString, def: SchemaDefault::String("Super+v"), ui_label: Some("Voice (PTT)"), ui_section: Some("shortcuts"), ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: None },
    SchemaEntry { key: "engines.whisper.model", type_: TypioFieldType::TypioFieldString, def: SchemaDefault::String("base"), ui_label: None, ui_section: None, ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: None },
    SchemaEntry { key: "engines.whisper.language", type_: TypioFieldType::TypioFieldString, def: SchemaDefault::String(""), ui_label: None, ui_section: None, ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: None },
    SchemaEntry { key: "engines.sherpa-onnx.model", type_: TypioFieldType::TypioFieldString, def: SchemaDefault::String(""), ui_label: None, ui_section: None, ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: None },
    SchemaEntry { key: "engines.sherpa-onnx.language", type_: TypioFieldType::TypioFieldString, def: SchemaDefault::String(""), ui_label: None, ui_section: None, ui_min: 0, ui_max: 0, ui_step: 0, ui_options: None, runtime_property: None },
    SchemaEntry { key: "voice.unload_after_ms", type_: TypioFieldType::TypioFieldInt, def: SchemaDefault::Int(480000), ui_label: Some("Unload model after (ms)"), ui_section: Some("voice"), ui_min: 0, ui_max: 3600000, ui_step: 60000, ui_options: None, runtime_property: None },
    ];
}

/* -------------------------------------------------------------------------- */
/* Cached C-compatible schema table                                           */
/* -------------------------------------------------------------------------- */

struct SyncSchemaTable(Vec<TypioConfigField>);
unsafe impl Sync for SyncSchemaTable {}
unsafe impl Send for SyncSchemaTable {}

lazy_static::lazy_static! {
    static ref SCHEMA_C_TABLE: SyncSchemaTable = SyncSchemaTable(
        SCHEMA
            .iter()
            .map(|e| {
                let def = match e.def {
                    SchemaDefault::String(s) => TypioFieldDefault {
                        s: CString::new(s).unwrap().into_raw(),
                    },
                    SchemaDefault::Int(i) => TypioFieldDefault { i },
                    SchemaDefault::Bool(b) => TypioFieldDefault { b },
                    SchemaDefault::Float(f) => TypioFieldDefault { f },
                };
                TypioConfigField {
                    key: CString::new(e.key).unwrap().into_raw(),
                    type_: e.type_,
                    def,
                    ui_label: e.ui_label.map_or(ptr::null(), |s| {
                        CString::new(s).unwrap().into_raw() as *const c_char
                    }),
                    ui_section: e.ui_section.map_or(ptr::null(), |s| {
                        CString::new(s).unwrap().into_raw() as *const c_char
                    }),
                    ui_min: e.ui_min,
                    ui_max: e.ui_max,
                    ui_step: e.ui_step,
                    ui_options: e.ui_options.map_or(ptr::null(), |opts| opts.0.as_ptr()),
                    runtime_property: e.runtime_property.map_or(ptr::null(), |s| {
                        CString::new(s).unwrap().into_raw() as *const c_char
                    }),
                }
            })
            .collect()
    );
}

/* -------------------------------------------------------------------------- */
/* C FFI                                                                      */
/* -------------------------------------------------------------------------- */

#[no_mangle]
pub extern "C" fn typio_config_schema_fields(count: *mut usize) -> *const TypioConfigField {
    let table = &SCHEMA_C_TABLE.0;
    if !count.is_null() {
        unsafe { *count = table.len() };
    }
    table.as_ptr()
}

#[no_mangle]
pub extern "C" fn typio_config_schema_find(key: *const c_char) -> *const TypioConfigField {
    if key.is_null() {
        return ptr::null();
    }
    let key_str = unsafe { CStr::from_ptr(key).to_string_lossy() };
    let table = &SCHEMA_C_TABLE.0;
    table
        .iter()
        .find(|f| {
            let f_key = unsafe { CStr::from_ptr(f.key).to_string_lossy() };
            f_key == key_str.as_ref()
        })
        .map_or(ptr::null(), |f| f as *const TypioConfigField)
}

#[no_mangle]
pub extern "C" fn typio_config_schema_runtime_property(key: *const c_char) -> *const c_char {
    let field = typio_config_schema_find(key);
    if field.is_null() {
        return ptr::null();
    }
    let f = unsafe { &*field };
    if f.runtime_property.is_null() {
        return ptr::null();
    }
    let prop = unsafe { CStr::from_ptr(f.runtime_property).to_string_lossy() };
    if prop.is_empty() {
        return ptr::null();
    }
    f.runtime_property
}

#[no_mangle]
pub extern "C" fn typio_config_apply_defaults(config: *mut Config) {
    if config.is_null() {
        return;
    }
    let cfg = unsafe { &mut *config };

    for entry in SCHEMA.iter() {
        let key = entry.key;
        if cfg.entries.contains_key(key) {
            continue;
        }
        match &entry.def {
            SchemaDefault::String(s) if !s.is_empty() => {
                cfg.set_value(key.to_string(), crate::config::ConfigValue::String(CString::new(s.to_string()).unwrap()));
            }
            SchemaDefault::Int(i) => {
                cfg.set_value(key.to_string(), crate::config::ConfigValue::Int(*i));
            }
            SchemaDefault::Bool(b) => {
                cfg.set_value(key.to_string(), crate::config::ConfigValue::Bool(*b));
            }
            SchemaDefault::Float(f) => {
                cfg.set_value(key.to_string(), crate::config::ConfigValue::Float(*f));
            }
            _ => {}
        }
    }
}

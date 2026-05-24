use crate::config::Config;
use std::ffi::{c_char, CStr, CString};
use std::{env, fs};
use std::path::Path;
use std::ptr;

pub const TYPIO_RIME_SCHEMA_LIST_MAX_SCHEMAS: usize = 32;

#[repr(C)]
pub struct TypioRimeSchemaInfo {
    pub id: *mut c_char,
    pub name: *mut c_char,
}

#[repr(C)]
pub struct TypioRimeSchemaList {
    pub available: bool,
    pub current_schema: *mut c_char,
    pub user_data_dir: *mut c_char,
    pub schemas: [TypioRimeSchemaInfo; TYPIO_RIME_SCHEMA_LIST_MAX_SCHEMAS],
    pub schema_count: usize,
}

fn expand_path(path: &str) -> String {
    let mut expanded = String::new();
    let mut chars = path.chars().peekable();

    if let Some('~') = chars.peek() {
        chars.next();
        if chars.peek() == Some(&'/') || chars.peek().is_none() {
            if let Ok(home) = env::var("HOME") {
                expanded.push_str(&home);
            } else {
                expanded.push('~');
            }
        } else {
            expanded.push('~');
        }
    }

    let remaining: String = chars.collect();
    
    // Simplistic $ENV expansion
    // To match C exactly: replace $VAR and ${VAR}
    let mut i = 0;
    while i < remaining.len() {
        if remaining[i..].starts_with("${") {
            let end = remaining[i+2..].find('}');
            if let Some(end_idx) = end {
                let var_name = &remaining[i+2..i+2+end_idx];
                if let Ok(val) = env::var(var_name) {
                    expanded.push_str(&val);
                } else {
                    expanded.push_str(&remaining[i..i+3+end_idx]);
                }
                i += 3 + end_idx;
                continue;
            }
        } else if remaining[i..].starts_with('$') {
            let end_idx = remaining[i+1..].find(|c: char| !c.is_alphanumeric() && c != '_').unwrap_or(remaining.len() - i - 1);
            if end_idx > 0 {
                let var_name = &remaining[i+1..i+1+end_idx];
                if let Ok(val) = env::var(var_name) {
                    expanded.push_str(&val);
                } else {
                    expanded.push_str(&remaining[i..i+1+end_idx]);
                }
                i += 1 + end_idx;
                continue;
            }
        }
        
        expanded.push(remaining[i..].chars().next().unwrap());
        i += remaining[i..].chars().next().unwrap().len_utf8();
    }

    expanded
}

fn dup_trimmed_value(line: &str) -> Option<String> {
    let mut trimmed = line.trim();
    if trimmed.starts_with('"') || trimmed.starts_with('\'') {
        trimmed = &trimmed[1..];
    }
    if trimmed.ends_with('"') || trimmed.ends_with('\'') {
        trimmed = &trimmed[..trimmed.len()-1];
    }
    let res = trimmed.trim().to_string();
    if res.is_empty() { None } else { Some(res) }
}

fn parse_schema_list(path: &Path, list: &mut TypioRimeSchemaList) -> bool {
    let content = match fs::read_to_string(path) {
        Ok(c) => c,
        Err(_) => return false,
    };

    for line in content.lines() {
        let trimmed = line.trim_start();
        if trimmed.starts_with('#') {
            continue;
        }

        if let Some(idx) = line.find("- schema:") {
            if list.schema_count >= TYPIO_RIME_SCHEMA_LIST_MAX_SCHEMAS {
                break;
            }
            if let Some(id) = dup_trimmed_value(&line[idx + 9..]) {
                if let Ok(c_id) = CString::new(id) {
                    list.schemas[list.schema_count].id = c_id.into_raw();
                    list.schema_count += 1;
                }
            }
        }
    }
    list.schema_count > 0
}

fn parse_schema_name(path: &Path) -> Option<String> {
    let content = fs::read_to_string(path).ok()?;
    let mut in_schema = false;
    
    for line in content.lines() {
        if line.starts_with("schema:") {
            in_schema = true;
            continue;
        }
        if in_schema && line.starts_with("  name:") {
            return dup_trimmed_value(&line[7..]);
        }
    }
    None
}

fn fill_schema_names(list: &mut TypioRimeSchemaList) {
    if list.user_data_dir.is_null() {
        return;
    }
    let user_data_dir = unsafe { CStr::from_ptr(list.user_data_dir) }.to_string_lossy().into_owned();
    let base_path = Path::new(&user_data_dir);

    for i in 0..list.schema_count {
        if list.schemas[i].id.is_null() {
            continue;
        }
        let id = unsafe { CStr::from_ptr(list.schemas[i].id) }.to_string_lossy();
        
        let path1 = base_path.join(format!("{}.schema.yaml", id));
        let path2 = base_path.join(format!("build/{}.schema.yaml", id));
        
        let mut name = parse_schema_name(&path1);
        if name.is_none() {
            name = parse_schema_name(&path2);
        }
        
        if let Some(n) = name {
            if let Ok(c_name) = CString::new(n) {
                list.schemas[i].name = c_name.into_raw();
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn typio_rime_schema_list_load(
    config: *const Config,
    default_data_dir: *const c_char,
    list: *mut TypioRimeSchemaList,
) -> bool {
    if list.is_null() {
        return false;
    }
    let list_ref = unsafe { &mut *list };
    unsafe { std::ptr::write_bytes(list_ref as *mut _, 0, 1) };

    if !config.is_null() {
        let cfg = unsafe { &*config };
        
        if let Some(crate::config::ConfigValue::String(s)) = cfg.entries.get("schema") {
            list_ref.current_schema = CString::new(s.as_bytes()).unwrap().into_raw();
        }
        if let Some(crate::config::ConfigValue::String(s)) = cfg.entries.get("user_data_dir") {
            let expanded = expand_path(s.to_string_lossy().as_ref());
            list_ref.user_data_dir = CString::new(expanded).unwrap().into_raw();
        }
    }

    if list_ref.user_data_dir.is_null() && !default_data_dir.is_null() {
        let default_dir = unsafe { CStr::from_ptr(default_data_dir) }.to_string_lossy();
        let expanded = expand_path(&default_dir);
        list_ref.user_data_dir = CString::new(expanded).unwrap().into_raw();
    }

    if !list_ref.user_data_dir.is_null() {
        let base_dir = unsafe { CStr::from_ptr(list_ref.user_data_dir) }.to_string_lossy().into_owned();
        let path1 = Path::new(&base_dir).join("default.custom.yaml");
        let path2 = Path::new(&base_dir).join("build/default.yaml");

        if !parse_schema_list(&path1, list_ref) {
            parse_schema_list(&path2, list_ref);
        }
    }

    fill_schema_names(list_ref);

    list_ref.available = !list_ref.current_schema.is_null() || list_ref.schema_count > 0;
    list_ref.available
}

#[no_mangle]
pub extern "C" fn typio_rime_schema_list_clear(list: *mut TypioRimeSchemaList) {
    if list.is_null() {
        return;
    }
    let list_ref = unsafe { &mut *list };
    
    if !list_ref.current_schema.is_null() {
        unsafe { drop(CString::from_raw(list_ref.current_schema)) };
        list_ref.current_schema = ptr::null_mut();
    }
    
    if !list_ref.user_data_dir.is_null() {
        unsafe { drop(CString::from_raw(list_ref.user_data_dir)) };
        list_ref.user_data_dir = ptr::null_mut();
    }
    
    for i in 0..list_ref.schema_count {
        if !list_ref.schemas[i].id.is_null() {
            unsafe { drop(CString::from_raw(list_ref.schemas[i].id)) };
            list_ref.schemas[i].id = ptr::null_mut();
        }
        if !list_ref.schemas[i].name.is_null() {
            unsafe { drop(CString::from_raw(list_ref.schemas[i].name)) };
            list_ref.schemas[i].name = ptr::null_mut();
        }
    }
    
    list_ref.schema_count = 0;
    list_ref.available = false;
}

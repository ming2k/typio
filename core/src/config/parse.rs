//! Parsing helpers for configuration files (TOML and INI-like)

use super::{Config, ConfigValue};
use std::ffi::CString;

pub(super) fn toml_to_config_value(v: &toml::Value) -> Option<ConfigValue> {
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

pub(super) fn parse_ini_like(content: &str) -> *mut Config {
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

//! Serialization helpers for configuration output

use super::{Config, ConfigValue};
use std::collections::BTreeMap;
use std::io::Write;

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

pub(super) fn config_to_string_internal(cfg: &Config) -> String {
    let mut output = Vec::new();
    let f = &mut output;

    f.write_all(b"# Typio configuration file (TOML-compatible subset)\n\n").unwrap();

    let mut top_level: Vec<(&String, &ConfigValue)> = Vec::new();
    let mut sections: BTreeMap<String, Vec<(&str, &ConfigValue)>> = BTreeMap::new();

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

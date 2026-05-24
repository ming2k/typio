use std::io;

use serde_json::Value;

use crate::ipc::IpcClient;

fn require_daemon() -> io::Result<IpcClient> {
    IpcClient::connect().map_err(|e| {
        io::Error::new(
            e.kind(),
            format!("{e}\n\nIs the daemon running? Start it with: daemon"),
        )
    })
}

pub fn cmd_status() -> io::Result<()> {
    let mut client = require_daemon()?;
    let result = client.get_all()?;

    let scalars = [
        ("Version", "Version"),
        ("ActiveKeyboardEngine", "ActiveKeyboardEngine"),
        ("ActiveVoiceEngine", "ActiveVoiceEngine"),
        ("RimeSchema", "RimeSchema"),
    ];

    for (label, key) in &scalars {
        if let Some(v) = result.get(key).and_then(|v| v.as_str()) {
            if !v.is_empty() {
                println!("{:<26} {}", label, v);
            }
        }
    }

    for (label, key) in &[
        ("OrderedKeyboardEngines", "OrderedKeyboardEngines"),
        ("AvailableVoiceEngines", "AvailableVoiceEngines"),
    ] {
        if let Some(arr) = result.get(key).and_then(|v| v.as_array()) {
            print!("{:<26}", label);
            let mut first = true;
            for item in arr {
                if let Some(s) = item.as_str() {
                    print!("{}{}", if first { " " } else { ", " }, s);
                    first = false;
                }
            }
            println!();
        }
    }

    Ok(())
}

pub fn cmd_stop() -> io::Result<()> {
    let mut client = require_daemon()?;
    client.call("Stop", None)?;
    println!("Daemon stopped.");
    Ok(())
}

pub fn cmd_version() -> io::Result<()> {
    let mut client = require_daemon()?;
    let result = client.get_all()?;
    if let Some(v) = result.get("Version").and_then(|v| v.as_str()) {
        println!("{}", v);
    }
    Ok(())
}

pub fn cmd_engine(sub: Option<&str>) -> io::Result<()> {
    let mut client = require_daemon()?;

    let sub = match sub {
        Some(s) => s,
        None => {
            let result = client.get_all()?;
            if let Some(v) = result
                .get("ActiveKeyboardEngine")
                .and_then(|v| v.as_str())
            {
                println!("{}", v);
            }
            return Ok(());
        }
    };

    match sub {
        "list" => {
            let result = client.get_all()?;
            let active = result
                .get("ActiveKeyboardEngine")
                .and_then(|v| v.as_str())
                .unwrap_or("");
            if let Some(arr) = result
                .get("OrderedKeyboardEngines")
                .and_then(|v| v.as_array())
            {
                for item in arr {
                    if let Some(name) = item.as_str() {
                        if name == active {
                            println!("* {}", name);
                        } else {
                            println!("  {}", name);
                        }
                    }
                }
            }
        }
        "next" => {
            client.call("NextEngine", None)?;
        }
        name => {
            client.call(
                "ActivateEngine",
                Some(Value::Object(
                    [("engine".to_string(), Value::String(name.to_string()))]
                        .into_iter()
                        .collect(),
                )),
            )?;
        }
    }
    Ok(())
}

pub fn cmd_rime_schema(schema: Option<&str>) -> io::Result<()> {
    let mut client = require_daemon()?;

    if let Some(schema) = schema {
        client.call(
            "SetRimeSchema",
            Some(Value::Object(
                [("schema".to_string(), Value::String(schema.to_string()))]
                    .into_iter()
                    .collect(),
            )),
        )?;
    } else {
        let result = client.get_all()?;
        if let Some(v) = result.get("RimeSchema").and_then(|v| v.as_str()) {
            println!("{}", v);
        }
    }
    Ok(())
}

pub fn cmd_rime(sub: Option<&str>, args: &[String]) -> io::Result<()> {
    match sub {
        Some("schema") => cmd_rime_schema(args.first().map(|s| s.as_str())),
        Some("deploy") => {
            let mut client = require_daemon()?;
            client.call("DeployRimeConfig", None)?;
            Ok(())
        }
        Some(other) => cmd_rime_schema(Some(other)),
        None => cmd_rime_schema(None),
    }
}

pub fn cmd_config(sub: Option<&str>, args: &[String]) -> io::Result<()> {
    let mut client = require_daemon()?;

    match sub {
        Some("reload") => {
            client.call("ReloadConfig", None)?;
        }
        Some("get") => {
            let result = client.get_all()?;
            if let Some(v) = result.get("ConfigText").and_then(|v| v.as_str()) {
                print!("{}", v);
            }
        }
        Some("set") => {
            let text = args.first().ok_or_else(|| {
                io::Error::new(io::ErrorKind::InvalidInput, "config set expects a text argument")
            })?;
            client.call(
                "SetConfigText",
                Some(Value::Object(
                    [("content".to_string(), Value::String(text.clone()))]
                        .into_iter()
                        .collect(),
                )),
            )?;
        }
        _ => {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "Usage: cli config <reload|get|set TEXT>",
            ));
        }
    }
    Ok(())
}

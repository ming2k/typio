use std::env;
use std::io::{self, Read, Write};
use std::os::unix::net::UnixStream;
use std::path::PathBuf;

use serde_json::Value;

/// Return the canonical UDS socket path.
///
/// Prefers $XDG_RUNTIME_DIR/typio/daemon.sock,
/// falls back to ~/.local/share/typio/daemon.sock,
/// then /tmp/typio-daemon.sock.
pub fn socket_path() -> Option<PathBuf> {
    if let Ok(runtime_dir) = env::var("XDG_RUNTIME_DIR") {
        if !runtime_dir.is_empty() {
            let mut p = PathBuf::from(runtime_dir);
            p.push("typio");
            p.push("daemon.sock");
            return Some(p);
        }
    }

    if let Ok(home) = env::var("HOME") {
        if !home.is_empty() {
            let mut p = PathBuf::from(home);
            p.push(".local");
            p.push("share");
            p.push("typio");
            p.push("daemon.sock");
            return Some(p);
        }
    }

    Some(PathBuf::from("/tmp/typio-daemon.sock"))
}

/// Simple JSON-RPC 2.0 client over UDS with 4-byte BE length prefix.
pub struct IpcClient {
    stream: UnixStream,
    next_id: i64,
}

impl IpcClient {
    pub fn connect() -> io::Result<Self> {
        let path = socket_path().ok_or_else(|| {
            io::Error::new(io::ErrorKind::NotFound, "could not resolve daemon socket path")
        })?;
        let stream = UnixStream::connect(&path)?;
        Ok(IpcClient {
            stream,
            next_id: 1,
        })
    }

    /// Send a JSON-RPC request and wait for the response.
    pub fn call(&mut self, method: &str, params: Option<Value>) -> io::Result<Value> {
        let id = self.next_id;
        self.next_id += 1;

        let req = serde_json::json!({
            "jsonrpc": "2.0",
            "id": id,
            "method": method,
            "params": params.unwrap_or_else(|| serde_json::json!({})),
        });
        let req_bytes = serde_json::to_vec(&req)?;

        if req_bytes.len() > (1 << 20) {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "request too large",
            ));
        }

        let len_be = (req_bytes.len() as u32).to_be_bytes();
        self.stream.write_all(&len_be)?;
        self.stream.write_all(&req_bytes)?;
        self.stream.flush()?;

        // Read 4-byte length prefix
        let mut len_buf = [0u8; 4];
        self.stream.read_exact(&mut len_buf)?;
        let resp_len = u32::from_be_bytes(len_buf) as usize;
        if resp_len > (1 << 20) {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "response too large",
            ));
        }

        // Read payload
        let mut resp_buf = vec![0u8; resp_len];
        self.stream.read_exact(&mut resp_buf)?;
        let resp: Value = serde_json::from_slice(&resp_buf).map_err(|e| {
            io::Error::new(io::ErrorKind::InvalidData, format!("invalid JSON: {e}"))
        })?;

        // Check id matches
        if let Some(resp_id) = resp.get("id").and_then(|v| v.as_i64()) {
            if resp_id != id {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    "response id mismatch",
                ));
            }
        }

        // Return either result or error
        if let Some(err) = resp.get("error") {
            let msg = err
                .get("message")
                .and_then(|v| v.as_str())
                .unwrap_or("unknown error");
            return Err(io::Error::new(io::ErrorKind::Other, msg.to_string()));
        }

        Ok(resp.get("result").cloned().unwrap_or(Value::Null))
    }

    /// Convenience: call GetAll and return the property map.
    pub fn get_all(&mut self) -> io::Result<Value> {
        self.call("GetAll", None)
    }
}

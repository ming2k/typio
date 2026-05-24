# IPC Protocol Reference

Typio daemon exposes a **Unix Domain Socket (UDS)** for control and introspection. This is the primary transport used by the `typio` CLI. A legacy D-Bus adapter is still available; see [D-Bus Interface Reference](dbus-interface.md) for details.

## Socket location

| Variable | Default | Override |
|----------|---------|----------|
| Path | `$XDG_RUNTIME_DIR/typio/daemon.sock` | Set `XDG_RUNTIME_DIR` before starting the daemon |

The socket is created with mode `0600`. Incoming connections are validated with `SO_PEERCRED`; connections from a different uid are rejected.

## Wire format

All messages use **big-endian length-prefix framing**:

```
[ 4 bytes: payload length in bytes (big-endian) ]
[ N bytes: UTF-8 JSON payload                     ]
```

Both requests and responses use the same framing.

## Request format

Requests are JSON objects:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `jsonrpc` | `string` | Yes | Always `"2.0"` |
| `id` | `integer` | Yes | Request identifier; echoed in the response |
| `method` | `string` | Yes | Method name (see table below) |
| `params` | `object` | No | Method-specific parameters |

Example:

```json
{"jsonrpc":"2.0","id":1,"method":"GetAll"}
```

## Response format

Successful responses:

```json
{"jsonrpc":"2.0","id":1,"result":{...}}
```

Error responses:

```json
{"jsonrpc":"2.0","id":1,"error":{"code":-32603,"message":"Internal error"}}
```

## Methods

| Method | Params | Result | Description |
|--------|--------|--------|-------------|
| `GetAll` | — | `object` | All properties (see table below) |
| `Get` | `{"property":"Name"}` | `value` | Single property value |
| `ActivateEngine` | `{"engine":"rime"}` | `null` | Switch to named engine |
| `NextEngine` | — | `null` | Cycle to next engine |
| `SetRimeSchema` | `{"schema":"luna_pinyin"}` | `null` | Set Rime schema and reload |
| `DeployRimeConfig` | — | `null` | Rebuild Rime config artifacts |
| `SetConfigText` | `{"content":"..."}` | `null` | Replace entire config |
| `ReloadConfig` | — | `null` | Reload config from disk |
| `Stop` | — | `null` | Shut down the daemon |

## Properties

`GetAll` returns an object with the following keys. All values are JSON-native types (strings, numbers, booleans, arrays, or objects).

| Property | Type | Description |
|----------|------|-------------|
| `Version` | `string` | Build version string |
| `ActiveKeyboardEngine` | `string` | Currently active keyboard engine |
| `ActiveEngine` | `string` | Alias for `ActiveKeyboardEngine` |
| `AvailableKeyboardEngines` | `string[]` | All registered keyboard engines |
| `AvailableEngines` | `string[]` | All engines (keyboard + voice) |
| `OrderedKeyboardEngines` | `string[]` | Keyboard engines in switch order |
| `OrderedEngines` | `string[]` | Alias for `OrderedKeyboardEngines` |
| `EngineDisplayNames` | `object` | `{name: display_name}` mapping |
| `EngineOrder` | `string[]` | Full `engine_order` array from config |
| `AvailableVoiceEngines` | `string[]` | Registered voice engines |
| `ActiveVoiceEngine` | `string` | Currently active voice engine |
| `ActiveEngineMode` | `object` | Mode details (`mode_class`, `mode_id`, etc.) |
| `ActiveEngineState` | `object` | Engine details (`name`, `display_name`, `capabilities`, `config.*`, etc.) |
| `RuntimeState` | `object` | Wayland frontend diagnostics |
| `RimeSchema` | `string` | Current Rime schema identifier |
| `ConfigText` | `string` | Full `typio.toml` contents |

## Notifications

The server may send unsolicited JSON objects (no `id` field) when state changes:

```json
{"jsonrpc":"2.0","method":"PropertiesChanged","params":{...}}
```

Clients should handle these alongside synchronous responses.

## Error codes

| Code | Meaning |
|------|---------|
| `-32600` | Invalid Request |
| `-32601` | Method not found |
| `-32602` | Invalid params |
| `-32603` | Internal error |

## Quick example (Python)

```python
import socket, struct, json

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect("/run/user/1000/typio/daemon.sock")

req = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "GetAll"})
frame = struct.pack(">I", len(req)) + req.encode()
sock.sendall(frame)

# Read length
length = struct.unpack(">I", sock.recv(4))[0]
resp = json.loads(sock.recv(length))
print(resp["result"]["ActiveKeyboardEngine"])
```

## Implementation notes

- Server: `daemon/ipc/uds_server.c`, `daemon/ipc/ipc_bus.c`, `daemon/ipc/status_service.c`
- Client (Rust): `cli/src/ipc.rs`
- Protocol helpers: `daemon/ipc/tip_protocol.h`, `daemon/ipc/tip_json.h`
- Integration test: `tests/test_uds_ipc.c`

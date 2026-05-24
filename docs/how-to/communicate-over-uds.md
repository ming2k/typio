# How to Communicate with Typio over UDS

The Typio daemon exposes a Unix Domain Socket (UDS) for control and introspection. This guide shows how to connect, send requests, and handle responses.

## Prerequisites

- Typio daemon is running (`typio-daemon` or your desktop environment's autostart).
- You know the socket path (usually `$XDG_RUNTIME_DIR/typio/daemon.sock`).

## Find the socket path

```bash
# Default location (when XDG_RUNTIME_DIR is set)
echo "$XDG_RUNTIME_DIR/typio/daemon.sock"

# Verify the daemon is listening
ls -l "$XDG_RUNTIME_DIR/typio/daemon.sock"
```

The socket is created with permissions `0600`; only your own user can connect.

## Connect

Use `AF_UNIX` + `SOCK_STREAM`:

```c
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int fd = socket(AF_UNIX, SOCK_STREAM, 0);
struct sockaddr_un addr = { .sun_family = AF_UNIX };
strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
connect(fd, (struct sockaddr *)&addr, sizeof(addr));
```

Or in Python:

```python
import socket
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect("/run/user/1000/typio/daemon.sock")
```

## Send a request

All messages use **big-endian length-prefix framing**:

```
[ 4 bytes: payload length (big-endian) ]
[ N bytes: UTF-8 JSON payload          ]
```

Example: query all properties

```python
import struct, json

req = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "GetAll"})
frame = struct.pack(">I", len(req)) + req.encode()
sock.sendall(frame)
```

## Read the response

Read 4 bytes for the length, then read exactly that many bytes:

```python
length = struct.unpack(">I", sock.recv(4))[0]
resp = json.loads(sock.recv(length))
print(resp["result"]["ActiveKeyboardEngine"])
```

## Common operations

### Switch engine

```json
{"jsonrpc":"2.0","id":2,"method":"ActivateEngine","params":{"engine":"rime"}}
```

### Cycle to next engine

```json
{"jsonrpc":"2.0","id":3,"method":"NextEngine"}
```

### Reload config from disk

```json
{"jsonrpc":"2.0","id":4,"method":"ReloadConfig"}
```

### Shut down the daemon

```json
{"jsonrpc":"2.0","id":5,"method":"Stop"}
```

## Handle errors

Error responses look like this:

```json
{"jsonrpc":"2.0","id":2,"error":{"code":-32603,"message":"Failed to activate engine"}}
```

Check for the presence of `"error"` before reading `"result"`.

## Full Python example

```python
#!/usr/bin/env python3
import os, socket, struct, json

SOCK = os.path.expandvars("$XDG_RUNTIME_DIR/typio/daemon.sock")

def uds_call(method, params=None, req_id=1):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(SOCK)
    payload = {"jsonrpc": "2.0", "id": req_id, "method": method}
    if params:
        payload["params"] = params
    data = json.dumps(payload).encode()
    sock.sendall(struct.pack(">I", len(data)) + data)
    length = struct.unpack(">I", sock.recv(4))[0]
    resp = json.loads(sock.recv(length))
    sock.close()
    return resp

# Query active engine
r = uds_call("GetAll")
print("Active:", r["result"]["ActiveKeyboardEngine"])

# Switch engine
r = uds_call("ActivateEngine", {"engine": "rime"}, req_id=2)
print("OK" if "result" in r else r["error"]["message"])
```

## See also

- [IPC Protocol Reference](../reference/ipc-protocol.md) — Complete method and property tables, error codes, and wire-format details.
- [D-Bus Interface Reference](../reference/dbus-interface.md) — Legacy D-Bus transport (still supported for backward compatibility).

# Typio librime Integration

## 1. Architecture Overview

Typio integrates librime via a **plugin engine**. The Rime engine lives in `src/engines/rime/`, compiles to a shared library `engines/rime.so`, and is loaded at runtime by Typio's engine manager.

```
┌─────────────────────────────────────┐
│  Typio Daemon (Wayland IME)         │
│  ├─ typio-core (engine manager)     │
│  │   └─ loads engines/rime.so       │
│  └─ Wayland protocol frontend       │
└──────────────┬──────────────────────┘
               │ TypioEngineBaseOps + TypioKeyboardEngineOps API
               ▼
┌─────────────────────────────────────┐
│  Rime Engine Plugin                 │
│  ├─ rime_engine.c   (entry point)   │
│  ├─ rime_session.c  (session mgmt)  │
│  ├─ rime_sync.c     (context sync)  │
│  ├─ rime_deploy.c   (deployment)    │
│  ├─ rime_mode.c     (mode switching)│
│  ├─ rime_key.c      (key handling)  │
│  ├─ rime_config.c   (configuration) │
│  ├─ path_expand.c   (path helpers)  │
│  └─ linked against librime.so       │
└──────────────┬──────────────────────┘
               │ Rime C API (rime_api.h)
               ▼
┌─────────────────────────────────────┐
│  librime                            │
│  ├─ schema management               │
│  ├─ session management              │
│  ├─ key processing (process_key)    │
│  └─ candidate / commit handling     │
└─────────────────────────────────────┘
```

## 2. Core Data Structures

### 2.1 Engine Global State (TypioRimeState)

Each Rime engine instance (`TypioEngine`) stores its `user_data` as a `TypioRimeState`:

```c
typedef struct TypioRimeState {
    RimeApi *api;              // librime API function table pointer
    RimeTraits traits;         // init traits (data dirs, distribution info)
    TypioRimeConfig config;    // config (shared_data_dir, user_data_dir, schema)
    bool initialized;          // whether initialize() has completed
    bool maintenance_done;     // whether deployment has finished
    uint32_t deploy_id;        // deployment version, used for session invalidation
    /* Cached from the last notification callback */
    bool ascii_mode;           // cached ascii_mode from option notification
    bool ascii_mode_known;     // whether ascii_mode has been received
} TypioRimeState;
```

### 2.2 Input Context Session (TypioRimeSession)

**Key design**: Rime sessions belong to `TypioInputContext` (stored via property), not to transient focus state. Losing focus only clears the screen; the session survives until the context is destroyed, preserving runtime options such as `ascii_mode` across focus changes and engine switches.

```c
typedef struct TypioRimeSession {
    TypioRimeState *state;
    RimeSessionId session_id;    // librime session ID
    bool ascii_mode_known;       // whether we have read the current ascii_mode
    bool ascii_mode;             // cached ascii_mode value
    uint32_t deploy_id;          // deploy_id at session creation time
} TypioRimeSession;
```

### 2.3 Engine Capability Flags

```c
.capabilities = TYPIO_CAP_PREEDIT 
              | TYPIO_CAP_CANDIDATES 
              | TYPIO_CAP_PREDICTION 
              | TYPIO_CAP_LEARNING
```

Supports: preedit text, candidate list, prediction, and user dictionary learning.

## 3. Lifecycle Management

### 3.1 Initialization (`typio_rime_init`)

1. **Load config**: reads the `[engines.rime]` section from `typio.toml`
   - `shared_data_dir`: system Rime data directory (default `/usr/share/rime-data`)
   - `user_data_dir`: user data directory (default `~/.local/share/typio/rime`)
   - `schema`: default schema ID
2. **Ensure directory exists**: automatically creates `user_data_dir`
3. **Get API**: calls `rime_get_api()` to obtain the function table
4. **Log version**: calls `api->get_version()` and logs the linked librime version for troubleshooting
5. **Install notification handler**: calls `api->set_notification_handler()` before `setup()` so deploy and option events are delivered asynchronously
6. **Set Traits**:
   ```c
   traits.shared_data_dir = ...;
   traits.user_data_dir = ...;
   traits.distribution_name = "Typio";
   traits.app_name = "rime.typio";
   ```
7. **Initialize librime**: `setup()` → `initialize()`
8. **Trigger deployment**: if `build/default.yaml` does not exist, starts an async deployment

### 3.2 Destruction (`typio_rime_destroy`)

1. Calls `api->finalize()` to shut down librime
2. Frees config strings and the state structure

## 4. Session Management

### 4.1 Get / Create Session (`typio_rime_get_session`)

```c
TypioRimeSession *typio_rime_get_session(TypioEngine *engine,
                                         TypioInputContext *ctx,
                                         bool create);
```

Logic:
1. Look up an existing session from `ctx` property (key: `"rime.session"`)
2. **Deployment check**: if `session->deploy_id != state->deploy_id`, the session is stale; clean it up and recreate
3. If creation is requested and deployment is done:
   - `api->create_session()` to create a new session
   - `api->select_schema()` to apply the configured schema
   - `api->get_status()` to verify the schema was actually applied (logs mismatch)
   - Store in `ctx` property with destructor callback `typio_rime_free_session`
   - Sync initial mode state

### 4.2 Handling During Deployment

If librime is deploying (`maintenance_done` is false and `is_maintenance_mode()` returns true):
- `get_session(..., true)` returns `NULL`
- `process_key` shows a temporary preedit message `"… Rime 正在部署"`
- Keys are marked as `HANDLED` to prevent input leakage
- When the async deployment finishes, the `"deploy"` / `"success"` notification sets `maintenance_done = true`

## 5. Key Event Processing

### 5.1 Key Translation

Typio's `TypioKeyEvent` is translated into librime `process_key` arguments:

```c
// Modifier mapping
Typio MOD_SHIFT   → RIME_SHIFT_MASK (1 << 0)
Typio MOD_CTRL    → RIME_CONTROL_MASK (1 << 2)
Typio MOD_ALT     → RIME_MOD1_MASK (1 << 3)
Typio MOD_SUPER   → RIME_MOD4_MASK (1 << 6)
Typio MOD_CAPSLOCK→ RIME_LOCK_MASK (1 << 1)
Typio MOD_NUMLOCK → RIME_MOD2_MASK (1 << 4)

// Release event
TYPIO_EVENT_KEY_RELEASE → TYPIO_RIME_RELEASE_MASK (1 << 30)
```

### 5.2 Processing Flow (`typio_rime_process_key`)

```
1. Escape key: if there is preedit / candidates, reset and consume
2. Get / create session (if deploying, show waiting message)
3. Call api->process_key(session_id, keysym, mask)
4. Mode detection (fallback): if Shift is pressed, poll get_option("ascii_mode")
   and notify mode change.  Primary mode changes come from the notification
   handler ("option" / "ascii_mode" events).
5. If handled:
   a. flush_commit() — retrieve committed text and commit to Typio
   b. sync_context() — sync preedit and candidates to TypioInputContext
6. Return: COMMITTED / COMPOSING / HANDLED / NOT_HANDLED
```

## 6. Candidate & Preedit Synchronization

### 6.1 Context Sync (`typio_rime_sync_context`)

Extracts data from librime's `RimeContext`:

**Preedit text**:
```c
if (rime_context.composition.preedit) {
    TypioPreeditSegment segment = {
        .text = preedit,
        .format = TYPIO_PREEDIT_UNDERLINE,
        .cursor_pos = rime_context.composition.cursor_pos,
    };
    typio_input_context_set_preedit(ctx, &preedit);
}
```

**Candidate list**:
- Supports `select_keys` (custom select keys) and `select_labels` (custom labels)
- Generates `TypioCandidateList`, containing: text, comment, label, page, selected, etc.
- Small pages (≤10) use stack allocation; large pages use heap allocation

### 6.2 Performance Optimizations

Implements **dual-path synchronization**:

1. **Selection-only change**: if preedit hasn't changed and only the highlighted candidate moved, directly call `set_candidate_selection()`, skipping full candidate reconstruction
2. **Full sync**: when preedit or candidate content changes, rebuild everything

Slow-sync detection: if sync takes ≥ 8 ms, log debug info (session_id, candidate count, page number, etc.).

### 6.3 Commit Text (`typio_rime_flush_commit`)

```c
if (api->get_commit(session_id, &commit)) {
    if (commit.text && *commit.text) {
        typio_input_context_commit(ctx, commit.text);
    }
    api->free_commit(&commit);
}
```

## 7. Deployment & Config Reload

### 7.1 Deployment Mechanism

- **Auto-deployment**: on startup, if `build/default.yaml` is missing, triggers a full deployment
- **Notification-driven tracking**: `set_notification_handler` receives `"deploy"` / `"success"` or `"failure"` events, setting `maintenance_done` without polling `is_maintenance_mode()`
- **Explicit deployment**: when requested via D-Bus or control panel:
  1. `invalidate_generated_yaml()`: delete `.yaml` files under `build/` (librime tracks changes with second-level timestamps; multiple modifications within the same second require forced invalidation)
  2. `start_maintenance(full_check=true)`: start the librime deployment thread
  3. `deploy_id++`: mark all existing sessions as stale; they will be recreated on next use

### 7.2 Config Reload (`typio_rime_reload_config`)

```
1. If deployment was requested:
   - invalidate generated YAML
   - run maintenance (deploy)
2. Update schema config
3. Read [engines.rime] config section
   - changes to shared_data_dir / user_data_dir require Typio restart (warning log)
4. apply_runtime_config():
   - re-acquire session for the current focused context (create=true triggers recreation)
   - clear current composition
   - apply new schema
   - sync context
```

### 7.3 Environment Variables

- `TYPIO_RIME_SYNC_DEPLOY=1`: force synchronous (blocking) deployment, useful for debugging

## 8. Mode Switching

The Rime engine exposes two modes:

| Mode    | mode_class | Label | Icon                |
|---------|-----------|-------|---------------------|
| Chinese | `NATIVE`  | 中    | `typio-rime`        |
| ASCII   | `LATIN`   | A     | `typio-rime-latin`  |

- Mode is determined by Rime's `ascii_mode` option
- **Notification-driven** (primary): the notification handler receives `"option"` / `"ascii_mode"` events from librime and caches the value in `TypioRimeState`.  Mode changes are pushed to Typio via `typio_instance_notify_mode()` immediately.
- **Shift fallback**: if a notification was missed (e.g. older librime or race during handler registration), Shift key detection in `process_key` polls `get_option("ascii_mode")` as a fallback.
- **Explicit toggle**: set `ascii_mode` option via `set_mode("ascii" / "chinese")`
- **Persistence**: mode state is preserved for the lifetime of the session

## 9. Focus & Lifecycle Events

| Event      | Behavior                                                                 |
|-----------|--------------------------------------------------------------------------|
| `focus_in` | Get / create session, sync current context state                         |
| `focus_out`| `reset()` — clear screen, but keep the session                           |
| `reset`    | `clear_composition()` + clear Typio preedit / candidates + restore mode notification |

## 10. Interaction Points with the Typio Framework

### 10.1 Calling Typio APIs

The Rime engine, as a plugin, interacts with Typio core via:

**Input / Output**:
- `typio_input_context_commit(ctx, text)` — commit text
- `typio_input_context_set_preedit(ctx, preedit)` — set preedit
- `typio_input_context_set_candidates(ctx, list)` — set candidates
- `typio_input_context_set_candidate_selection(ctx, index)` — update selection only
- `typio_input_context_clear_preedit / clear_candidates` — clear

**Context properties**:
- `typio_input_context_get_property(ctx, key)` — retrieve session
- `typio_input_context_set_property(ctx, key, data, destructor)` — store session

**Mode notification**:
- `typio_instance_notify_mode(instance, mode)` — notify mode change

**Config retrieval**:
- `typio_instance_get_engine_config(instance, "rime")` — get engine config section
- `typio_instance_dup_rime_schema(instance)` — get persisted schema ID
- `typio_instance_rime_deploy_requested(instance)` — check if deployment was requested

### 10.2 Called by Typio

Registered via the dual-vtable architecture (`TypioEngineBaseOps` + `TypioKeyboardEngineOps`):

```c
static const TypioEngineBaseOps typio_rime_base_ops = {
    .init = typio_rime_init,
    .destroy = typio_rime_destroy,
    .focus_in = typio_rime_focus_in,
    .focus_out = typio_rime_focus_out,
    .reset = typio_rime_reset,
    .reload_config = typio_rime_reload_config,
};

static const TypioKeyboardEngineOps typio_rime_keyboard_ops = {
    .process_key = typio_rime_process_key,
    .get_mode = typio_rime_get_mode,
    .set_mode = typio_rime_set_mode,
};
```

This design separates mandatory lifecycle operations from keyboard-specific callbacks, enforced at compile time rather than runtime NULL checks.

### 10.3 Build Integration

`src/engines/rime/CMakeLists.txt`:

```cmake
add_library(typio-engine-rime MODULE
    path_expand.c
    rime_engine.c
)

target_link_libraries(typio-engine-rime PRIVATE
    typio-core
    ${RIME_LIBRARIES}
)
```

- Compiled as `MODULE` (plugin `.so`)
- Installed to `${TYPIO_INSTALL_ENGINEDIR}` (default `${prefix}/lib/typio/engines/`)
- Discovers and links librime via `pkg_check_modules(RIME rime)`

## 11. librime Documentation & Links

### Official Resources

| Resource                 | Link                                                              |
|--------------------------|-------------------------------------------------------------------|
| **librime repository**   | https://github.com/rime/librime                                   |
| **librime API docs**     | https://github.com/rime/librime/wiki/RimeApi                      |
| **Rime schema collection**| https://github.com/rime/plum                                     |
| **Rime official Wiki**   | https://github.com/rime/home/wiki                                 |
| **librime header**       | https://github.com/rime/librime/blob/master/src/rime_api.h        |

### Key API Notes

- `RimeApi`: function table struct; all librime operations go through this table.  In librime 1.0+ every function pointer is guaranteed to be non-NULL after `setup()`, so defensive NULL checks are unnecessary.
- `RimeTraits`: initialization parameters, including data directories, distribution name, etc.
- `RimeSessionId`: session identifier; independent per input context
- `RimeContext`: current composition state (preedit, candidates, menu, etc.)
- `RimeCommit`: committed text
- `RimeStatus`: current schema and option state (schema_id, is_ascii_mode, etc.)
- `process_key`: process a single key press
- `select_schema`: switch input schema
- `set_option` / `get_option`: runtime options (e.g., `ascii_mode`)
- `set_notification_handler`: async deploy / option event delivery (replaces polling)
- `get_version`: linked librime version string (logged on init for troubleshooting)
- `get_status` / `free_status`: verify schema selection and read option state
- `highlight_candidate_on_current_page`: programmatic candidate selection
- `delete_candidate_on_current_page`: remove a candidate from user dictionary
- `start_maintenance` / `is_maintenance_mode`: deployment management

### Internal Typio References

| Document              | Path                                    | Description                        |
|-----------------------|-----------------------------------------|------------------------------------|
| Engine integration guide | `docs/how-to/integrate-engine.md`    | How to add a new engine            |
| Engine reference      | `docs/reference/engines.md`             | All engines' descriptions & config |
| Engine API            | `docs/reference/api/engine.md`          | Full `TypioEngineBaseOps` / `TypioKeyboardEngineOps` definition |
| Architecture overview | `docs/explanation/architecture-overview.md` | Typio overall architecture     |

## 12. File List

```
src/engines/rime/
├── CMakeLists.txt       # build config (MODULE plugin)
├── rime_internal.h      # shared data structures, constants, and cross-module API
├── rime_utils.c         # small utility helpers (monotonic time, directory creation)
├── rime_config.c        # configuration loading and cleanup
├── rime_deploy.c        # deployment and maintenance management
├── rime_session.c       # librime session lifecycle per input context
├── rime_sync.c          # preedit / candidate / commit synchronisation
├── rime_mode.c          # Chinese / ASCII mode switching and notification
├── rime_key.c           # key-event translation (modifiers, special keys)
├── rime_engine.c        # engine entry point and dual-vtable registration
├── path_expand.c        # path expansion (~, $HOME, ${VAR})
└── path_expand.h        # path expansion header
```

The implementation is split into focused modules so each file has a single responsibility:

| File | Lines (approx.) | Responsibility |
|------|----------------|----------------|
| `rime_engine.c` | ~370 | Engine entry point, notification handler, `TypioEngineBaseOps` wiring |
| `rime_sync.c` | ~290 | Convert librime `RimeContext` → `TypioInputContext`, candidate actions |
| `rime_session.c` | ~120 | Create / destroy / validate `RimeSessionId`, schema selection with `get_status` |
| `rime_deploy.c` | ~120 | Trigger and track librime deployment |
| `rime_mode.c` | ~90 | ASCII ↔ Chinese mode detection and notification |
| `rime_config.c` | ~80 | Load `[engines.rime]` settings from Typio config |
| `rime_key.c` | ~35 | Modifier mask translation `TypioKeyEvent` → librime |
| `rime_utils.c` | ~40 | `mkdir -p`, path existence, YAML suffix check |

This modular layout keeps the engine consistent with Typio's project-wide convention: `basic.c` (~250 lines) and `mozc_engine.cc` are also organised by lifecycle → input → output → config.

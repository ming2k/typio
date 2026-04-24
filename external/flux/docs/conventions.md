# Conventions

This document is the single source of truth for coding style,
naming, API shape, and the idioms that should be consistent across
every file in the codebase.

## Language

- **C11**. No C++, no compiler extensions except `__builtin_*` for
  math intrinsics and the `__attribute__((visibility("hidden")))` /
  `__attribute__((format(...)))` GCC/Clang attributes.
- `_Generic` and VLAs are allowed; alloca is not.
- All source files compile clean at `-Wall -Wextra` with no
  suppression pragmas. Disable noisy-but-harmless warnings only at
  the meson project level, not per-file.

## Naming

| Entity | Convention | Example |
|---|---|---|
| Public types | `fx_snake_case` | `fx_canvas`, `fx_color_space` |
| Public functions | `fx_verb_noun` | `fx_surface_acquire`, `fx_path_line_to` |
| Public enumerators | `FX_SCREAMING_SNAKE` | `FX_BLEND_SRC_OVER`, `FX_CAP_ROUND` |
| Public macros | `FX_SCREAMING_SNAKE` | `FX_API`, `FX_MAX_FRAMES_IN_FLIGHT` |
| Internal functions | `fx_verb_noun` (no `FX_API`) | `fx_swapchain_build` |
| Internal types | `fx_snake_case` | `fx_sc_image`, `fx_draw_op` |
| Local variables | `snake_case` | `image_count`, `fr` |
| Struct members | `snake_case` | `surface_format`, `needs_recreate` |

Abbreviations are acceptable where they are unambiguous in context:
`fr` for `fx_frame`, `s` for `fx_surface`, `ctx` for `fx_context`,
`c` for `fx_canvas`, `p` for `fx_paint` or `fx_path` (distinguish by
argument position or local alias). Avoid abbreviating names that would
require a reader to look up the meaning.

## API design rules

1. **Opaque types everywhere.** Public structs are never exposed.
   Consumers interact only through functions. This lets the internal
   layout change without breaking ABI.

2. **Own vs borrow is explicit in function names / docs.** If a
   function takes ownership, it says so. If a pointer is borrowed, the
   caller's docs say "borrowed: must remain alive until X."

3. **Null-safe draw calls.** All `fx_draw_*` and `fx_canvas` state
   setters silently no-op when passed `NULL`. This lets a frame loop
   degrade gracefully when a resource fails to load.

4. **No hidden allocation in hot paths.** After the first few frames,
   all per-frame data is written into pre-allocated ring buffers. Any
   function that allocates in a steady-state frame is a bug.

5. **No return codes on draw calls.** Drawing to a canvas does not
   return a result. Errors (out of ring buffer space, atlas full) are
   handled internally — typically by growing the resource — and
   logged, not propagated.

6. **Creation functions return NULL on failure, never partial objects.**
   Callers check once for NULL then treat the object as valid.

7. **Enums have no trailing `_COUNT` sentinel** unless there is a
   functional reason to count at runtime. Use `sizeof(arr)/sizeof(*arr)`
   in switch tables and static arrays.

## Comments

- Write comments only when the **why** is non-obvious: a hidden
  constraint, a workaround for a driver bug, a subtle invariant.
- Never describe what the code does — well-named identifiers do that.
- No function-level docstring blocks for internal functions. The
  declaration in `internal.h` can carry a one-line note if the
  semantics are not obvious from the name.
- Public API functions in `flux.h` / `flux_text.h` may have a
  one-line comment explaining the lifetime contract if it is
  non-trivial (e.g., borrowed pointers).

## Error handling

```c
// Creating something: return NULL, log before returning
fx_surface *fx_surface_create_wayland(...) {
    ...
    if (something_failed) {
        FX_LOGE(ctx, "meaningful message: %d", code);
        free(s);
        return NULL;
    }
}

// Vulkan calls: FX_CHECK_VK logs and continues
FX_CHECK_VK(ctx, vkQueueSubmit(queue, 1, &si, fence));

// Draw calls: silent no-op on null inputs
void fx_clear(fx_canvas *c, fx_color color) {
    if (!c) return;
    ...
}
```

Never use `assert` for input validation on paths reachable from
user code. Use `if (!x) { FX_LOGE...; return; }`. Reserve `assert`
for internal invariants that should be impossible to violate at
runtime (loop postconditions, etc.).

## Logging

```c
FX_LOGE(ctx, "format %s", arg);   // error — always printed
FX_LOGW(ctx, "format %s", arg);   // warning
FX_LOGI(ctx, "format %s", arg);   // info — startup events, sizes
FX_LOGD(ctx, "format %s", arg);   // debug — per-frame events
```

In hot-path code, wrap `FX_LOGD` calls behind a compile-time check:

```c
#ifdef FX_DEBUG
    FX_LOGD(ctx, "ring grew to %zu bytes", new_size);
#endif
```

The default log sink writes to `stderr`. Users can supply their own
`fx_log_fn` through `fx_context_desc`.

## Memory

- Use `calloc` for structs that will be partially initialized.
- Match every `malloc/calloc/realloc` with a `free` in the same
  ownership scope. If lifetime crosses scopes, say so with a comment.
- Ring buffers are persistently mapped (HOST_VISIBLE + COHERENT).
  Write through the mapped pointer; never call `vkFlushMappedMemoryRanges`
  on coherent memory.
- Do not `free` GPU-resident memory with `free`. GPU memory is
  returned via `vkFreeMemory`.

## Threading

flux is **single-threaded per context**: recording, submission, and
presentation all happen on the context-owning thread. No internal
mutexes. This is explicit: the header says it, the docs say it, and
code that would require a lock should not be written until the
threading model is extended.

## File organization

- Each `.c` file owns one clear domain. Avoid "utils.c" files that
  become catch-alls.
- Internal declarations shared across two or more TUs live in
  `src/internal.h`. Internal declarations used by only one TU stay
  in that `.c` file as `static`.
- Vulkan-specific code lives under `src/vk/`. Non-Vulkan code (`path`,
  `stroker`, `tess`, `text`, `atlas`) must not include `<vulkan/vulkan.h>`
  directly — they depend only on the types in `internal.h`.

## Build constraints

- No optional features via `#ifdef` inside implementation files. Use
  meson build options to exclude files from the build entirely when a
  feature is disabled. Preprocessor guards are reserved for
  platform-detection (e.g. `VK_USE_PLATFORM_WAYLAND_KHR`) that
  Vulkan headers require.
- Shader compilation is a build-time step. No runtime SPIR-V
  compilation in the library. No strings containing GLSL in C files.

## Versioning

flux follows **semantic versioning**. The `meson.build` version is the
source of truth. ABI compatibility is maintained within a major version.
Adding a new `fx_*` symbol is a minor bump; breaking an existing symbol
is a major bump.

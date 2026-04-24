# Contributing

## Before you start

- Read [conventions.md](conventions.md) end-to-end. It is the
  contract that every PR is reviewed against.
- Check the [roadmap](roadmap.md) to understand which phase a feature
  belongs to. Do not implement phase-2 work in a phase-1 PR.
- For significant changes (new subsystem, API extension, alternative
  algorithm), open an issue first to discuss the design before writing
  code.

## Development workflow

1. Fork and clone the repository.
2. Create a feature branch off `main`:
   ```sh
   git checkout -b feat/my-change
   ```
3. Make your changes. Write or update tests as appropriate.
4. Run the full suite:
   ```sh
   FX_ENABLE_VALIDATION=1 meson test -C build
   ```
5. Open a pull request against `main`. Fill in the template below.

## Commit style

Each commit should be a single logical change. Commit messages follow
the conventional-commits format:

```
<type>(<scope>): <imperative, short description>

<optional longer explanation — why, not what>
<break paragraphs with blank lines>
```

Types:

| Type | When |
|---|---|
| `feat` | new behaviour visible to the API consumer |
| `fix` | bug fix (reference the issue number if one exists) |
| `perf` | performance improvement with no API change |
| `refactor` | internal restructuring, no behaviour change |
| `test` | adding or fixing tests only |
| `docs` | documentation only |
| `build` | meson, CI, dependencies |
| `chore` | maintenance that doesn't fit elsewhere |

Scope is optional and should match a subsystem name: `stroker`,
`tess`, `swapchain`, `atlas`, `text`, etc.

Examples:

```
feat(stroker): add round cap support

fix(swapchain): handle VK_SUBOPTIMAL_KHR on first present

test(tess): add concave polygon coverage assertion

docs(api): document fx_surface_resize lifetime contract
```

Keep the subject line under 72 characters. Do not end it with a
period. Do not add "Co-authored-by" attribution lines.

## Pull request template

```markdown
## Summary

One paragraph describing what this change does and why.

## Changes

- file.c: describe the change
- other_file.c: describe the change

## Testing

- [ ] meson test passes with FX_ENABLE_VALIDATION=1
- [ ] golden images updated if rendering changed (diff attached)
- [ ] new unit tests added for new code paths
- [ ] examples run clean

## Notes for reviewers

Any gotchas, open questions, or alternative approaches considered.
```

## Review checklist

The reviewer checks:

**Correctness**
- [ ] Vulkan synchronization: correct semaphores, fences, pipeline stage masks.
- [ ] Memory lifetimes: no use-after-free, no missing `*_destroy` calls.
- [ ] Error paths: every `NULL` or non-`VK_SUCCESS` result is handled.

**API**
- [ ] No opaque type layouts leaked into public headers.
- [ ] Own vs borrow is clearly documented for new pointer arguments.
- [ ] Null-safe draw calls remain null-safe.
- [ ] No allocation on the hot path (steady-state frame loop).

**Conventions**
- [ ] Naming follows [conventions.md](conventions.md).
- [ ] Comments explain *why*, not *what*.
- [ ] No new `-Wall -Wextra` warnings.

**Testing**
- [ ] Test coverage for new code paths.
- [ ] Golden images reviewed by eye (not just "pixel-identical").
- [ ] Validation layers produce zero errors.

**Scope**
- [ ] PR is limited to its stated purpose.
- [ ] No phase-N+1 work sneaked into a phase-N PR.
- [ ] No partial implementations without a `/* TODO phase N */` marker.

## Adding a new `fx_draw_*` call (checklist)

For contributors implementing a new drawing primitive:

1. Add the declaration to `include/flux/flux.h` with a lifetime
   comment if any argument is borrowed.
2. Append a `draw_op` in `src/canvas.c` — no GPU work.
3. Add the tessellation / geometry emit path in `src/tess.c` or
   `src/stroker.c`.
4. Wire the `pipeline_key` into `src/batcher.c`.
5. Add a GLSL shader pair in `src/shaders/` and register it in
   `src/vk/pipeline.c`.
6. Add a golden-image test scene in `tests/test_render_golden.c`.
7. Update `docs/api.md` with an example code snippet.

## Adding a Vulkan extension

1. Check for the extension in `fx_device_init` (or instance creation).
2. Fall back gracefully if not present — never make it required.
3. Guard the use site with a runtime boolean (`ctx->has_<ext>`).
4. Document the extension in [vulkan-backend.md](vulkan-backend.md)
   under the relevant section.

## Compatibility

- Phase 1 targets **Vulkan 1.2** as the baseline. Do not use 1.3
  core features without a 1.2 fallback.
- Do not use compiler extensions outside GCC/Clang attributes that
  are already in the codebase.
- Do not add new dependencies without discussion. The dependency list
  in §6 of the design brief is intentional.

## Reporting bugs

Open a GitHub issue with:

- OS, compositor, GPU make/model, Vulkan driver version.
- Steps to reproduce (minimal example preferred).
- Observed vs expected output (screenshot or pixel diff if visual).
- Output of `FX_ENABLE_VALIDATION=1 <command>` if applicable.

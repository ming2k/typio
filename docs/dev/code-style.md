# Code Style

## Language versions

- C11 for C code
- C++17 where C++ is already required

## Formatting

- 4-space indentation
- Keep public API names in the `typio_*` / `Typio*` style already used by the repo
- Prefer small, direct functions over clever abstractions

## Documentation

- Document non-obvious behavior in headers or near complex state transitions
- Keep generated protocol and renderer details behind narrow module boundaries

## Design preferences

- Prefer local helpers and direct data flow over broad abstractions
- Keep module boundaries explicit

## Before submitting

- Build succeeds from a clean tree
- `ctest --test-dir build --output-on-failure` passes
- User-facing behavior is documented
- Any new engine or runtime assumptions are written down

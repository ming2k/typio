# Contributing

## Expectations

Every change should keep three things aligned:

- code
- tests
- docs

If one of those changes and the others do not, the patch is incomplete.

## Development Loop

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Style

- C11
- 4-space indentation
- keep public API names in the `typio_*` / `Typio*` style already used by the repo
- prefer small, direct functions over clever abstractions
- document non-obvious behavior in headers or near complex code paths

## Tests

Add or update tests for:

- bug fixes
- engine-manager behavior changes
- config parsing changes
- input-context or commit/preedit behavior changes

Current test binaries:

- `test_core`
- `test_config`
- `test_engine_manager`

## Pull Request Checklist

- build succeeds from a clean tree
- `ctest --test-dir build --output-on-failure` passes
- user-facing behavior is documented
- any new engine or runtime assumptions are written down

## Useful References

- [Testing Locally](testing.md)
- [Development Guide](development.md)
- [Architecture](architecture.md)
- [Creating Engines](creating-engines.md)

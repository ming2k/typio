# Contributing

Thank you for your interest in improving Typio.

Every change should keep three things aligned:

- code
- tests
- docs

If one of those changes and the others do not, the patch is incomplete.

## Quick start

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Developer documentation

- [Developer Setup](docs/dev/setup.md)
- [Testing](docs/dev/testing.md)
- [Code Style](docs/dev/code-style.md)
- [Project Layout](docs/dev/project-layout.md)
- [Maintenance Manual](docs/dev/maintenance.md)

## Pull Request Checklist

- [ ] Build succeeds from a clean tree
- [ ] `ctest --test-dir build --output-on-failure` passes
- [ ] User-facing behavior is documented
- [ ] Any new engine or runtime assumptions are written down
- [ ] `CHANGELOG.md` is updated
- [ ] If architectural change: ADR added

## Questions?

Open an issue or discussion on the project repository.

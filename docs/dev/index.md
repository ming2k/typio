# Developer Documentation

This section is for contributors who will modify Typio source code. **If you only want to use Typio**, see the [Getting Started tutorial](../tutorials/01-getting-started.md) instead.

- [Setup](setup.md) — Local development environment and build options
- [Testing](testing.md) — How to run and write tests
- [Code Style](code-style.md) — C/C++ conventions and project norms
- [Project Layout](project-layout.md) — Tour of the source tree
- [Maintenance Manual](maintenance.md) — Rules for the Wayland keyboard pipeline
- [Release Process](release-process.md) — This section is under construction

## Before submitting a PR

- [ ] All tests pass (`ctest --test-dir build --output-on-failure`)
- [ ] Build succeeds from a clean tree
- [ ] Sanitizer builds pass if the change touches memory or lifetime boundaries
- [ ] User-facing behavior is documented
- [ ] `CHANGELOG.md` is updated
- [ ] If the change is architectural: an ADR is added or updated

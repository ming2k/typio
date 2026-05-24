// Build script for typio-core.
//
// The crate reads several env vars at compile time via option_env!:
//   - TYPIO_VERSION              — release version label
//   - TYPIO_BUILD_SOURCE_LABEL   — build-source tag for diagnostics
//   - TYPIO_DEFAULT_ENGINE_DIR   — absolute path to the system-wide engine
//                                  install dir, baked in as a fallback so
//                                  the daemon finds installed plugins even
//                                  when the per-user data dir is empty
//   - TYPIO_ENGINE_DIR           — legacy compile-time override (also
//                                  honored as a runtime env var)
//
// Cargo doesn't automatically rebuild when env vars referenced by macros
// change. Declare them here so a CMake reconfigure that changes the
// install prefix triggers a re-link instead of silently baking the old
// path.

fn main() {
    println!("cargo:rerun-if-env-changed=TYPIO_VERSION");
    println!("cargo:rerun-if-env-changed=TYPIO_BUILD_SOURCE_LABEL");
    println!("cargo:rerun-if-env-changed=TYPIO_DEFAULT_ENGINE_DIR");
    println!("cargo:rerun-if-env-changed=TYPIO_ENGINE_DIR");
}

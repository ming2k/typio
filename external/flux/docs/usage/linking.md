# Linking

flux can be consumed as a shared library, a static library, or a Meson
subproject. The public C headers are installed under `include/flux/`.

## Dynamic Link

The default Meson build produces a shared library:

```sh
meson setup build
meson compile -C build
meson install -C build
```

Compile an application against an installed flux:

```sh
cc app.c -o app $(pkg-config --cflags --libs flux)
```

At runtime the dynamic loader must be able to find `libflux.so`, either through
the install prefix, `ldconfig`, `LD_LIBRARY_PATH`, or an rpath chosen by the
application build.

## Static Link

Build and install a static flux library:

```sh
meson setup build-static -Ddefault_library=static
meson compile -C build-static
meson install -C build-static
```

Compile with static dependency flags:

```sh
cc app.c -o app $(pkg-config --cflags --libs --static flux)
```

Static linking pulls more transitive dependencies into the application link
line, including Vulkan, FreeType, HarfBuzz, pthreads, math, and optional
Wayland libraries when Wayland support is enabled.

## Meson Subproject

If flux is vendored as a Meson subproject, consume its dependency object:

```meson
flux_proj = subproject('flux')
flux_dep = flux_proj.get_variable('flux_dep')

executable('app', 'app.c', dependencies : [flux_dep])
```

Use this when the application wants to control whether flux is built as static
or shared from the top-level `default_library` option.

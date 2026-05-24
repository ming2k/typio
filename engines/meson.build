# ── Engines ───────────────────────────────────────────────────────────────
# basic engine is built statically into typio-core (see core/meson.build)

if build_rime_engine
    subdir('rime')

    rime_engine = shared_module('rime',
        rime_sources,
        dependencies: [typio_core_dep, rime_dep],
        include_directories: [
            include_directories('rime'),
            include_directories('../core/include'),
        ],
        install: true,
        install_dir: enginedir,
        build_rpath: '$ORIGIN/..',
        install_rpath: '$ORIGIN/..',
    )
endif

if build_mozc_engine
    subdir('mozc')

    mozc_engine = shared_module('mozc',
        mozc_sources, proto_src,
        dependencies: [typio_core_dep],
        include_directories: [
            include_directories('mozc'),
            include_directories('../core/include'),
        ],
        install: true,
        install_dir: enginedir,
        build_rpath: '$ORIGIN/..',
        install_rpath: '$ORIGIN/..',
    )
endif

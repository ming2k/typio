# Rime Engine
if(BUILD_RIME_ENGINE)
    add_subdirectory(rime)
endif()

# Mozc Engine
if(BUILD_MOZC_ENGINE)
    add_subdirectory(mozc)
endif()

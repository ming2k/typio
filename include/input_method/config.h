#ifndef INPUT_METHOD_CONFIG_H
#define INPUT_METHOD_CONFIG_H

#define INPUT_METHOD_VERSION_MAJOR 0
#define INPUT_METHOD_VERSION_MINOR 1
#define INPUT_METHOD_VERSION_PATCH 0

#define INPUT_METHOD_VERSION_STRING "0.1.0"

// Platform-specific configurations
#if defined(_WIN32)
    #define IM_EXPORT __declspec(dllexport)
    #define IM_IMPORT __declspec(dllimport)
#else
    #define IM_EXPORT __attribute__((visibility("default")))
    #define IM_IMPORT
#endif

#if defined(INPUT_METHOD_BUILD_SHARED)
    #define IM_API IM_EXPORT
#elif defined(INPUT_METHOD_USE_SHARED)
    #define IM_API IM_IMPORT
#else
    #define IM_API
#endif

#endif // INPUT_METHOD_CONFIG_H 
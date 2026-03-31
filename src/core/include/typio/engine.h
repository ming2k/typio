/**
 * @file engine.h
 * @brief Input engine interface for Typio
 *
 * This file defines the engine interface that all input engines must implement.
 * Engines are loaded as plugins and provide input method functionality.
 */

#ifndef TYPIO_ENGINE_H
#define TYPIO_ENGINE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Engine metadata structure
 */
struct TypioEngineInfo {
    const char *name;           /* Unique engine identifier */
    const char *display_name;   /* Human-readable name */
    const char *description;    /* Engine description */
    const char *version;        /* Engine version string */
    const char *author;         /* Engine author */
    const char *icon;           /* Icon name or path */
    const char *language;       /* Primary language code (e.g., "zh_CN") */
    TypioEngineType type;       /* Engine type */
    uint32_t capabilities;      /* Capability flags */
    int api_version;            /* Required API version */
};

/**
 * @brief Engine operations structure - virtual table for engine implementations
 */
typedef struct TypioEngineOps {
    /* Lifecycle */
    TypioResult (*init)(TypioEngine *engine, TypioInstance *instance);
    void (*destroy)(TypioEngine *engine);

    /* Focus handling */
    void (*focus_in)(TypioEngine *engine, TypioInputContext *ctx);
    void (*focus_out)(TypioEngine *engine, TypioInputContext *ctx);
    void (*reset)(TypioEngine *engine, TypioInputContext *ctx);

    /* Key event handling - returns processing status (Interception/Composition/Commit) */
    TypioKeyProcessResult (*process_key)(TypioEngine *engine, TypioInputContext *ctx,
                                         const TypioKeyEvent *event);

    /* Voice input (optional, for voice engines) */
    TypioResult (*voice_start)(TypioEngine *engine, TypioInputContext *ctx);
    TypioResult (*voice_stop)(TypioEngine *engine, TypioInputContext *ctx);
    TypioResult (*voice_process)(TypioEngine *engine, TypioInputContext *ctx,
                                  const void *audio_data, size_t size,
                                  int sample_rate, int channels);

    /* Configuration */
    TypioResult (*get_config)(TypioEngine *engine, TypioConfig **config);
    TypioResult (*set_config)(TypioEngine *engine, const TypioConfig *config);
    TypioResult (*reload_config)(TypioEngine *engine);

    /* State query */
    const char *(*get_preedit)(TypioEngine *engine, TypioInputContext *ctx);
    TypioCandidateList *(*get_candidates)(TypioEngine *engine, TypioInputContext *ctx);

    /* Dynamic icon (optional) — returns current icon name based on engine state */
    const char *(*get_status_icon)(TypioEngine *engine, TypioInputContext *ctx);

    /**
     * @brief Return the current engine sub-mode (optional).
     *
     * When implemented, the framework calls this after focus-in and engine
     * switches to obtain a structured description of the engine's current
     * mode.  The returned pointer must remain valid until the next call to
     * any engine operation on the same context.
     *
     * Engines that implement get_mode do not need to implement
     * get_status_icon — the framework derives the icon from
     * TypioEngineMode::icon_name.  If both are provided, get_mode takes
     * precedence.
     */
    const TypioEngineMode *(*get_mode)(TypioEngine *engine, TypioInputContext *ctx);
} TypioEngineOps;

/**
 * @brief Engine instance structure
 */
struct TypioEngine {
    const TypioEngineInfo *info;    /* Engine metadata */
    const TypioEngineOps *ops;      /* Engine operations */
    TypioInstance *instance;        /* Parent instance */
    void *user_data;                /* Engine-specific data */
    bool active;                    /* Whether engine is currently active */
    bool initialized;               /* Whether init has been called */
    char *config_path;              /* Path to engine configuration file */
};

/**
 * @brief Engine factory function type
 *
 * Each engine library must export a function with this signature named
 * "typio_engine_create" to create engine instances.
 */
typedef TypioEngine *(*TypioEngineFactory)(void);

/**
 * @brief Engine info function type
 *
 * Each engine library must export a function with this signature named
 * "typio_engine_get_info" to return engine metadata.
 */
typedef const TypioEngineInfo *(*TypioEngineInfoFunc)(void);

/* Macro to define engine entry points */
#define TYPIO_ENGINE_DEFINE(info_var, create_func) \
    const TypioEngineInfo *typio_engine_get_info(void) { \
        return &info_var; \
    } \
    TypioEngine *typio_engine_create(void) { \
        return create_func(); \
    }

/* Engine lifecycle functions */
TypioEngine *typio_engine_new(const TypioEngineInfo *info,
                               const TypioEngineOps *ops);
void typio_engine_free(TypioEngine *engine);

/* Engine utility functions */
const char *typio_engine_get_name(const TypioEngine *engine);
TypioEngineType typio_engine_get_type(const TypioEngine *engine);
uint32_t typio_engine_get_capabilities(const TypioEngine *engine);
bool typio_engine_has_capability(const TypioEngine *engine, TypioEngineCapability cap);
bool typio_engine_is_active(const TypioEngine *engine);
const char *typio_engine_get_config_path(const TypioEngine *engine);
void typio_engine_set_config_path(TypioEngine *engine, const char *path);

/* Engine data management */
void typio_engine_set_user_data(TypioEngine *engine, void *data);
void *typio_engine_get_user_data(const TypioEngine *engine);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_ENGINE_H */

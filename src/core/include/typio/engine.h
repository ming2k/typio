/**
 * @file engine.h
 * @brief Input engine interface for Typio
 *
 * This file defines the engine interface that all input engines must implement.
 * Engines are loaded as plugins and provide input method functionality.
 *
 * Design principles:
 *   1. Interface segregation — base ops (all engines) and keyboard ops
 *      (keyboard engines only) are separate structs.
 *   2. Type safety — the compiler, not runtime NULL checks, enforces which
 *      callbacks an engine must provide.
 *   3. Explicit contracts — every callback in base_ops is mandatory.
 *      Engines that do not need a particular behaviour provide a no-op.
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
    size_t struct_size;         /* sizeof(TypioEngineInfo) — ABI sentinel */
};

/* -------------------------------------------------------------------------- */
/* Engine operations                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Base operations — mandatory for every engine.
 *
 * All callbacks must be provided.  If an engine does not need a particular
 * behaviour it should supply a no-op implementation (e.g. a function that
 * simply returns TYPIO_OK or does nothing).
 */
typedef struct TypioEngineBaseOps {
    /**
     * @brief Initialise the engine instance.
     *
     * Allocate engine-specific state (usually via typio_engine_set_user_data)
     * and read configuration.  Called once before the engine becomes active.
     */
    TypioResult (*init)(TypioEngine *engine, TypioInstance *instance);

    /**
     * @brief Tear down the engine instance.
     *
     * Free all resources allocated by init().  Called once when the engine
     * is unloaded or the application exits.
     */
    void (*destroy)(TypioEngine *engine);

    /**
     * @brief The engine is no longer the active engine.
     *
     * Called when the user switches to a different engine.  Engines that
     * hold large in-memory resources (e.g. voice models) should free them
     * here to avoid memory bloat.  The engine remains registered and may
     * be reactivated later, in which case init() or focus_in() should
     * lazily reload the resource.
     */
    void (*deactivate)(TypioEngine *engine);

    /**
     * @brief The input context has gained focus.
     *
     * Engines should restore any visible UI state (preedit, candidates) that
     * was hidden by a previous focus_out.  Engines that do not have per-
     * context state may implement this as a no-op.
     */
    void (*focus_in)(TypioEngine *engine, TypioInputContext *ctx);

    /**
     * @brief The input context has lost focus.
     *
     * Engines should clear visible composition UI but preserve session state
     * (e.g. ascii_mode) so it survives focus churn.
     */
    void (*focus_out)(TypioEngine *engine, TypioInputContext *ctx);

    /**
     * @brief Reset engine state for the given context.
     *
     * Called on explicit reset (e.g. user pressed Escape).  Engines should
     * cancel any active composition and restore the default mode.
     */
    void (*reset)(TypioEngine *engine, TypioInputContext *ctx);

    /**
     * @brief Hot-reload engine-specific configuration.
     *
     * Called when the user edits typio.toml or issues a reload command.
     * Engines that do not support runtime config changes may return TYPIO_OK.
     */
    TypioResult (*reload_config)(TypioEngine *engine);
} TypioEngineBaseOps;

/**
 * @brief Keyboard-engine extension operations.
 *
 * Only keyboard engines (info->type == TYPIO_ENGINE_TYPE_KEYBOARD) populate
 * this vtable.  The framework verifies at registration time that
 * process_key is non-NULL; get_mode and set_mode are optional.
 */
typedef struct TypioKeyboardEngineOps {
    /**
     * @brief Process a single key event.
     *
     * The engine inspects the key and either consumes it (returning HANDLED,
     * COMPOSING, or COMMITTED) or passes it through (NOT_HANDLED).
     */
    TypioKeyProcessResult (*process_key)(TypioEngine *engine, TypioInputContext *ctx,
                                         const TypioKeyEvent *event);

    /**
     * @brief Return the current engine sub-mode.
     *
     * Used by the framework to display the current mode in the tray icon,
     * candidate popup, and D-Bus interface.  The returned pointer must remain
     * valid until the next call to any engine operation on the same context.
     *
     * Optional — if not provided the framework uses a generic icon.
     */
    const TypioEngineMode *(*get_mode)(TypioEngine *engine, TypioInputContext *ctx);

    /**
     * @brief Restore an engine-specific mode by @c mode_id.
     *
     * The provided @c mode_id should come from a previously reported
     * TypioEngineMode::mode_id value for the same engine.
     *
     * Optional — only needed when the engine exposes get_mode.
     */
    TypioResult (*set_mode)(TypioEngine *engine,
                            TypioInputContext *ctx,
                            const char *mode_id);
} TypioKeyboardEngineOps;

/**
 * @brief Voice-engine extension operations.
 *
 * Only voice engines (info->type == TYPIO_ENGINE_TYPE_VOICE) populate
 * this vtable.  The framework verifies at registration time that
 * process_audio is non-NULL.
 */
typedef struct TypioVoiceEngineOps {
    /**
     * @brief Run speech-to-text inference on a buffer of audio samples.
     *
     * @param engine    The voice engine instance.
     * @param samples   PCM float32 mono 16kHz audio data.
     * @param n_samples Number of samples.
     * @return Heap-allocated result text (caller frees), or NULL on failure.
     */
    char *(*process_audio)(TypioEngine *engine,
                           const float *samples, size_t n_samples);
} TypioVoiceEngineOps;

/* -------------------------------------------------------------------------- */
/* Engine instance                                                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Engine instance structure
 *
 * base_ops  is mandatory for every engine.
 * keyboard  is mandatory for keyboard engines and NULL for voice engines.
 * voice     is mandatory for voice engines and NULL for keyboard engines.
 */
struct TypioEngine {
    const TypioEngineInfo *info;        /* Engine metadata */
    const TypioEngineBaseOps *base_ops; /* Mandatory base operations */
    const TypioKeyboardEngineOps *keyboard; /* Keyboard vtable (may be NULL) */
    const TypioVoiceEngineOps *voice;   /* Voice vtable (may be NULL) */
    TypioInstance *instance;            /* Parent instance */
    void *user_data;                    /* Engine-specific data */
    bool active;                        /* Whether engine is currently active */
    bool initialized;                   /* Whether init has been called */
    char *config_path;                  /* Path to engine configuration file */
};

/* -------------------------------------------------------------------------- */
/* Entry points                                                               */
/* -------------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* -------------------------------------------------------------------------- */

TypioEngine *typio_engine_new(const TypioEngineInfo *info,
                               const TypioEngineBaseOps *base_ops,
                               const TypioKeyboardEngineOps *keyboard,
                               const TypioVoiceEngineOps *voice);
void typio_engine_free(TypioEngine *engine);

/* -------------------------------------------------------------------------- */
/* Utilities                                                                  */
/* -------------------------------------------------------------------------- */

const char *typio_engine_get_name(const TypioEngine *engine);
TypioEngineType typio_engine_get_type(const TypioEngine *engine);
uint32_t typio_engine_get_capabilities(const TypioEngine *engine);
bool typio_engine_has_capability(const TypioEngine *engine, TypioEngineCapability cap);
bool typio_engine_is_active(const TypioEngine *engine);
const char *typio_engine_get_config_path(const TypioEngine *engine);
void typio_engine_set_config_path(TypioEngine *engine, const char *path);
void typio_engine_set_user_data(TypioEngine *engine, void *data);
void *typio_engine_get_user_data(const TypioEngine *engine);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_ENGINE_H */

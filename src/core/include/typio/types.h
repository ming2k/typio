/**
 * @file types.h
 * @brief Common types and definitions for Typio
 */

#ifndef TYPIO_TYPES_H
#define TYPIO_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version info */
#define TYPIO_API_VERSION       1
#define TYPIO_ABI_MIN_VERSION   1
#define TYPIO_ABI_MAX_VERSION   1

/* ABI compatibility: struct size sentinel for runtime validation */
#define TYPIO_ENGINE_INFO_SIZE  sizeof(struct TypioEngineInfo)

/* Forward declarations */
typedef struct TypioInstance TypioInstance;
typedef struct TypioEngine TypioEngine;
typedef struct TypioEngineInfo TypioEngineInfo;
typedef struct TypioEngineManager TypioEngineManager;
typedef struct TypioInputContext TypioInputContext;
typedef struct TypioEvent TypioEvent;
typedef struct TypioKeyEvent TypioKeyEvent;
typedef struct TypioConfig TypioConfig;
typedef struct TypioCandidate TypioCandidate;
typedef struct TypioCandidateList TypioCandidateList;
typedef struct TypioPreedit TypioPreedit;

/* Result codes */
typedef enum {
    TYPIO_OK = 0,
    TYPIO_ERROR = -1,
    TYPIO_ERROR_INVALID_ARGUMENT = -2,
    TYPIO_ERROR_OUT_OF_MEMORY = -3,
    TYPIO_ERROR_NOT_FOUND = -4,
    TYPIO_ERROR_ALREADY_EXISTS = -5,
    TYPIO_ERROR_NOT_INITIALIZED = -6,
    TYPIO_ERROR_ENGINE_LOAD_FAILED = -7,
    TYPIO_ERROR_ENGINE_NOT_AVAILABLE = -8,
} TypioResult;

/* Engine types */
typedef enum {
    TYPIO_ENGINE_TYPE_KEYBOARD = 0,    /* Standard keyboard input */
    TYPIO_ENGINE_TYPE_VOICE = 1,       /* Voice input */
    TYPIO_ENGINE_TYPE_HANDWRITING = 2, /* Handwriting recognition */
    TYPIO_ENGINE_TYPE_CUSTOM = 100,    /* Custom engine types start here */
} TypioEngineType;

/* Engine capabilities */
typedef enum {
    TYPIO_CAP_NONE = 0,
    TYPIO_CAP_PREEDIT = (1 << 0),          /* Supports preedit text */
    TYPIO_CAP_CANDIDATES = (1 << 1),        /* Supports candidate list */
    TYPIO_CAP_PREDICTION = (1 << 2),        /* Supports word prediction */
    TYPIO_CAP_VOICE_INPUT = (1 << 3),       /* Supports voice input */
    TYPIO_CAP_CONTINUOUS_VOICE = (1 << 4),  /* Supports continuous voice */
    TYPIO_CAP_PUNCTUATION = (1 << 5),       /* Auto punctuation */
    TYPIO_CAP_LEARNING = (1 << 6),          /* User dictionary learning */
} TypioEngineCapability;

/* Event types */
typedef enum {
    TYPIO_EVENT_KEY_PRESS = 0,
    TYPIO_EVENT_KEY_RELEASE = 1,
    TYPIO_EVENT_FOCUS_IN = 2,
    TYPIO_EVENT_FOCUS_OUT = 3,
    TYPIO_EVENT_RESET = 4,
    TYPIO_EVENT_VOICE_START = 5,
    TYPIO_EVENT_VOICE_END = 6,
    TYPIO_EVENT_VOICE_DATA = 7,
    TYPIO_EVENT_COMMIT = 8,
    TYPIO_EVENT_CANDIDATE_SELECT = 9,
} TypioEventType;

/* Key processing result - explicitly modeling Interception, Composition, Commit */
typedef enum {
    TYPIO_KEY_NOT_HANDLED = 0, /* Not intercepted - pass to client application */
    TYPIO_KEY_HANDLED = 1,     /* Intercepted - handled internally (e.g. navigation) */
    TYPIO_KEY_COMPOSING = 2,   /* Intercepted - composition/preedit state updated */
    TYPIO_KEY_COMMITTED = 3,   /* Intercepted - text was committed */
} TypioKeyProcessResult;

/* Key modifiers */
typedef enum {
    TYPIO_MOD_NONE = 0,
    TYPIO_MOD_SHIFT = (1 << 0),
    TYPIO_MOD_CTRL = (1 << 1),
    TYPIO_MOD_ALT = (1 << 2),
    TYPIO_MOD_SUPER = (1 << 3),
    TYPIO_MOD_CAPSLOCK = (1 << 4),
    TYPIO_MOD_NUMLOCK = (1 << 5),
} TypioModifier;

/* Engine mode classification */
typedef enum {
    TYPIO_MODE_CLASS_NATIVE = 0,  /* Native script input (Chinese, Japanese, etc.) */
    TYPIO_MODE_CLASS_LATIN  = 1,  /* Latin/ASCII passthrough */
} TypioModeClass;

/**
 * @brief Structured engine mode reported by engines to the framework.
 *
 * Engines populate this to describe their current sub-mode.  The framework
 * uses @c mode_class for generic logic (e.g. indicator styling) and
 * forwards @c mode_id / @c display_label to control surfaces and D-Bus.
 */
typedef struct TypioEngineMode {
    TypioModeClass mode_class;    /* Broad classification */
    const char *mode_id;          /* Engine-specific mode identifier (e.g. "hiragana") */
    const char *display_label;    /* Short UI label (e.g. "あ", "A", "中") */
    const char *icon_name;        /* Icon name (freedesktop theme) */
} TypioEngineMode;

/* Callback types */
typedef void (*TypioCommitCallback)(TypioInputContext *ctx, const char *text, void *user_data);
typedef void (*TypioPreeditCallback)(TypioInputContext *ctx, const TypioPreedit *preedit, void *user_data);
typedef void (*TypioCandidateCallback)(TypioInputContext *ctx, const TypioCandidateList *candidates, void *user_data);
typedef void (*TypioEngineChangedCallback)(TypioInstance *instance, const TypioEngineInfo *engine, void *user_data);
typedef void (*TypioVoiceEngineChangedCallback)(TypioInstance *instance, const TypioEngineInfo *engine, void *user_data);
typedef void (*TypioStatusIconChangedCallback)(TypioInstance *instance, const char *icon_name, void *user_data);
typedef void (*TypioModeChangedCallback)(TypioInstance *instance, const TypioEngineMode *mode, void *user_data);

/* Log levels */
typedef enum {
    TYPIO_LOG_DEBUG = 0,
    TYPIO_LOG_INFO = 1,
    TYPIO_LOG_WARNING = 2,
    TYPIO_LOG_ERROR = 3,
} TypioLogLevel;

typedef void (*TypioLogCallback)(TypioLogLevel level, const char *message, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_TYPES_H */

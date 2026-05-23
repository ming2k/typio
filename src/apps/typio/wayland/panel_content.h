/**
 * @file panel_content.h
 * @brief Content model for the unified panel backend (Typio Panel System).
 *
 * This header defines the data-only description of what the panel should
 * display. It is intentionally free of Wayland, GPU, or rendering types so
 * that it can be constructed and tested without a display server.
 *
 * Phase 1: status indicator support for voice recording/processing.
 * Future phases will expand this to aggregate candidates, preedit decor,
 * toolbars, and other zones.
 */

#ifndef TYPIO_WL_PANEL_CONTENT_H
#define TYPIO_WL_PANEL_CONTENT_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations — defined in typio/input_context.h */
typedef struct TypioPreedit TypioPreedit;
typedef struct TypioCandidateList TypioCandidateList;

/**
 * @brief Severity level for a status indicator.
 */
typedef enum {
    TYPIO_PANEL_STATUS_INFO,     /**< Neutral information (e.g. "Processing...") */
    TYPIO_PANEL_STATUS_WARNING,  /**< Warning state (e.g. voice unavailable) */
    TYPIO_PANEL_STATUS_ERROR,    /**< Error state */
} TypioPanelStatusSeverity;

/**
 * @brief Status indicator content.
 *
 * Used when the panel has no candidates to show but needs to convey
 * transient state (voice recording, inference, errors, etc.).
 */
typedef struct {
    bool active;
    const char *message;                /**< Human-readable status text */
    TypioPanelStatusSeverity severity;
    bool animate;                       /**< Whether the indicator is animated */
    float progress;                     /**< 0.0–1.0 progress, or negative if unused */
} TypioPanelStatus;

/**
 * @brief Unified content description for the panel backend.
 *
 * The frontend aggregates data from multiple subsystems (input context,
 * voice service, etc.) into one of these structures and pushes it to the
 * panel composer.
 */
typedef struct {
    /* Input-context data (may be NULL if no active session) */
    struct {
        const TypioCandidateList *candidates;   /**< Candidate list or NULL */
        const TypioPreedit *preedit;            /**< Preedit segments or NULL */
        const char *mode_label;                 /**< Engine mode label or NULL */
    } input;

    /* Status indicator (voice recording, processing, errors) */
    TypioPanelStatus status;

    /* Content signature for cheap change detection */
    uint64_t signature;
} TypioPanelContent;

/**
 * @brief Zero-initialise a TypioPanelContent structure.
 */
static inline void typio_panel_content_init(TypioPanelContent *content) {
    if (content) {
        memset(content, 0, sizeof(*content));
    }
}

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_PANEL_CONTENT_H */

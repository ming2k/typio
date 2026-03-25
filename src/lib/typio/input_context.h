/**
 * @file input_context.h
 * @brief Input context for managing input state per client
 */

#ifndef TYPIO_INPUT_CONTEXT_H
#define TYPIO_INPUT_CONTEXT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Preedit text formatting
 */
typedef enum {
    TYPIO_PREEDIT_NONE = 0,
    TYPIO_PREEDIT_UNDERLINE = (1 << 0),
    TYPIO_PREEDIT_HIGHLIGHT = (1 << 1),
    TYPIO_PREEDIT_BOLD = (1 << 2),
    TYPIO_PREEDIT_ITALIC = (1 << 3),
} TypioPreeditFormat;

/**
 * @brief Preedit segment
 */
typedef struct TypioPreeditSegment {
    const char *text;           /* Segment text */
    uint32_t format;            /* Format flags */
} TypioPreeditSegment;

/**
 * @brief Preedit text structure
 */
struct TypioPreedit {
    TypioPreeditSegment *segments;  /* Array of segments */
    size_t segment_count;           /* Number of segments */
    int cursor_pos;                 /* Cursor position in characters */
};

/**
 * @brief Single candidate entry
 */
struct TypioCandidate {
    const char *text;           /* Candidate text */
    const char *comment;        /* Optional comment/annotation */
    const char *label;          /* Optional label (e.g., "1", "a") */
};

/**
 * @brief Candidate list structure
 */
struct TypioCandidateList {
    TypioCandidate *candidates;     /* Array of candidates */
    size_t count;                   /* Number of candidates */
    int page;                       /* Current page number */
    int page_size;                  /* Candidates per page */
    int total;                      /* Total number of candidates */
    int selected;                   /* Currently selected index */
    bool has_prev;                  /* Has previous page */
    bool has_next;                  /* Has next page */
    uint64_t content_signature;     /* Stable signature excluding selection */
};

/**
 * @brief Input context capabilities/hints
 */
typedef enum {
    TYPIO_CTX_CAP_PREEDIT = (1 << 0),       /* Client supports preedit */
    TYPIO_CTX_CAP_SURROUNDING = (1 << 1),   /* Client provides surrounding text */
    TYPIO_CTX_CAP_PASSWORD = (1 << 2),      /* Password input mode */
    TYPIO_CTX_CAP_MULTILINE = (1 << 3),     /* Multiline text input */
} TypioContextCapability;

/* Input context lifecycle */
TypioInputContext *typio_input_context_new(TypioInstance *instance);
void typio_input_context_free(TypioInputContext *ctx);

/* Focus management */
void typio_input_context_focus_in(TypioInputContext *ctx);
void typio_input_context_focus_out(TypioInputContext *ctx);
bool typio_input_context_is_focused(TypioInputContext *ctx);

/* Reset state */
void typio_input_context_reset(TypioInputContext *ctx);

/* Event processing */
bool typio_input_context_process_key(TypioInputContext *ctx,
                                      const TypioKeyEvent *event);

/* Candidate selection */
bool typio_input_context_select_candidate(TypioInputContext *ctx, int index);
bool typio_input_context_page_candidates(TypioInputContext *ctx, bool next);

/* Commit text to client */
void typio_input_context_commit(TypioInputContext *ctx, const char *text);

/* Preedit management */
void typio_input_context_set_preedit(TypioInputContext *ctx,
                                      const TypioPreedit *preedit);
const TypioPreedit *typio_input_context_get_preedit(TypioInputContext *ctx);
void typio_input_context_clear_preedit(TypioInputContext *ctx);

/* Candidate list management */
void typio_input_context_set_candidates(TypioInputContext *ctx,
                                         const TypioCandidateList *candidates);
const TypioCandidateList *typio_input_context_get_candidates(TypioInputContext *ctx);
void typio_input_context_set_candidate_selection(TypioInputContext *ctx, int selected);
void typio_input_context_clear_candidates(TypioInputContext *ctx);

/* Surrounding text */
void typio_input_context_set_surrounding(TypioInputContext *ctx,
                                          const char *text,
                                          int cursor_pos,
                                          int anchor_pos);
bool typio_input_context_get_surrounding(TypioInputContext *ctx,
                                          const char **text,
                                          int *cursor_pos,
                                          int *anchor_pos);
void typio_input_context_delete_surrounding(TypioInputContext *ctx,
                                             int offset, int length);

/* Context capabilities */
void typio_input_context_set_capabilities(TypioInputContext *ctx, uint32_t caps);
uint32_t typio_input_context_get_capabilities(TypioInputContext *ctx);

/* Callbacks */
void typio_input_context_set_commit_callback(TypioInputContext *ctx,
                                              TypioCommitCallback callback,
                                              void *user_data);
void typio_input_context_set_preedit_callback(TypioInputContext *ctx,
                                               TypioPreeditCallback callback,
                                               void *user_data);
void typio_input_context_set_candidate_callback(TypioInputContext *ctx,
                                                 TypioCandidateCallback callback,
                                                 void *user_data);

/* User data */
void typio_input_context_set_user_data(TypioInputContext *ctx, void *data);
void *typio_input_context_get_user_data(TypioInputContext *ctx);

/* Engine-specific property storage */
void typio_input_context_set_property(TypioInputContext *ctx,
                                       const char *key, void *value,
                                       void (*free_func)(void *));
void *typio_input_context_get_property(TypioInputContext *ctx, const char *key);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_INPUT_CONTEXT_H */

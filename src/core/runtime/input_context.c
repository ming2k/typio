/**
 * @file input_context.c
 * @brief Input context implementation
 */

#include "typio/input_context.h"
#include "typio/instance.h"
#include "typio/engine_manager.h"
#include "typio/engine.h"
#include "../utils/log.h"
#include "../utils/string.h"

#include <stdlib.h>
#include <string.h>

#define TYPIO_CANDIDATE_SIGNATURE_OFFSET UINT64_C(1469598103934665603)
#define TYPIO_CANDIDATE_SIGNATURE_PRIME UINT64_C(1099511628211)

/* Property entry for engine-specific state */
typedef struct PropertyEntry {
    char *key;
    void *value;
    void (*free_func)(void *);
    struct PropertyEntry *next;
} PropertyEntry;

struct TypioInputContext {
    TypioInstance *instance;

    /* Focus state */
    bool focused;

    /* Capabilities */
    uint32_t capabilities;

    /* Preedit state */
    TypioPreedit *preedit;
    TypioPreeditSegment *preedit_segments;
    size_t preedit_segment_capacity;

    /* Candidate state */
    TypioCandidateList *candidates;
    TypioCandidate *candidate_items;
    size_t candidate_capacity;

    /* Surrounding text */
    char *surrounding_text;
    int surrounding_cursor;
    int surrounding_anchor;

    /* Callbacks */
    TypioCommitCallback commit_callback;
    void *commit_user_data;

    TypioPreeditCallback preedit_callback;
    void *preedit_user_data;

    TypioCandidateCallback candidate_callback;
    void *candidate_user_data;

    /* User data */
    void *user_data;

    /* Property storage (for engine state) */
    PropertyEntry *properties;
};

/* External declaration */
extern void typio_instance_set_focused_context(TypioInstance *instance,
                                                TypioInputContext *ctx);

static uint64_t input_context_signature_bytes(uint64_t hash,
                                              const void *data,
                                              size_t size) {
    const unsigned char *bytes = data;

    if (!bytes) {
        return hash;
    }

    for (size_t i = 0; i < size; ++i) {
        hash ^= (uint64_t)bytes[i];
        hash *= TYPIO_CANDIDATE_SIGNATURE_PRIME;
    }

    return hash;
}

static uint64_t input_context_signature_string(uint64_t hash, const char *text) {
    hash = input_context_signature_bytes(hash, text ? text : "",
                                         text ? strlen(text) : 0);
    hash ^= UINT64_C(0xff);
    hash *= TYPIO_CANDIDATE_SIGNATURE_PRIME;
    return hash;
}

static uint64_t input_context_candidate_signature(const TypioCandidateList *candidates) {
    uint64_t hash = TYPIO_CANDIDATE_SIGNATURE_OFFSET;

    if (!candidates) {
        return 0;
    }

    hash = input_context_signature_bytes(hash, &candidates->count,
                                         sizeof(candidates->count));
    hash = input_context_signature_bytes(hash, &candidates->page,
                                         sizeof(candidates->page));
    hash = input_context_signature_bytes(hash, &candidates->page_size,
                                         sizeof(candidates->page_size));
    hash = input_context_signature_bytes(hash, &candidates->total,
                                         sizeof(candidates->total));
    hash = input_context_signature_bytes(hash, &candidates->has_prev,
                                         sizeof(candidates->has_prev));
    hash = input_context_signature_bytes(hash, &candidates->has_next,
                                         sizeof(candidates->has_next));

    for (size_t i = 0; i < candidates->count; ++i) {
        hash = input_context_signature_string(hash, candidates->candidates[i].text);
        hash = input_context_signature_string(hash, candidates->candidates[i].comment);
        hash = input_context_signature_string(hash, candidates->candidates[i].label);
    }

    return hash;
}

TypioInputContext *typio_input_context_new(TypioInstance *instance) {
    TypioInputContext *ctx = calloc(1, sizeof(TypioInputContext));
    if (!ctx) {
        return nullptr;
    }

    ctx->instance = instance;

    /* Initialize preedit */
    ctx->preedit = calloc(1, sizeof(TypioPreedit));
    ctx->preedit_segment_capacity = 8;
    ctx->preedit_segments = calloc(ctx->preedit_segment_capacity,
                                   sizeof(TypioPreeditSegment));

    /* Initialize candidates */
    ctx->candidates = calloc(1, sizeof(TypioCandidateList));
    ctx->candidate_capacity = 16;
    ctx->candidate_items = calloc(ctx->candidate_capacity,
                                  sizeof(TypioCandidate));

    if (!ctx->preedit || !ctx->preedit_segments ||
        !ctx->candidates || !ctx->candidate_items) {
        typio_input_context_free(ctx);
        return nullptr;
    }

    ctx->preedit->segments = ctx->preedit_segments;
    ctx->candidates->candidates = ctx->candidate_items;
    ctx->candidates->page_size = 10;

    return ctx;
}

void typio_input_context_free(TypioInputContext *ctx) {
    if (!ctx) {
        return;
    }

    /* Free properties */
    PropertyEntry *prop = ctx->properties;
    while (prop) {
        PropertyEntry *next = prop->next;
        if (prop->free_func && prop->value) {
            prop->free_func(prop->value);
        }
        free(prop->key);
        free(prop);
        prop = next;
    }

    /* Free preedit segments text */
    if (ctx->preedit_segments) {
        for (size_t i = 0; i < ctx->preedit->segment_count; i++) {
            free((void *)ctx->preedit_segments[i].text);
        }
    }
    free(ctx->preedit_segments);
    free(ctx->preedit);

    /* Free candidate items text */
    if (ctx->candidate_items) {
        for (size_t i = 0; i < ctx->candidates->count; i++) {
            free((void *)ctx->candidate_items[i].text);
            free((void *)ctx->candidate_items[i].comment);
            free((void *)ctx->candidate_items[i].label);
        }
    }
    free(ctx->candidate_items);
    free(ctx->candidates);

    free(ctx->surrounding_text);
    free(ctx);
}

void typio_input_context_focus_in(TypioInputContext *ctx) {
    if (!ctx || ctx->focused) {
        return;
    }

    ctx->focused = true;
    typio_instance_set_focused_context(ctx->instance, ctx);

    /* Notify engine */
    TypioEngineManager *manager = typio_instance_get_engine_manager(ctx->instance);
    TypioEngine *engine = typio_engine_manager_get_active(manager);
    if (engine && engine->base_ops) {
        engine->base_ops->focus_in(engine, ctx);
    }
}

void typio_input_context_focus_out(TypioInputContext *ctx) {
    if (!ctx || !ctx->focused) {
        return;
    }

    /* Notify engine */
    TypioEngineManager *manager = typio_instance_get_engine_manager(ctx->instance);
    TypioEngine *engine = typio_engine_manager_get_active(manager);
    if (engine && engine->base_ops) {
        engine->base_ops->focus_out(engine, ctx);
    }

    ctx->focused = false;
    typio_instance_set_focused_context(ctx->instance, nullptr);
}

bool typio_input_context_is_focused(TypioInputContext *ctx) {
    return ctx ? ctx->focused : false;
}

void typio_input_context_reset(TypioInputContext *ctx) {
    if (!ctx) {
        return;
    }

    /* Clear preedit */
    typio_input_context_clear_preedit(ctx);

    /* Clear candidates */
    typio_input_context_clear_candidates(ctx);

    /* Notify engine */
    TypioEngineManager *manager = typio_instance_get_engine_manager(ctx->instance);
    TypioEngine *engine = typio_engine_manager_get_active(manager);
    if (engine && engine->base_ops) {
        engine->base_ops->reset(engine, ctx);
    }
}

bool typio_input_context_process_key(TypioInputContext *ctx,
                                      const TypioKeyEvent *event) {
    if (!ctx || !event) {
        return false;
    }

    TypioEngineManager *manager = typio_instance_get_engine_manager(ctx->instance);
    TypioEngine *engine = typio_engine_manager_get_active(manager);

    if (!engine || !engine->keyboard || !engine->keyboard->process_key) {
        return false;
    }

    TypioKeyProcessResult result = engine->keyboard->process_key(engine, ctx, event);

    return (result != TYPIO_KEY_NOT_HANDLED);
}

void typio_input_context_commit(TypioInputContext *ctx, const char *text) {
    if (!ctx || !text) {
        return;
    }

    /* Clear preedit on commit */
    typio_input_context_clear_preedit(ctx);
    typio_input_context_clear_candidates(ctx);

    /* Notify callback */
    if (ctx->commit_callback) {
        ctx->commit_callback(ctx, text, ctx->commit_user_data);
    }
}

/* Internal clear without firing callback */
static void input_context_clear_preedit_silent(TypioInputContext *ctx) {
    if (!ctx || !ctx->preedit) {
        return;
    }
    for (size_t i = 0; i < ctx->preedit->segment_count; i++) {
        free((void *)ctx->preedit_segments[i].text);
        ctx->preedit_segments[i].text = nullptr;
    }
    ctx->preedit->segment_count = 0;
    ctx->preedit->cursor_pos = 0;
}

/* Internal clear without firing callback */
static void input_context_clear_candidates_silent(TypioInputContext *ctx) {
    if (!ctx || !ctx->candidates) {
        return;
    }
    for (size_t i = 0; i < ctx->candidates->count; i++) {
        free((void *)ctx->candidate_items[i].text);
        free((void *)ctx->candidate_items[i].comment);
        free((void *)ctx->candidate_items[i].label);
        ctx->candidate_items[i].text = nullptr;
        ctx->candidate_items[i].comment = nullptr;
        ctx->candidate_items[i].label = nullptr;
    }
    ctx->candidates->count = 0;
    ctx->candidates->page = 0;
    ctx->candidates->total = 0;
    ctx->candidates->selected = -1;
    ctx->candidates->has_prev = false;
    ctx->candidates->has_next = false;
}

void typio_input_context_set_preedit(TypioInputContext *ctx,
                                      const TypioPreedit *preedit) {
    if (!ctx || !preedit) {
        return;
    }

    /* Clear existing without firing callback */
    input_context_clear_preedit_silent(ctx);

    /* Grow segments array if needed */
    if (preedit->segment_count > ctx->preedit_segment_capacity) {
        size_t new_capacity = preedit->segment_count * 2;
        TypioPreeditSegment *new_segments = realloc(
            ctx->preedit_segments, new_capacity * sizeof(TypioPreeditSegment));
        if (!new_segments) {
            return;
        }
        ctx->preedit_segments = new_segments;
        ctx->preedit->segments = new_segments;
        ctx->preedit_segment_capacity = new_capacity;
    }

    /* Copy segments */
    for (size_t i = 0; i < preedit->segment_count; i++) {
        ctx->preedit_segments[i].text = typio_strdup(preedit->segments[i].text);
        ctx->preedit_segments[i].format = preedit->segments[i].format;
    }
    ctx->preedit->segment_count = preedit->segment_count;
    ctx->preedit->cursor_pos = preedit->cursor_pos;

    /* Notify callback */
    if (ctx->preedit_callback) {
        ctx->preedit_callback(ctx, ctx->preedit, ctx->preedit_user_data);
    }
}

const TypioPreedit *typio_input_context_get_preedit(TypioInputContext *ctx) {
    return ctx ? ctx->preedit : nullptr;
}

void typio_input_context_clear_preedit(TypioInputContext *ctx) {
    if (!ctx || !ctx->preedit) {
        return;
    }

    /* Free segment text */
    for (size_t i = 0; i < ctx->preedit->segment_count; i++) {
        free((void *)ctx->preedit_segments[i].text);
        ctx->preedit_segments[i].text = nullptr;
    }
    ctx->preedit->segment_count = 0;
    ctx->preedit->cursor_pos = 0;

    /* Notify callback */
    if (ctx->preedit_callback) {
        ctx->preedit_callback(ctx, ctx->preedit, ctx->preedit_user_data);
    }
}

void typio_input_context_set_candidates(TypioInputContext *ctx,
                                         const TypioCandidateList *candidates) {
    if (!ctx || !candidates) {
        return;
    }

    /* Clear existing without firing callback */
    input_context_clear_candidates_silent(ctx);

    /* Grow array if needed */
    if (candidates->count > ctx->candidate_capacity) {
        size_t new_capacity = candidates->count * 2;
        TypioCandidate *new_items = realloc(
            ctx->candidate_items, new_capacity * sizeof(TypioCandidate));
        if (!new_items) {
            return;
        }
        ctx->candidate_items = new_items;
        ctx->candidates->candidates = new_items;
        ctx->candidate_capacity = new_capacity;
    }

    /* Copy candidates */
    for (size_t i = 0; i < candidates->count; i++) {
        ctx->candidate_items[i].text = typio_strdup(candidates->candidates[i].text);
        ctx->candidate_items[i].comment = typio_strdup(candidates->candidates[i].comment);
        ctx->candidate_items[i].label = typio_strdup(candidates->candidates[i].label);
    }

    ctx->candidates->count = candidates->count;
    ctx->candidates->page = candidates->page;
    ctx->candidates->page_size = candidates->page_size;
    ctx->candidates->total = candidates->total;
    ctx->candidates->selected = candidates->selected;
    ctx->candidates->has_prev = candidates->has_prev;
    ctx->candidates->has_next = candidates->has_next;
    ctx->candidates->content_signature = input_context_candidate_signature(candidates);

    /* Notify callback */
    if (ctx->candidate_callback) {
        ctx->candidate_callback(ctx, ctx->candidates, ctx->candidate_user_data);
    }
}

const TypioCandidateList *typio_input_context_get_candidates(TypioInputContext *ctx) {
    return ctx ? ctx->candidates : nullptr;
}

void typio_input_context_set_candidate_selection(TypioInputContext *ctx, int selected) {
    if (!ctx || !ctx->candidates || ctx->candidates->count == 0) {
        return;
    }

    if (ctx->candidates->selected == selected) {
        return;
    }

    ctx->candidates->selected = selected;

    if (ctx->candidate_callback) {
        ctx->candidate_callback(ctx, ctx->candidates, ctx->candidate_user_data);
    }
}

void typio_input_context_clear_candidates(TypioInputContext *ctx) {
    if (!ctx || !ctx->candidates) {
        return;
    }

    /* Free candidate text */
    for (size_t i = 0; i < ctx->candidates->count; i++) {
        free((void *)ctx->candidate_items[i].text);
        free((void *)ctx->candidate_items[i].comment);
        free((void *)ctx->candidate_items[i].label);
        ctx->candidate_items[i].text = nullptr;
        ctx->candidate_items[i].comment = nullptr;
        ctx->candidate_items[i].label = nullptr;
    }

    ctx->candidates->count = 0;
    ctx->candidates->page = 0;
    ctx->candidates->total = 0;
    ctx->candidates->selected = -1;
    ctx->candidates->has_prev = false;
    ctx->candidates->has_next = false;
    ctx->candidates->content_signature = 0;

    /* Notify callback */
    if (ctx->candidate_callback) {
        ctx->candidate_callback(ctx, ctx->candidates, ctx->candidate_user_data);
    }
}

void typio_input_context_set_surrounding(TypioInputContext *ctx,
                                          const char *text,
                                          int cursor_pos,
                                          int anchor_pos) {
    if (!ctx) {
        return;
    }

    free(ctx->surrounding_text);
    ctx->surrounding_text = text ? typio_strdup(text) : nullptr;
    ctx->surrounding_cursor = cursor_pos;
    ctx->surrounding_anchor = anchor_pos;
}

bool typio_input_context_get_surrounding(TypioInputContext *ctx,
                                          const char **text,
                                          int *cursor_pos,
                                          int *anchor_pos) {
    if (!ctx || !ctx->surrounding_text) {
        return false;
    }

    if (text) *text = ctx->surrounding_text;
    if (cursor_pos) *cursor_pos = ctx->surrounding_cursor;
    if (anchor_pos) *anchor_pos = ctx->surrounding_anchor;

    return true;
}

void typio_input_context_delete_surrounding([[maybe_unused]] TypioInputContext *ctx,
                                             [[maybe_unused]] int offset, [[maybe_unused]] int length) {
    /* This would typically communicate with the client */
    /* For now, just a placeholder */
}

void typio_input_context_set_capabilities(TypioInputContext *ctx, uint32_t caps) {
    if (ctx) {
        ctx->capabilities = caps;
    }
}

uint32_t typio_input_context_get_capabilities(TypioInputContext *ctx) {
    return ctx ? ctx->capabilities : 0;
}

void typio_input_context_set_commit_callback(TypioInputContext *ctx,
                                              TypioCommitCallback callback,
                                              void *user_data) {
    if (ctx) {
        ctx->commit_callback = callback;
        ctx->commit_user_data = user_data;
    }
}

void typio_input_context_set_preedit_callback(TypioInputContext *ctx,
                                               TypioPreeditCallback callback,
                                               void *user_data) {
    if (ctx) {
        ctx->preedit_callback = callback;
        ctx->preedit_user_data = user_data;
    }
}

void typio_input_context_set_candidate_callback(TypioInputContext *ctx,
                                                 TypioCandidateCallback callback,
                                                 void *user_data) {
    if (ctx) {
        ctx->candidate_callback = callback;
        ctx->candidate_user_data = user_data;
    }
}

void typio_input_context_set_user_data(TypioInputContext *ctx, void *data) {
    if (ctx) {
        ctx->user_data = data;
    }
}

void *typio_input_context_get_user_data(TypioInputContext *ctx) {
    return ctx ? ctx->user_data : nullptr;
}

void typio_input_context_set_property(TypioInputContext *ctx,
                                       const char *key, void *value,
                                       void (*free_func)(void *)) {
    if (!ctx || !key) {
        return;
    }

    /* Check if property exists */
    PropertyEntry *prop = ctx->properties;
    while (prop) {
        if (strcmp(prop->key, key) == 0) {
            /* Free old value */
            if (prop->free_func && prop->value) {
                prop->free_func(prop->value);
            }
            prop->value = value;
            prop->free_func = free_func;
            return;
        }
        prop = prop->next;
    }

    /* Create new property */
    prop = malloc(sizeof(PropertyEntry));
    if (!prop) {
        return;
    }

    prop->key = typio_strdup(key);
    prop->value = value;
    prop->free_func = free_func;
    prop->next = ctx->properties;
    ctx->properties = prop;
}

void *typio_input_context_get_property(TypioInputContext *ctx, const char *key) {
    if (!ctx || !key) {
        return nullptr;
    }

    PropertyEntry *prop = ctx->properties;
    while (prop) {
        if (strcmp(prop->key, key) == 0) {
            return prop->value;
        }
        prop = prop->next;
    }

    return nullptr;
}

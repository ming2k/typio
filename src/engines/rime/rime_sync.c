/**
 * @file rime_sync.c
 * @brief Synchronise librime RimeContext to TypioInputContext
 *
 * Handles preedit text, candidate lists, commit text, and performance-
 * optimised fast paths for selection-only changes.
 */

#include "rime_internal.h"

void typio_rime_clear_state(TypioInputContext *ctx) {
    typio_input_context_clear_preedit(ctx);
    typio_input_context_clear_candidates(ctx);
}

bool typio_rime_flush_commit(TypioRimeSession *session,
                              TypioInputContext *ctx) {
    RIME_STRUCT(RimeCommit, commit);
    bool committed = false;

    if (!session || !ctx || !session->state) {
        return false;
    }

    if (session->state->api->get_commit(session->session_id, &commit)) {
        if (commit.text && *commit.text) {
            typio_input_context_commit(ctx, commit.text);
            committed = true;
        }
        session->state->api->free_commit(&commit);
    }

    return committed;
}

/**
 * Check whether the Rime context differs from the current InputContext only
 * in the highlighted candidate index.  When true, the caller can skip the
 * expensive full-copy path and just update the selection.
 */
static bool is_selection_only_change(const RimeContext *rime_context,
                                      TypioInputContext *ctx) {
    const TypioCandidateList *current = typio_input_context_get_candidates(ctx);

    if (!current || current->count == 0) {
        return false;
    }

    if (rime_context->menu.num_candidates <= 0 || !rime_context->menu.candidates) {
        return false;
    }

    if ((size_t)rime_context->menu.num_candidates != current->count ||
        rime_context->menu.page_no != current->page ||
        rime_context->menu.page_size != current->page_size ||
        rime_context->menu.is_last_page != !current->has_next) {
        return false;
    }

    const size_t select_keys_len = rime_context->menu.select_keys ? strlen(rime_context->menu.select_keys) : 0;
    static const char *const fast_labels[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};

    for (int i = 0; i < rime_context->menu.num_candidates; ++i) {
        const char *rime_text = rime_context->menu.candidates[i].text;
        const char *cur_text = current->candidates[i].text;
        const char *rime_comment = rime_context->menu.candidates[i].comment;
        const char *cur_comment = current->candidates[i].comment;
        const char *rime_label = nullptr;
        const char *cur_label = current->candidates[i].label;
        char fallback_label[16];

        if (rime_context->select_labels && rime_context->select_labels[i]) {
            rime_label = rime_context->select_labels[i];
        } else if (rime_context->menu.select_keys && (size_t)i < select_keys_len) {
            fallback_label[0] = rime_context->menu.select_keys[i];
            fallback_label[1] = '\0';
            rime_label = fallback_label;
        } else if (i < 9) {
            rime_label = fast_labels[i];
        } else {
            snprintf(fallback_label, sizeof(fallback_label), "%d", i + 1);
            rime_label = fallback_label;
        }

        if (!rime_text || !cur_text || strcmp(rime_text, cur_text) != 0 ||
            strcmp(rime_comment ? rime_comment : "",
                   cur_comment ? cur_comment : "") != 0 ||
            strcmp(rime_label ? rime_label : "",
                   cur_label ? cur_label : "") != 0) {
            return false;
        }
    }

    return true;
}

static bool preedit_matches_context(const RimeContext *rime_context,
                                     TypioInputContext *ctx) {
    const TypioPreedit *current = typio_input_context_get_preedit(ctx);
    const char *rime_preedit;

    if (!rime_context || !ctx) {
        return false;
    }

    rime_preedit = rime_context->composition.preedit;
    if (!rime_preedit || !*rime_preedit) {
        return !current || current->segment_count == 0;
    }

    if (!current || current->segment_count != 1 || !current->segments[0].text) {
        return false;
    }

    return current->cursor_pos == rime_context->composition.cursor_pos &&
           strcmp(current->segments[0].text, rime_preedit) == 0;
}

bool typio_rime_highlight_candidate(TypioRimeSession *session, size_t index) {
    if (!session || !session->state) {
        return false;
    }
    return session->state->api->highlight_candidate_on_current_page(
        session->session_id, index) != 0;
}

bool typio_rime_delete_candidate(TypioRimeSession *session, size_t index) {
    if (!session || !session->state) {
        return false;
    }
    return session->state->api->delete_candidate_on_current_page(
        session->session_id, index) != 0;
}

bool typio_rime_sync_context(TypioRimeSession *session,
                               TypioInputContext *ctx) {
    RIME_STRUCT(RimeContext, rime_context);
    TypioRimeState *state;
    bool has_preedit = false;
    bool has_candidates = false;
    uint64_t start_ms;
    uint64_t end_ms;
    size_t candidate_count = 0;
    int selected = -1;
    int page = 0;
    int total = 0;

    if (!session || !ctx || !session->state) {
        return false;
    }

    start_ms = typio_rime_monotonic_ms();
    state = session->state;
    if (!state->api->get_context(session->session_id, &rime_context)) {
        typio_rime_clear_state(ctx);
        return false;
    }

    /* Fast path: when preedit text is unchanged and only the highlighted
     * candidate moved, skip both preedit rebuilding and candidate copying. */
    if (preedit_matches_context(&rime_context, ctx) &&
        is_selection_only_change(&rime_context, ctx)) {
        selected = rime_context.menu.highlighted_candidate_index;
        typio_input_context_set_candidate_selection(ctx, selected);
        has_preedit = true;
        has_candidates = true;

        state->api->free_context(&rime_context);
        return true;
    }

    if (rime_context.composition.preedit && *rime_context.composition.preedit) {
        TypioPreeditSegment segment = {
            .text = rime_context.composition.preedit,
            .format = TYPIO_PREEDIT_UNDERLINE,
        };
        TypioPreedit preedit = {
            .segments = &segment,
            .segment_count = 1,
            .cursor_pos = rime_context.composition.cursor_pos,
        };
        typio_input_context_set_preedit(ctx, &preedit);
        has_preedit = true;
    } else {
        typio_input_context_clear_preedit(ctx);
    }

    if (rime_context.menu.num_candidates > 0 && rime_context.menu.candidates) {
        const int count = rime_context.menu.num_candidates;
        TypioCandidate stack_items[10];
        char *stack_labels[10];
        TypioCandidate *items = count <= 10 ? stack_items : calloc((size_t)count, sizeof(*items));
        char **labels = count <= 10 ? stack_labels : calloc((size_t)count, sizeof(*labels));

        if (count <= 10) {
            memset(items, 0, sizeof(TypioCandidate) * (size_t)count);
            memset(labels, 0, sizeof(char *) * (size_t)count);
        }

        candidate_count = (size_t)count;
        selected = rime_context.menu.highlighted_candidate_index;
        page = rime_context.menu.page_no;
        total = rime_context.menu.page_no * rime_context.menu.page_size +
                count + (rime_context.menu.is_last_page ? 0 :
                         rime_context.menu.page_size);

        if (items && labels) {
            TypioCandidateList list = {
                .candidates = items,
                .count = (size_t)count,
                .page = rime_context.menu.page_no,
                .page_size = rime_context.menu.page_size,
                .total = total,
                .selected = rime_context.menu.highlighted_candidate_index,
                .has_prev = rime_context.menu.page_no > 0,
                .has_next = !rime_context.menu.is_last_page,
            };

            const size_t select_keys_len = rime_context.menu.select_keys ? strlen(rime_context.menu.select_keys) : 0;
            static const char *const fast_labels[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};

            for (int i = 0; i < count; ++i) {
                items[i].text = rime_context.menu.candidates[i].text;
                items[i].comment = rime_context.menu.candidates[i].comment;

                if (rime_context.select_labels && rime_context.select_labels[i]) {
                    items[i].label = rime_context.select_labels[i];
                    continue;
                }

                if (rime_context.menu.select_keys && (size_t)i < select_keys_len) {
                    char label[2] = {rime_context.menu.select_keys[i], '\0'};
                    labels[i] = typio_strdup(label);
                    items[i].label = labels[i];
                    continue;
                }

                if (i < 9) {
                    items[i].label = fast_labels[i];
                    continue;
                }

                char label[16];
                snprintf(label, sizeof(label), "%d", i + 1);
                labels[i] = typio_strdup(label);
                items[i].label = labels[i];
            }

            typio_input_context_set_candidates(ctx, &list);
            has_candidates = true;
        } else {
            typio_input_context_clear_candidates(ctx);
        }

        if (labels) {
            for (int i = 0; i < count; ++i) {
                free(labels[i]);
            }
            if (count > 10) free(labels);
        }
        if (count > 10) free(items);
    } else {
        typio_input_context_clear_candidates(ctx);
    }

    state->api->free_context(&rime_context);

    end_ms = typio_rime_monotonic_ms();
    if (end_ms >= start_ms && (end_ms - start_ms) >= TYPIO_RIME_SLOW_SYNC_MS) {
        typio_log_debug(
            "Rime sync slow: total=%" PRIu64 "ms session=%u candidates=%zu selected=%d page=%d total_candidates=%d preedit=%s",
            end_ms - start_ms,
            (unsigned int)session->session_id,
            candidate_count,
            selected,
            page,
            total,
            has_preedit ? "yes" : "no");
    }

    return has_preedit || has_candidates;
}

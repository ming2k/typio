/**
 * @file rime_sync.c
 * @brief Synchronise librime RimeContext to TypioInputContext
 *
 * Builds one atomic TypioComposition (preedit + candidates) per sync and emits
 * it through typio_input_context_set_composition (ADR-0011). Commit text is a
 * separate one-shot event.
 */

#include "rime_internal.h"

void typio_rime_clear_state(TypioInputContext *ctx) {
    typio_input_context_clear(ctx);
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

    /* One transactional composition; preedit segment and candidate items must
     * stay alive until set_composition copies them, so they live at function
     * scope rather than inside the per-section branches. */
    TypioComposition comp = {
        .struct_size = sizeof(TypioComposition),
        .selected = -1,
    };
    TypioPreeditSegment segment;
    TypioCandidate stack_items[10];
    char *stack_labels[10];
    TypioCandidate *items = NULL;
    char **labels = NULL;
    int count = 0;

    if (!session || !ctx || !session->state) {
        return false;
    }

    start_ms = typio_rime_monotonic_ms();
    state = session->state;
    if (!state->api->get_context(session->session_id, &rime_context)) {
        typio_rime_clear_state(ctx);
        return false;
    }

    if (rime_context.composition.preedit && *rime_context.composition.preedit) {
        segment.text = rime_context.composition.preedit;
        segment.format = TYPIO_PREEDIT_UNDERLINE;
        comp.segments = &segment;
        comp.segment_count = 1;
        comp.cursor_pos = rime_context.composition.cursor_pos;
        has_preedit = true;
    }

    if (rime_context.menu.num_candidates > 0 && rime_context.menu.candidates) {
        count = rime_context.menu.num_candidates;
        items = count <= 10 ? stack_items : calloc((size_t)count, sizeof(*items));
        labels = count <= 10 ? stack_labels : calloc((size_t)count, sizeof(*labels));

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

            comp.candidates = items;
            comp.candidate_count = (size_t)count;
            comp.page = rime_context.menu.page_no;
            comp.page_size = rime_context.menu.page_size;
            comp.total = total;
            comp.selected = rime_context.menu.highlighted_candidate_index;
            comp.has_prev = rime_context.menu.page_no > 0;
            comp.has_next = !rime_context.menu.is_last_page;
            has_candidates = true;
        }
    }

    if (has_preedit || has_candidates) {
        typio_input_context_set_composition(ctx, &comp);
    } else {
        typio_input_context_clear(ctx);
    }

    if (labels) {
        for (int i = 0; i < count; ++i) {
            free(labels[i]);
        }
        if (count > 10) free(labels);
    }
    if (items && count > 10) free(items);

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

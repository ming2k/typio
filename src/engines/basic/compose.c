/**
 * @file compose.c
 * @brief Compose/dead-key state machine for the basic keyboard engine
 */

#include "compose.h"
#include <stdlib.h>
#include <string.h>

#define COMPOSE_KEY(first, second) (((uint32_t)(first) << 16) | (uint32_t)(second))

typedef struct {
    uint32_t key;
    uint32_t result;
} ComposeRule;

/* clang-format off */
static const ComposeRule rules[] = {
    /* Acute accent (') */
    {COMPOSE_KEY('\'', 'A'), 0x00C1}, /* Á */
    {COMPOSE_KEY('\'', 'C'), 0x0106}, /* Ć */
    {COMPOSE_KEY('\'', 'E'), 0x00C9}, /* É */
    {COMPOSE_KEY('\'', 'G'), 0x01F4}, /* Ǵ */
    {COMPOSE_KEY('\'', 'I'), 0x00CD}, /* Í */
    {COMPOSE_KEY('\'', 'L'), 0x0139}, /* Ĺ */
    {COMPOSE_KEY('\'', 'N'), 0x0143}, /* Ń */
    {COMPOSE_KEY('\'', 'O'), 0x00D3}, /* Ó */
    {COMPOSE_KEY('\'', 'R'), 0x0154}, /* Ŕ */
    {COMPOSE_KEY('\'', 'S'), 0x015A}, /* Ś */
    {COMPOSE_KEY('\'', 'U'), 0x00DA}, /* Ú */
    {COMPOSE_KEY('\'', 'Y'), 0x00DD}, /* Ý */
    {COMPOSE_KEY('\'', 'Z'), 0x0179}, /* Ź */
    {COMPOSE_KEY('\'', 'a'), 0x00E1}, /* á */
    {COMPOSE_KEY('\'', 'c'), 0x0107}, /* ć */
    {COMPOSE_KEY('\'', 'e'), 0x00E9}, /* é */
    {COMPOSE_KEY('\'', 'g'), 0x01F5}, /* ǵ */
    {COMPOSE_KEY('\'', 'i'), 0x00ED}, /* í */
    {COMPOSE_KEY('\'', 'k'), 0x1E31}, /* ḱ */
    {COMPOSE_KEY('\'', 'l'), 0x013A}, /* ĺ */
    {COMPOSE_KEY('\'', 'm'), 0x1E3F}, /* ḿ */
    {COMPOSE_KEY('\'', 'n'), 0x0144}, /* ń */
    {COMPOSE_KEY('\'', 'o'), 0x00F3}, /* ó */
    {COMPOSE_KEY('\'', 'p'), 0x1E55}, /* ṕ */
    {COMPOSE_KEY('\'', 'r'), 0x0155}, /* ŕ */
    {COMPOSE_KEY('\'', 's'), 0x015B}, /* ś */
    {COMPOSE_KEY('\'', 'u'), 0x00FA}, /* ú */
    {COMPOSE_KEY('\'', 'y'), 0x00FD}, /* ý */
    {COMPOSE_KEY('\'', 'z'), 0x017A}, /* ź */

    /* Grave accent (`) */
    {COMPOSE_KEY('`', 'A'), 0x00C0}, /* À */
    {COMPOSE_KEY('`', 'E'), 0x00C8}, /* È */
    {COMPOSE_KEY('`', 'I'), 0x00CC}, /* Ì */
    {COMPOSE_KEY('`', 'O'), 0x00D2}, /* Ò */
    {COMPOSE_KEY('`', 'U'), 0x00D9}, /* Ù */
    {COMPOSE_KEY('`', 'a'), 0x00E0}, /* à */
    {COMPOSE_KEY('`', 'e'), 0x00E8}, /* è */
    {COMPOSE_KEY('`', 'i'), 0x00EC}, /* ì */
    {COMPOSE_KEY('`', 'o'), 0x00F2}, /* ò */
    {COMPOSE_KEY('`', 'u'), 0x00F9}, /* ù */

    /* Circumflex (^) */
    {COMPOSE_KEY('^', 'A'), 0x00C2}, /* Â */
    {COMPOSE_KEY('^', 'E'), 0x00CA}, /* Ê */
    {COMPOSE_KEY('^', 'I'), 0x00CE}, /* Î */
    {COMPOSE_KEY('^', 'O'), 0x00D4}, /* Ô */
    {COMPOSE_KEY('^', 'U'), 0x00DB}, /* Û */
    {COMPOSE_KEY('^', 'a'), 0x00E2}, /* â */
    {COMPOSE_KEY('^', 'e'), 0x00EA}, /* ê */
    {COMPOSE_KEY('^', 'i'), 0x00EE}, /* î */
    {COMPOSE_KEY('^', 'o'), 0x00F4}, /* ô */
    {COMPOSE_KEY('^', 'u'), 0x00FB}, /* û */

    /* Diaeresis/umlaut (") */
    {COMPOSE_KEY('"', 'A'), 0x00C4}, /* Ä */
    {COMPOSE_KEY('"', 'E'), 0x00CB}, /* Ë */
    {COMPOSE_KEY('"', 'I'), 0x00CF}, /* Ï */
    {COMPOSE_KEY('"', 'O'), 0x00D6}, /* Ö */
    {COMPOSE_KEY('"', 'U'), 0x00DC}, /* Ü */
    {COMPOSE_KEY('"', 'Y'), 0x0178}, /* Ÿ */
    {COMPOSE_KEY('"', 'a'), 0x00E4}, /* ä */
    {COMPOSE_KEY('"', 'e'), 0x00EB}, /* ë */
    {COMPOSE_KEY('"', 'i'), 0x00EF}, /* ï */
    {COMPOSE_KEY('"', 'o'), 0x00F6}, /* ö */
    {COMPOSE_KEY('"', 'u'), 0x00FC}, /* ü */
    {COMPOSE_KEY('"', 'y'), 0x00FF}, /* ÿ */

    /* Tilde (~) */
    {COMPOSE_KEY('~', 'A'), 0x00C3}, /* Ã */
    {COMPOSE_KEY('~', 'N'), 0x00D1}, /* Ñ */
    {COMPOSE_KEY('~', 'O'), 0x00D5}, /* Õ */
    {COMPOSE_KEY('~', 'a'), 0x00E3}, /* ã */
    {COMPOSE_KEY('~', 'n'), 0x00F1}, /* ñ */
    {COMPOSE_KEY('~', 'o'), 0x00F5}, /* õ */

    /* Cedilla (,) */
    {COMPOSE_KEY(',', 'C'), 0x00C7}, /* Ç */
    {COMPOSE_KEY(',', 'c'), 0x00E7}, /* ç */

    /* Slash (/) */
    {COMPOSE_KEY('/', 'L'), 0x0141}, /* Ł */
    {COMPOSE_KEY('/', 'O'), 0x00D8}, /* Ø */
    {COMPOSE_KEY('/', 'l'), 0x0142}, /* ł */
    {COMPOSE_KEY('/', 'o'), 0x00F8}, /* ø */

    /* Special punctuation */
    {COMPOSE_KEY('?', '?'), 0x00BF}, /* ¿ */
    {COMPOSE_KEY('!', '!'), 0x00A1}, /* ¡ */
    {COMPOSE_KEY('<', '<'), 0x00AB}, /* « */
    {COMPOSE_KEY('>', '>'), 0x00BB}, /* » */
    {COMPOSE_KEY('\'', '\''), 0x0027}, /* ' (cancel: two quotes = literal quote) */
    {COMPOSE_KEY('`', '`'), 0x0060},   /* ` (literal) */
    {COMPOSE_KEY('"', '"'), 0x0022},  /* " (literal) */
    {COMPOSE_KEY('^', '^'), 0x005E},   /* ^ (literal) */
    {COMPOSE_KEY('~', '~'), 0x007E},   /* ~ (literal) */
    {COMPOSE_KEY(',', ','), 0x002C},   /* , (literal) */
    {COMPOSE_KEY('-', '-'), 0x2013},   /* – en dash */
    {COMPOSE_KEY('-', '='), 0x2014},   /* — em dash */
    {COMPOSE_KEY('.', '.'), 0x2026},   /* … ellipsis */
};
/* clang-format on */

static const size_t rule_count = sizeof(rules) / sizeof(rules[0]);

static const ComposeRule *find_rule(uint32_t first, uint32_t second) {
    uint32_t key = COMPOSE_KEY(first, second);
    for (size_t i = 0; i < rule_count; i++) {
        if (rules[i].key == key) {
            return &rules[i];
        }
    }
    return NULL;
}

static bool can_start_compose(uint32_t codepoint) {
    for (size_t i = 0; i < rule_count; i++) {
        if ((rules[i].key >> 16) == codepoint) {
            return true;
        }
    }
    return false;
}

static size_t encode_utf8(uint32_t codepoint, char out[5]) {
    if (codepoint <= 0x7F) {
        out[0] = (char)codepoint;
        out[1] = '\0';
        return 1;
    }
    if (codepoint <= 0x7FF) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        out[2] = '\0';
        return 2;
    }
    if (codepoint <= 0xFFFF) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        out[3] = '\0';
        return 3;
    }
    if (codepoint <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (codepoint >> 18));
        out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[3] = (char)(0x80 | (codepoint & 0x3F));
        out[4] = '\0';
        return 4;
    }
    out[0] = '\0';
    return 0;
}

struct BasicCompose {
    bool active;
    uint32_t first;
    char preedit[8];
};

BasicCompose *basic_compose_new(void) {
    BasicCompose *state = calloc(1, sizeof(BasicCompose));
    return state;
}

void basic_compose_free(BasicCompose *state) {
    free(state);
}

BasicComposeResult basic_compose_process_key(BasicCompose *state,
                                              uint32_t codepoint,
                                              uint32_t out_codepoints[4],
                                              size_t *out_count) {
    *out_count = 0;

    if (!state->active) {
        if (can_start_compose(codepoint)) {
            state->active = true;
            state->first = codepoint;
            encode_utf8(codepoint, state->preedit);
            return BASIC_COMPOSE_CONSUME;
        }
        return BASIC_COMPOSE_NONE;
    }

    state->active = false;

    const ComposeRule *rule = find_rule(state->first, codepoint);
    if (rule) {
        out_codepoints[0] = rule->result;
        *out_count = 1;
        return BASIC_COMPOSE_COMMIT;
    }

    /* No match: flush the buffered first key and let caller re-process current key. */
    out_codepoints[0] = state->first;
    *out_count = 1;
    return BASIC_COMPOSE_CANCEL;
}

const char *basic_compose_get_preedit(const BasicCompose *state) {
    if (!state || !state->active) {
        return NULL;
    }
    return state->preedit;
}

uint32_t basic_compose_cancel(BasicCompose *state) {
    if (!state || !state->active) {
        return 0;
    }
    uint32_t cp = state->first;
    state->active = false;
    return cp;
}

void basic_compose_reset(BasicCompose *state) {
    if (state) {
        state->active = false;
        state->first = 0;
        state->preedit[0] = '\0';
    }
}

bool basic_compose_is_active(const BasicCompose *state) {
    return state && state->active;
}

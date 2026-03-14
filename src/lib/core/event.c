/**
 * @file event.c
 * @brief Event handling implementation
 */

#include "typio/event.h"

#include <stdlib.h>
#include <string.h>

TypioKeyEvent *typio_key_event_new(TypioEventType type, uint32_t keycode,
                                    uint32_t keysym, uint32_t modifiers) {
    TypioKeyEvent *event = calloc(1, sizeof(TypioKeyEvent));
    if (!event) {
        return nullptr;
    }

    event->type = type;
    event->keycode = keycode;
    event->keysym = keysym;
    event->modifiers = modifiers;

    return event;
}

void typio_key_event_free(TypioKeyEvent *event) {
    free(event);
}

bool typio_key_event_is_press(const TypioKeyEvent *event) {
    return event && event->type == TYPIO_EVENT_KEY_PRESS;
}

bool typio_key_event_is_release(const TypioKeyEvent *event) {
    return event && event->type == TYPIO_EVENT_KEY_RELEASE;
}

bool typio_key_event_has_modifier(const TypioKeyEvent *event, TypioModifier mod) {
    return event && (event->modifiers & mod) != 0;
}

bool typio_key_event_is_modifier_only(const TypioKeyEvent *event) {
    if (!event) {
        return false;
    }

    switch (event->keysym) {
        case TYPIO_KEY_Shift_L:
        case TYPIO_KEY_Shift_R:
        case TYPIO_KEY_Control_L:
        case TYPIO_KEY_Control_R:
        case TYPIO_KEY_Alt_L:
        case TYPIO_KEY_Alt_R:
        case TYPIO_KEY_Super_L:
        case TYPIO_KEY_Super_R:
            return true;
        default:
            return false;
    }
}

uint32_t typio_key_event_get_unicode(const TypioKeyEvent *event) {
    if (!event) {
        return 0;
    }

    /* Return pre-set unicode if available */
    if (event->unicode) {
        return event->unicode;
    }

    /* Simple ASCII mapping for common keys */
    if (event->keysym >= 0x20 && event->keysym <= 0x7e) {
        return event->keysym;
    }

    return 0;
}

bool typio_key_event_is_backspace(const TypioKeyEvent *event) {
    return event && event->keysym == TYPIO_KEY_BackSpace;
}

bool typio_key_event_is_enter(const TypioKeyEvent *event) {
    return event && event->keysym == TYPIO_KEY_Return;
}

bool typio_key_event_is_escape(const TypioKeyEvent *event) {
    return event && event->keysym == TYPIO_KEY_Escape;
}

bool typio_key_event_is_space(const TypioKeyEvent *event) {
    return event && event->keysym == TYPIO_KEY_space;
}

bool typio_key_event_is_tab(const TypioKeyEvent *event) {
    return event && event->keysym == TYPIO_KEY_Tab;
}

bool typio_key_event_is_arrow(const TypioKeyEvent *event) {
    if (!event) {
        return false;
    }

    switch (event->keysym) {
        case TYPIO_KEY_Left:
        case TYPIO_KEY_Right:
        case TYPIO_KEY_Up:
        case TYPIO_KEY_Down:
            return true;
        default:
            return false;
    }
}

bool typio_key_event_is_page(const TypioKeyEvent *event) {
    if (!event) {
        return false;
    }

    switch (event->keysym) {
        case TYPIO_KEY_Page_Up:
        case TYPIO_KEY_Page_Down:
            return true;
        default:
            return false;
    }
}

TypioVoiceEvent *typio_voice_event_new(TypioEventType type) {
    TypioVoiceEvent *event = calloc(1, sizeof(TypioVoiceEvent));
    if (!event) {
        return nullptr;
    }

    event->type = type;

    return event;
}

void typio_voice_event_free(TypioVoiceEvent *event) {
    free(event);
}

void typio_voice_event_set_data(TypioVoiceEvent *event, const void *data,
                                 size_t size, int sample_rate, int channels,
                                 int bits_per_sample) {
    if (!event) {
        return;
    }

    event->audio_data = data;
    event->audio_size = size;
    event->sample_rate = sample_rate;
    event->channels = channels;
    event->bits_per_sample = bits_per_sample;
}

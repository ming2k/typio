/**
 * @file event.h
 * @brief Event handling for Typio
 */

#ifndef TYPIO_EVENT_H
#define TYPIO_EVENT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Key event structure
 */
struct TypioKeyEvent {
    TypioEventType type;    /* TYPIO_EVENT_KEY_PRESS or TYPIO_EVENT_KEY_RELEASE */
    uint32_t keycode;       /* Raw keycode */
    uint32_t keysym;        /* Key symbol (XKB compatible) */
    uint32_t modifiers;     /* Modifier flags */
    uint32_t unicode;       /* Unicode codepoint (0 if not applicable) */
    uint64_t time;          /* Event timestamp in milliseconds */
    bool is_repeat;         /* Whether this is a key repeat event */
};

/**
 * @brief Voice data event structure
 */
typedef struct TypioVoiceEvent {
    TypioEventType type;    /* TYPIO_EVENT_VOICE_* */
    const void *audio_data; /* Audio sample data */
    size_t audio_size;      /* Size of audio data in bytes */
    int sample_rate;        /* Sample rate (e.g., 16000) */
    int channels;           /* Number of channels (usually 1) */
    int bits_per_sample;    /* Bits per sample (usually 16) */
} TypioVoiceEvent;

/**
 * @brief Generic event structure
 */
struct TypioEvent {
    TypioEventType type;
    uint64_t time;
    union {
        TypioKeyEvent key;
        TypioVoiceEvent voice;
    } data;
};

/* Key event creation */
TypioKeyEvent *typio_key_event_new(TypioEventType type, uint32_t keycode,
                                    uint32_t keysym, uint32_t modifiers);
void typio_key_event_free(TypioKeyEvent *event);

/* Key event helpers */
bool typio_key_event_is_press(const TypioKeyEvent *event);
bool typio_key_event_is_release(const TypioKeyEvent *event);
bool typio_key_event_has_modifier(const TypioKeyEvent *event, TypioModifier mod);
bool typio_key_event_is_modifier_only(const TypioKeyEvent *event);
uint32_t typio_key_event_get_unicode(const TypioKeyEvent *event);

/* Common key checks */
bool typio_key_event_is_backspace(const TypioKeyEvent *event);
bool typio_key_event_is_enter(const TypioKeyEvent *event);
bool typio_key_event_is_escape(const TypioKeyEvent *event);
bool typio_key_event_is_space(const TypioKeyEvent *event);
bool typio_key_event_is_tab(const TypioKeyEvent *event);
bool typio_key_event_is_arrow(const TypioKeyEvent *event);
bool typio_key_event_is_page(const TypioKeyEvent *event);

/* Voice event creation */
TypioVoiceEvent *typio_voice_event_new(TypioEventType type);
void typio_voice_event_free(TypioVoiceEvent *event);
void typio_voice_event_set_data(TypioVoiceEvent *event, const void *data,
                                 size_t size, int sample_rate, int channels,
                                 int bits_per_sample);

/* Key symbol definitions (XKB compatible) */
#define TYPIO_KEY_BackSpace     0xff08
#define TYPIO_KEY_Tab           0xff09
#define TYPIO_KEY_Return        0xff0d
#define TYPIO_KEY_KP_Enter      0xff8d
#define TYPIO_KEY_Escape        0xff1b
#define TYPIO_KEY_Delete        0xffff
#define TYPIO_KEY_Home          0xff50
#define TYPIO_KEY_Left          0xff51
#define TYPIO_KEY_Up            0xff52
#define TYPIO_KEY_Right         0xff53
#define TYPIO_KEY_Down          0xff54
#define TYPIO_KEY_Page_Up       0xff55
#define TYPIO_KEY_Page_Down     0xff56
#define TYPIO_KEY_End           0xff57
#define TYPIO_KEY_space         0x0020

#define TYPIO_KEY_Shift_L       0xffe1
#define TYPIO_KEY_Shift_R       0xffe2
#define TYPIO_KEY_Control_L     0xffe3
#define TYPIO_KEY_Control_R     0xffe4
#define TYPIO_KEY_Alt_L         0xffe9
#define TYPIO_KEY_Alt_R         0xffea
#define TYPIO_KEY_Super_L       0xffeb
#define TYPIO_KEY_Super_R       0xffec

/* Function keys */
#define TYPIO_KEY_F1            0xffbe
#define TYPIO_KEY_F2            0xffbf
#define TYPIO_KEY_F3            0xffc0
#define TYPIO_KEY_F4            0xffc1
#define TYPIO_KEY_F5            0xffc2
#define TYPIO_KEY_F6            0xffc3
#define TYPIO_KEY_F7            0xffc4
#define TYPIO_KEY_F8            0xffc5
#define TYPIO_KEY_F9            0xffc6
#define TYPIO_KEY_F10           0xffc7
#define TYPIO_KEY_F11           0xffc8
#define TYPIO_KEY_F12           0xffc9

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_EVENT_H */

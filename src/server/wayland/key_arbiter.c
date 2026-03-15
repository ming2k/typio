/**
 * @file key_arbiter.c
 * @brief Key event arbiter — buffers modifier events during potential
 *        system shortcut sequences and either consumes or replays them
 *
 * State machine:
 *
 *   IDLE ── 2nd chord modifier pressed ──▶ BUFFERING
 *
 *   BUFFERING ── all chord mods released cleanly ──▶ CONSUME → IDLE
 *   BUFFERING ── non-modifier / Alt / Super pressed ──▶ REPLAY → IDLE
 *   BUFFERING ── buffer overflow ──▶ REPLAY → IDLE
 */

#include "key_arbiter.h"

#include "shortcut_chord.h"
#include "vk_bridge.h"
#include "wl_frontend_internal.h"
#include "wl_trace.h"
#include "typio/engine_manager.h"
#include "typio/typio.h"
#include "utils/log.h"

#include <wayland-client-protocol.h>

/* ── Helpers ─────────────────────────────────────────────────────── */

static void arbiter_buffer_push(TypioKeyArbiter *arbiter,
                                bool is_press,
                                uint32_t key, uint32_t keysym,
                                uint32_t modifiers, uint32_t unicode,
                                uint32_t time) {
    if (arbiter->buffer_count >= TYPIO_ARBITER_MAX_BUFFERED)
        return;  /* caller should have triggered replay before this */

    TypioArbiterEvent *ev = &arbiter->buffer[arbiter->buffer_count++];
    ev->is_press  = is_press;
    ev->key       = key;
    ev->keysym    = keysym;
    ev->modifiers = modifiers;
    ev->unicode   = unicode;
    ev->time      = time;
}

static void arbiter_replay(TypioKeyArbiter *arbiter,
                           TypioWlKeyboard *keyboard,
                           TypioWlSession *session) {
    for (size_t i = 0; i < arbiter->buffer_count; i++) {
        TypioArbiterEvent *ev = &arbiter->buffer[i];
        if (ev->is_press)
            typio_wl_keyboard_process_key_press(keyboard, session,
                                             ev->key, ev->keysym,
                                             ev->modifiers, ev->unicode,
                                             ev->time);
        else
            typio_wl_keyboard_process_key_release(keyboard, session,
                                               ev->key, ev->keysym,
                                               ev->modifiers, ev->unicode,
                                               ev->time);
    }
    arbiter->buffer_count = 0;
    arbiter->state = TYPIO_ARBITER_IDLE;
}

/**
 * Release any keys that were forwarded to the app (via virtual keyboard)
 * before buffering started, whose releases are now being consumed.
 *
 * The first chord modifier (e.g., Ctrl) was passed through in IDLE and
 * forwarded to the app.  Its release ended up in the buffer.  If we
 * discard the buffer, the app never sees the release and thinks the
 * modifier is stuck.
 */
static void arbiter_release_orphaned_keys(TypioKeyArbiter *arbiter,
                                          TypioWlKeyboard *keyboard) {
    TypioWlFrontend *frontend = keyboard->frontend;

    for (size_t i = 0; i < arbiter->buffer_count; i++) {
        TypioArbiterEvent *ev = &arbiter->buffer[i];
        if (ev->is_press)
            continue;

        /* Only release keys that are in FORWARDED state — those were
         * sent to the app before we started buffering. */
        if (ev->key >= TYPIO_WL_MAX_TRACKED_KEYS)
            continue;
        if (frontend->key_states[ev->key] != TYPIO_KEY_FORWARDED)
            continue;

        typio_wl_vk_forward_key(keyboard, ev->time, ev->key,
                                WL_KEYBOARD_KEY_STATE_RELEASED,
                                ev->unicode);
        frontend->key_states[ev->key] = TYPIO_KEY_IDLE;
        frontend->key_generations[ev->key] = 0;
    }
}

static void arbiter_consume(TypioKeyArbiter *arbiter,
                            TypioWlKeyboard *keyboard) {
    TypioWlFrontend *frontend = keyboard->frontend;
    TypioEngineManager *manager =
        typio_instance_get_engine_manager(frontend->instance);

    /* Forward releases for keys that were already forwarded to the app
     * before we entered BUFFERING (e.g., the first Ctrl or Shift). */
    arbiter_release_orphaned_keys(arbiter, keyboard);

    if (manager && typio_engine_manager_next(manager) == TYPIO_OK) {
        typio_wl_trace(frontend, "key",
                       "stage=shortcut-switch detail=ctrl+shift engine switch (arbiter)");
        typio_log(TYPIO_LOG_INFO,
                  "Switched engine via Ctrl+Shift chord (arbiter)");
    }

    arbiter->buffer_count = 0;
    arbiter->state = TYPIO_ARBITER_IDLE;
}

/**
 * Should we start buffering on this press event?
 * Enter BUFFERING when both Ctrl and Shift are physically held and
 * the pressed key is one of the chord modifiers.
 */
static bool arbiter_should_start_buffering(uint32_t keysym,
                                           uint32_t physical_modifiers) {
    if (!typio_wl_shortcut_chord_is_switch_modifier(keysym))
        return false;

    /* Both chord modifiers must be physically held (including the one
     * being pressed right now — physical_modifiers is updated before
     * the arbiter runs). */
    uint32_t required = TYPIO_MOD_CTRL | TYPIO_MOD_SHIFT;
    if ((physical_modifiers & required) != required)
        return false;

    /* Alt/Super must not be held */
    uint32_t forbidden = TYPIO_MOD_ALT | TYPIO_MOD_SUPER;
    return (physical_modifiers & forbidden) == 0;
}

/**
 * Are all chord modifiers released?
 */
static bool arbiter_chord_complete(uint32_t physical_modifiers) {
    return (physical_modifiers &
            (TYPIO_MOD_CTRL | TYPIO_MOD_SHIFT)) == 0;
}

/**
 * Should we cancel buffering because of this event?
 * Yes if: non-modifier key pressed, or Alt/Super appeared.
 */
static bool arbiter_should_cancel(uint32_t keysym,
                                  uint32_t physical_modifiers,
                                  bool is_press) {
    if (is_press && !typio_wl_shortcut_chord_is_switch_modifier(keysym))
        return true;

    if ((physical_modifiers & (TYPIO_MOD_ALT | TYPIO_MOD_SUPER)) != 0)
        return true;

    return false;
}

/* ── Public API ──────────────────────────────────────────────────── */

void typio_wl_key_arbiter_init(TypioKeyArbiter *arbiter) {
    arbiter->state = TYPIO_ARBITER_IDLE;
    arbiter->buffer_count = 0;
}

void typio_wl_key_arbiter_reset(TypioKeyArbiter *arbiter) {
    arbiter->state = TYPIO_ARBITER_IDLE;
    arbiter->buffer_count = 0;
}

void typio_wl_key_arbiter_press(TypioKeyArbiter *arbiter,
                                TypioWlKeyboard *keyboard,
                                TypioWlSession *session,
                                uint32_t key, uint32_t keysym,
                                uint32_t modifiers, uint32_t unicode,
                                uint32_t time) {
    uint32_t phys = keyboard->physical_modifiers;

    switch (arbiter->state) {
    case TYPIO_ARBITER_IDLE:
        if (arbiter_should_start_buffering(keysym, phys)) {
            arbiter->state = TYPIO_ARBITER_BUFFERING;
            arbiter_buffer_push(arbiter, true, key, keysym, modifiers,
                                unicode, time);
            typio_wl_trace(keyboard->frontend, "arbiter",
                           "stage=buffer-start keysym=0x%x phys=0x%x",
                           keysym, phys);
            return;
        }
        /* Pass through */
        typio_wl_keyboard_process_key_press(keyboard, session, key, keysym,
                                         modifiers, unicode, time);
        return;

    case TYPIO_ARBITER_BUFFERING:
        if (arbiter_should_cancel(keysym, phys, true) ||
            arbiter->buffer_count >= TYPIO_ARBITER_MAX_BUFFERED - 1) {
            /* Replay buffered events, then process this one normally */
            typio_wl_trace(keyboard->frontend, "arbiter",
                           "stage=replay reason=%s keysym=0x%x",
                           arbiter->buffer_count >= TYPIO_ARBITER_MAX_BUFFERED - 1
                               ? "overflow" : "cancel",
                           keysym);
            arbiter_replay(arbiter, keyboard, session);
            typio_wl_keyboard_process_key_press(keyboard, session, key, keysym,
                                             modifiers, unicode, time);
            return;
        }
        /* Still a potential chord — buffer */
        arbiter_buffer_push(arbiter, true, key, keysym, modifiers,
                            unicode, time);
        return;
    }
}

void typio_wl_key_arbiter_release(TypioKeyArbiter *arbiter,
                                  TypioWlKeyboard *keyboard,
                                  TypioWlSession *session,
                                  uint32_t key, uint32_t keysym,
                                  uint32_t modifiers, uint32_t unicode,
                                  uint32_t time) {
    uint32_t phys = keyboard->physical_modifiers;

    switch (arbiter->state) {
    case TYPIO_ARBITER_IDLE:
        typio_wl_keyboard_process_key_release(keyboard, session, key, keysym,
                                           modifiers, unicode, time);
        return;

    case TYPIO_ARBITER_BUFFERING:
        if (arbiter_should_cancel(keysym, phys, false)) {
            typio_wl_trace(keyboard->frontend, "arbiter",
                           "stage=replay reason=cancel-release keysym=0x%x",
                           keysym);
            arbiter_replay(arbiter, keyboard, session);
            typio_wl_keyboard_process_key_release(keyboard, session, key, keysym,
                                               modifiers, unicode, time);
            return;
        }

        /* Buffer the release */
        arbiter_buffer_push(arbiter, false, key, keysym, modifiers,
                            unicode, time);

        /* Check if the chord is complete (all chord mods released) */
        if (arbiter_chord_complete(phys)) {
            typio_wl_trace(keyboard->frontend, "arbiter",
                           "stage=consume detail=chord complete");
            arbiter_consume(arbiter, keyboard);
            return;
        }
        return;
    }
}

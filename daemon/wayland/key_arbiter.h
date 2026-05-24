/**
 * @file key_arbiter.h
 * @brief Key event arbiter — buffers modifier events during potential
 *        system shortcut sequences and either consumes or replays them
 */

#ifndef TYPIO_WL_KEY_ARBITER_H
#define TYPIO_WL_KEY_ARBITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct TypioWlKeyboard;
struct TypioWlSession;

typedef enum {
    TYPIO_ARBITER_IDLE = 0,
    TYPIO_ARBITER_BUFFERING,
} TypioArbiterState;

#define TYPIO_ARBITER_MAX_BUFFERED 8

typedef struct {
    bool is_press;
    uint32_t key;
    uint32_t keysym;
    uint32_t modifiers;
    uint32_t unicode;
    uint32_t time;
} TypioArbiterEvent;

typedef struct {
    TypioArbiterState state;
    TypioArbiterEvent buffer[TYPIO_ARBITER_MAX_BUFFERED];
    size_t buffer_count;
} TypioKeyArbiter;

void typio_wl_key_arbiter_init(TypioKeyArbiter *arbiter);
void typio_wl_key_arbiter_reset(TypioKeyArbiter *arbiter);

void typio_wl_key_arbiter_press(TypioKeyArbiter *arbiter,
                                struct TypioWlKeyboard *keyboard,
                                struct TypioWlSession *session,
                                uint32_t key, uint32_t keysym,
                                uint32_t modifiers, uint32_t unicode,
                                uint32_t time);

void typio_wl_key_arbiter_release(TypioKeyArbiter *arbiter,
                                  struct TypioWlKeyboard *keyboard,
                                  struct TypioWlSession *session,
                                  uint32_t key, uint32_t keysym,
                                  uint32_t modifiers, uint32_t unicode,
                                  uint32_t time);

#endif /* TYPIO_WL_KEY_ARBITER_H */

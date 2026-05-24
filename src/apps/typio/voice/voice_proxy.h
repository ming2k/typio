/**
 * @file voice_proxy.h
 * @brief Reference-counted proxy around a TypioVoiceBackend
 *
 * Sits between the engine ABI and a concrete speech-to-text backend so the
 * inference thread, the main thread (engine_manager swap / unload / engine
 * deactivate), and the optional background reload thread can all touch the
 * backend safely.
 *
 * Threading contract
 * ------------------
 *   - process() may run on any thread. While the call is in flight, the
 *     backend it received is guaranteed valid even if a concurrent reload,
 *     deactivate, or destroy happens.
 *   - reload_begin / reload_end / deactivate / destroy may be called from
 *     the main thread at any time, even while process() is in flight.
 *   - destroy() may return before the backend memory is actually released —
 *     the proxy frees itself when the last in-flight process() drops its
 *     refcount.
 *
 * Why this lives here
 * -------------------
 * Both whisper and sherpa proxies used to carry their own near-identical
 * copy of this state machine. Two copies of tricky concurrency code is a
 * latent divergence bug; one tested copy is the right shape.
 */

#ifndef TYPIO_VOICE_PROXY_H
#define TYPIO_VOICE_PROXY_H

#include "voice_backend.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque proxy state. Its first field is a TypioVoiceBackend with proxy ops,
 * so engine->user_data can be cast to TypioVoiceBackend * unchanged.
 */
typedef struct TypioVoiceProxy TypioVoiceProxy;

/**
 * Create a new proxy wrapping @c initial_impl. Takes ownership of
 * @c initial_impl (may be NULL — proxy starts in "no model" state).
 * Returns NULL on allocation failure, in which case @c initial_impl is
 * destroyed.
 */
TypioVoiceProxy *typio_voice_proxy_new(TypioVoiceBackend *initial_impl);

/**
 * Get the proxy as a TypioVoiceBackend. Use this as @c engine->user_data
 * and as the argument to process_audio.
 */
TypioVoiceBackend *typio_voice_proxy_as_backend(TypioVoiceProxy *proxy);

/**
 * True iff a backend is currently loaded and the proxy has not been
 * marked for destruction. Cheap; safe to call from any thread.
 */
bool typio_voice_proxy_is_ready(TypioVoiceProxy *proxy);

/**
 * Mark the proxy for destruction. The proxy may free itself synchronously
 * if no work is in flight, or defer the free until in-flight process() /
 * reload completes. After this call, the proxy pointer must not be used
 * by the caller.
 */
void typio_voice_proxy_destroy(TypioVoiceProxy *proxy);

/**
 * Free the current backend and leave the proxy alive (used by engine
 * deactivate to release model memory while keeping the proxy slot for a
 * later focus_in reload). Safe to call while process() is in flight; the
 * old backend is parked and freed once refcount drops to zero.
 */
void typio_voice_proxy_clear_impl(TypioVoiceProxy *proxy);

/**
 * Install @c new_impl as the active backend (used by lazy focus_in load
 * paths that do not go through reload_begin/end). Any existing backend
 * is freed via the same safe-defer path. Takes ownership of @c new_impl.
 */
void typio_voice_proxy_set_impl(TypioVoiceProxy *proxy,
                                 TypioVoiceBackend *new_impl);

/**
 * Try to claim the reload slot. Returns true on success (caller must
 * later call reload_end), false if a reload is already in progress.
 */
bool typio_voice_proxy_reload_begin(TypioVoiceProxy *proxy);

/**
 * Complete the reload: install @c new_impl (may be NULL on load failure)
 * and clear the reload-in-flight flag. Takes ownership of @c new_impl.
 * Frees the old backend via the safe-defer path. If destroy() was called
 * during the reload, the proxy finalizes itself here.
 */
void typio_voice_proxy_reload_end(TypioVoiceProxy *proxy,
                                   TypioVoiceBackend *new_impl);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_VOICE_PROXY_H */

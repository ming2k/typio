/**
 * @file session_checkpoint.h
 * @brief Durable composition checkpoint across daemon restarts
 *
 * Persists the active keyboard engine's in-flight composition to a small
 * file under $XDG_RUNTIME_DIR so a daemon crash + systemd restart can
 * resume a half-typed composition instead of losing it. Only engines that
 * implement the optional snapshot/restore ops (see engine.h) participate;
 * for everything else these calls are cheap no-ops.
 *
 * The wire format and its validation live in checkpoint_codec.{c,h}; this
 * module is the effectful glue: path resolution, atomic file I/O, and the
 * engine/frontend handshake.
 */

#ifndef TYPIO_WL_SESSION_CHECKPOINT_H
#define TYPIO_WL_SESSION_CHECKPOINT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct TypioWlFrontend;

/**
 * Snapshot the active engine's composition and write it atomically. A
 * no-op when there is no session, no active keyboard engine, the engine
 * lacks session ops, or the engine reports nothing to persist (in which
 * case any stale checkpoint is discarded).
 */
void typio_wl_session_checkpoint_save(struct TypioWlFrontend *frontend);

/**
 * Read a checkpoint and, if it is well-formed, fresh, and belongs to the
 * current active engine, replay it into the session's input context. The
 * checkpoint is discarded after a restore attempt (success or rejection)
 * so it is never replayed twice. Returns true only when a composition was
 * actually restored.
 */
bool typio_wl_session_checkpoint_try_restore(struct TypioWlFrontend *frontend);

/** Remove any persisted checkpoint. Called once a composition commits. */
void typio_wl_session_checkpoint_discard(struct TypioWlFrontend *frontend);

#ifdef __cplusplus
}
#endif

#endif /* TYPIO_WL_SESSION_CHECKPOINT_H */

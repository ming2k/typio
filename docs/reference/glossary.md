# Glossary

Quick-reference definitions for terms used across Typio documentation, code, and protocols.

Entries link to detailed documentation where applicable; related terms are cross-referenced so you can follow a concept chain when learning.

---

## C

**Candidate**
: An alternative conversion result produced by the active engine. In Chinese IMEs candidates are hanzi; in Japanese they are kanji; in emoji search they are matching symbols. Candidates are displayed in a popup near the cursor and selected by number keys, arrow keys, or Space.

  *See also: [Preedit](#p), [Commit](#c), [Composition](#c), [Popup](#p).*

**Commit**
: The action of sending finalized text to the client application. Once committed, the text leaves Typio's control and becomes ordinary document content. The engine triggers a commit by calling `typio_input_context_commit()`; the frontend translates it into a `commit_string` protocol request.

  *See also: [Preedit](#p), [Candidate](#c), [Composition](#c).*

**Composition / Composing state**
: The state in which the user has typed something that is not yet committed. During composition there is usually a **preedit** string visible at the cursor and a **candidate** list available for selection. The four `process_key` return codes (`NOT_HANDLED`, `HANDLED`, `COMPOSING`, `COMMITTED`) describe how a single keystroke affects this state.

  *See also: [Preedit](#p), [Candidate](#c), [Commit](#c).*

**Core (`typio-core`)**
: The platform-agnostic Rust library at the center of Typio. It owns `TypioInstance`, `TypioInputContext`, engine loading, config parsing, and the engine ABI. It knows nothing about Wayland, D-Bus, GTK, or audio. See [Architecture Overview §Core ↔ Apps Boundary](../explanation/architecture-overview.md#core--apps-boundary).

  *See also: [Frontend](#f), [Engine](#e).*

---

## E

**Engine**
: A plugin (shared object or built-in) that implements language logic: turning key events into text. Examples: `rime` (Chinese), `mozc` (Japanese), `basic` (Latin direct input), voice backends (speech-to-text). Engines communicate with Typio through a narrow C ABI — three data-structure emits and four return codes.

  *See also: [Core](#c), [IME](#i), [Input context](#i), [Property bag](#p), [Session](#s).*

---

## F

**Focus churn**
: Rapid `focus_out` → `focus_in` sequences caused by window switching, popup menus, modal dialogs, or notification bubbles. Typio deliberately preserves engine **sessions** across focus churn so the user does not lose a half-typed composition.

  *See also: [Session](#s), [Input context](#i).*

**Frontend**
: The Wayland-facing part of the daemon (`daemon/wayland/`). It is the system-specific adapter that binds compositor protocols, manages the keyboard **grab**, translates `wl_keyboard` events into `TypioKeyEvent`, and turns engine callbacks back into Wayland requests (`set_preedit_string`, `commit_string`, popup geometry). The frontend knows everything about the platform but delegates all linguistic decisions to **core**.

  *See also: [Core](#c), [Grab](#g), [Virtual keyboard](#v).*

---

## G

**Grab / Keyboard grab**
: A Wayland protocol object (`zwp_input_method_keyboard_grab_v2`) that gives Typio exclusive access to the raw keyboard event stream while the input method is active. Without the grab, keys would flow directly to the client application and Typio could not intercept them for composition or candidate navigation. Because a stuck grab can lock the user's keyboard, Typio implements safety backstops (emergency-exit shortcut, rejected-press failsafe); a grab that goes *missing* relative to intent is rebuilt by the per-step **reconciler** diff, while a grab left *dead-but-present* (e.g. across suspend) is recovered via a fact source — see [Reconciler](#r).

  *See also: [Reconciler](#r), [Virtual keyboard](#v), [Frontend](#f).*

---

## I

**IME (Input Method Editor)**
: In the Typio documentation "IME" usually refers to the entire Typio daemon and framework — the host that manages protocols, UI, and engine scheduling — rather than a single engine. The **engine** is the language-specific plugin; the IME is the container that loads and runs it.

  *See also: [Engine](#e), [Frontend](#f), [Core](#c).*

**Input context (`TypioInputContext`)**
: Per-focus state carrier created by the framework and passed to engines. It holds the current preedit, candidate list, cursor position, and a **property bag** for engine-private data. An input context survives focus churn; only its visible UI is hidden on `focus_out`.

  *See also: [Property bag](#p), [Session](#s), [Composition](#c).*

---

## P

**Popup**
: The floating window that displays the **candidate** list near the text cursor. On Wayland it is implemented through `zwp_input_popup_surface_v2` and rendered with a Vulkan swapchain (no SHM buffers).

  *See also: [Candidate](#c), [Preedit](#p), [Frontend](#f).*

**Preedit**
: The uncommitted text shown inline at the cursor while the user is still composing. For example, typing pinyin `"ni"` with a Chinese engine produces preedit `"ni"` (underlined) while the user has not yet selected the hanzi. Preedit is one part of the engine's [composition](#c) (alongside candidates) and is cleared on commit or reset.

  *See also: [Candidate](#c), [Commit](#c), [Composition](#c).*

**Property bag**
: A key-value store attached to every `TypioInputContext` that lets engines save opaque pointers (session handles, cached modes, partial buffers) without the framework knowing their types. The framework holds the pointer and calls the registered destructor when the context is destroyed.

  *See also: [Input context](#i), [Session](#s), [Engine](#e).*

---

## R

**Reconciler**
: The self-correcting step the frontend runs every event-loop iteration: `desired = reduce(facts)`, `actual = observe(resources)`, then `apply(diff(desired, actual))`. Because the session state is *derived* (never stored) and the effects are idempotent, any divergence **`observe()` can see** — a resource missing relative to intent, a missed event — is repaired by the next step with no separate recovery path or divergence timer. It is a backstop for the frontend's *own* state, not a detector of external silent loss (a dead-but-present grab needs a fact source) ([ADR-0011](../adr/0011-composition-and-lifecycle-rewrite.md)). See [Lifecycle Resilience](../explanation/lifecycle-resilience.md#what-the-diff-can--and-cannot--see).

  *See also: [Grab](#g), [Frontend](#f), [Session](#s).*

---

## S

**Session (engine session)**
: Engine-specific per-context state that survives focus changes. For Rime this is a `RimeSessionId`; for Mozc it would be a protocol buffer. Sessions live in the **property bag** and are preserved across `focus_out`/`focus_in`, but are dropped on `reset` or daemon restart unless the engine implements `snapshot_session` / `restore_session`.

  *See also: [Property bag](#p), [Input context](#i), [Focus churn](#f).*

---

## V

**Virtual keyboard (`zwp_virtual_keyboard_v1`)**
: A separate Wayland protocol object Typio creates to forward keys the engine does **not** consume back to the client application. When the user presses a key that the engine returns `TYPIO_KEY_NOT_HANDLED` for, Typio re-emits it through the virtual keyboard so the client sees the original press/release pair. Virtual-keyboard health is tracked independently from grab health. See [Timing Model](../explanation/timing-model.md).

  *See also: [Grab](#g), [Frontend](#f), [Engine](#e).*

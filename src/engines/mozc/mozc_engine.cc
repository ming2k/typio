/**
 * @file mozc_engine.cc
 * @brief Japanese input engine powered by Mozc
 *
 * Communicates with mozc_server via Unix domain socket IPC using protobuf.
 * No Mozc client library needed — only a running mozc_server.
 */

extern "C" {
#include "typio/typio.h"
#include "typio_build_config.h"
#include "utils/log.h"
#include "utils/string.h"
}

#include "mozc_commands.pb.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <poll.h>
#include <pwd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* ---------------------------------------------------------------------------
 * Mozc IPC client
 * ---------------------------------------------------------------------------
 * Protocol: connect → write [size:4 LE][Command] → read [size:4 LE][Command]
 * Each RPC is a fresh connection (Mozc server expects this).
 * Socket path: ~/.mozc/session.ipc (or $XDG_CONFIG_HOME/mozc/session.ipc)
 * -------------------------------------------------------------------------*/

#define MOZC_SESSION_KEY "mozc.session"
#define MOZC_IPC_TIMEOUT_MS 300
#define MOZC_RETRY_BACKOFF_MS 3000
#define MOZC_DEFAULT_PAGE_SIZE 9

#ifndef MOZC_SERVER_PATH
#define MOZC_SERVER_PATH "/usr/lib/mozc/mozc_server"
#endif

struct TypioMozcConfig {
    char *server_path;
    int page_size;
};

struct TypioMozcState {
    TypioMozcConfig config;
    std::string socket_path;
    bool server_launched;
    uint64_t retry_after_ms;
};

struct TypioMozcSession {
    TypioMozcState *state;
    uint64_t session_id;
    mozc::commands::CompositionMode mode;
};

static bool mozc_mode_is_ascii(mozc::commands::CompositionMode mode) {
    switch (mode) {
    case mozc::commands::DIRECT:
    case mozc::commands::HALF_ASCII:
    case mozc::commands::FULL_ASCII:
        return true;
    default:
        return false;
    }
}

static bool mozc_sync_output(const mozc::commands::Output &output,
                             TypioInputContext *ctx,
                             TypioMozcSession *session);

static const char *mozc_input_type_name(mozc::commands::Input::CommandType type) {
    return mozc::commands::Input::CommandType_Name(type).c_str();
}

static std::string mozc_format_socket_path(const std::string &socket_path) {
    if (!socket_path.empty() && socket_path[0] == '\0') {
        return "@" + socket_path.substr(1);
    }

    return socket_path;
}

/* -- helpers -------------------------------------------------------------- */

static uint64_t mozc_monotonic_ms() {
    struct timespec ts = {};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
           static_cast<uint64_t>(ts.tv_nsec / 1000000L);
}

static std::string mozc_find_abstract_socket_path() {
    FILE *fp = fopen("/proc/net/unix", "r");
    if (!fp) {
        return {};
    }

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char *path = strchr(line, '@');
        if (!path) {
            continue;
        }

        char *newline = strchr(path, '\n');
        if (newline) {
            *newline = '\0';
        }

        if (strncmp(path, "@tmp/.mozc.", 11) == 0 &&
            strstr(path, ".session") != nullptr) {
            std::string abstract_path;
            abstract_path.push_back('\0');
            abstract_path.append(path + 1);
            fclose(fp);
            return abstract_path;
        }
    }

    fclose(fp);
    return {};
}

static std::string mozc_get_socket_path() {
    std::string abstract_path = mozc_find_abstract_socket_path();
    if (!abstract_path.empty()) {
        return abstract_path;
    }

    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        return std::string(xdg) + "/mozc/session.ipc";
    }

    const char *home = getenv("HOME");
    if (!home || !*home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : nullptr;
    }
    if (!home) {
        return {};
    }

    return std::string(home) + "/.mozc/session.ipc";
}

static bool mozc_connect_socket(int fd,
                                const std::string &socket_path,
                                int *error_out) {
    struct sockaddr_un addr = {};
    socklen_t addrlen;

    addr.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(addr.sun_path)) {
        if (error_out) {
            *error_out = ENAMETOOLONG;
        }
        return false;
    }

    memcpy(addr.sun_path, socket_path.data(), socket_path.size());

    if (!socket_path.empty() && socket_path[0] == '\0') {
        addrlen = static_cast<socklen_t>(
            offsetof(struct sockaddr_un, sun_path) + socket_path.size());
    } else {
        addr.sun_path[socket_path.size()] = '\0';
        addrlen = static_cast<socklen_t>(
            offsetof(struct sockaddr_un, sun_path) + socket_path.size() + 1);
    }

    if (connect(fd, reinterpret_cast<struct sockaddr *>(&addr), addrlen) == 0) {
        if (error_out) {
            *error_out = 0;
        }
        return true;
    }

    if (error_out) {
        *error_out = errno;
    }
    return false;
}

static bool mozc_ipc_call(const std::string &socket_path,
                          const mozc::commands::Input &request,
                          mozc::commands::Output *response) {
    const char *request_name =
        mozc_input_type_name(request.type());
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        typio_log_error("Mozc IPC %s: socket() failed: %s",
                        request_name,
                        strerror(errno));
        return false;
    }

    if (socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
        typio_log_error("Mozc IPC %s: socket path too long: %s",
                        request_name,
                        mozc_format_socket_path(socket_path).c_str());
        close(fd);
        return false;
    }

    int connect_error = 0;
    if (!mozc_connect_socket(fd, socket_path, &connect_error)) {
        typio_log_error("Mozc IPC %s: connect(%s) failed: %s",
                        request_name,
                        mozc_format_socket_path(socket_path).c_str(),
                        strerror(connect_error));
        close(fd);
        return false;
    }

    /* serialize request */
    std::string req_data;
    if (!request.SerializeToString(&req_data)) {
        typio_log_error("Mozc IPC %s: SerializeToString failed", request_name);
        close(fd);
        return false;
    }

    if (write(fd, req_data.data(), req_data.size()) != static_cast<ssize_t>(req_data.size())) {
        typio_log_error("Mozc IPC %s: write failed: %s",
                        request_name,
                        strerror(errno));
        close(fd);
        return false;
    }

    if (shutdown(fd, SHUT_WR) != 0) {
        typio_log_error("Mozc IPC %s: shutdown(SHUT_WR) failed: %s",
                        request_name,
                        strerror(errno));
        close(fd);
        return false;
    }

    /* poll for response */
    struct pollfd pfd = {fd, POLLIN, 0};
    int poll_result = poll(&pfd, 1, MOZC_IPC_TIMEOUT_MS);
    if (poll_result <= 0) {
        if (poll_result == 0) {
            typio_log_error("Mozc IPC %s: timed out waiting for response from %s",
                            request_name,
                            mozc_format_socket_path(socket_path).c_str());
        } else {
            typio_log_error("Mozc IPC %s: poll failed: %s",
                            request_name,
                            strerror(errno));
        }
        close(fd);
        return false;
    }

    std::string resp_data;
    char buffer[4096];
    while (true) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n == 0) {
            break;
        }
        if (n < 0) {
            typio_log_error("Mozc IPC %s: failed to read response payload: %s",
                            request_name,
                            strerror(errno));
            close(fd);
            return false;
        }
        resp_data.append(buffer, static_cast<size_t>(n));
        if (resp_data.size() > (1u << 20)) {
            typio_log_error("Mozc IPC %s: response too large: %zu bytes",
                            request_name,
                            resp_data.size());
            close(fd);
            return false;
        }
    }
    close(fd);

    if (resp_data.empty()) {
        typio_log_error("Mozc IPC %s: empty response payload", request_name);
        return false;
    }

    if (!response->ParseFromString(resp_data)) {
        typio_log_error("Mozc IPC %s: failed to parse %zu-byte protobuf response",
                        request_name,
                        resp_data.size());
        return false;
    }

    typio_log_debug("Mozc IPC %s: response ok size=%zu output_id=%llu consumed=%s error=%d",
                    request_name,
                    resp_data.size(),
                    response->has_id()
                        ? static_cast<unsigned long long>(response->id())
                        : 0ULL,
                    (response->has_consumed() && response->consumed())
                        ? "yes"
                        : "no",
                    response->has_error_code()
                        ? static_cast<int>(response->error_code())
                        : -1);
    return true;
}

static bool mozc_launch_server(const char *server_path) {
    pid_t pid = fork();
    if (pid < 0) {
        typio_log_error("Mozc server launch failed for %s: fork(): %s",
                        server_path ? server_path : "(null)",
                        strerror(errno));
        return false;
    }
    if (pid == 0) {
        setsid();
        execl(server_path, server_path, nullptr);
        _exit(1);
    }
    /* give the server a moment to start */
    usleep(300000);
    return true;
}

static bool mozc_ensure_connection(TypioMozcState *state) {
    /* quick check: can we connect? */
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        typio_log_error("Mozc connection check: socket() failed: %s", strerror(errno));
        return false;
    }

    int saved_errno = 0;
    bool ok = mozc_connect_socket(fd, state->socket_path, &saved_errno);
    close(fd);

    if (ok) {
        typio_log_debug("Mozc connection check ok: %s",
                        mozc_format_socket_path(state->socket_path).c_str());
        return true;
    }

    if (state->server_launched) {
        std::string refreshed_path = mozc_get_socket_path();
        if (!refreshed_path.empty() && refreshed_path != state->socket_path) {
            typio_log_info("Mozc socket path updated: %s -> %s",
                           mozc_format_socket_path(state->socket_path).c_str(),
                           mozc_format_socket_path(refreshed_path).c_str());
            state->socket_path = refreshed_path;

            fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (fd >= 0) {
                ok = mozc_connect_socket(fd, state->socket_path, &saved_errno);
                close(fd);
                if (ok) {
                    typio_log_debug("Mozc connection check ok after refresh: %s",
                                    mozc_format_socket_path(state->socket_path).c_str());
                    return true;
                }
            }
        }

        typio_log_error("Mozc connection check failed after launch attempt: connect(%s): %s",
                        mozc_format_socket_path(state->socket_path).c_str(),
                        strerror(saved_errno));
        return false;
    }

    typio_log_info("Mozc server not running, launching: %s", state->config.server_path);
    state->server_launched = true;
    if (!mozc_launch_server(state->config.server_path)) {
        return false;
    }

    return true; /* optimistic — session creation will verify */
}

/* -- session management --------------------------------------------------- */

static bool mozc_create_session(TypioMozcState *state, uint64_t *session_id) {
    uint64_t now_ms;

    if (!state || !session_id) {
        return false;
    }

    now_ms = mozc_monotonic_ms();
    if (state->retry_after_ms != 0 && now_ms < state->retry_after_ms) {
        typio_log_debug("Mozc CREATE_SESSION skipped until %llu ms (now=%llu)",
                        static_cast<unsigned long long>(state->retry_after_ms),
                        static_cast<unsigned long long>(now_ms));
        return false;
    }

    if (!mozc_ensure_connection(state)) {
        typio_log_error("Mozc CREATE_SESSION aborted: no IPC connection to %s",
                        mozc_format_socket_path(state->socket_path).c_str());
        state->retry_after_ms = now_ms + MOZC_RETRY_BACKOFF_MS;
        return false;
    }

    mozc::commands::Input input;
    input.set_type(mozc::commands::Input::CREATE_SESSION);
    input.mutable_capability()->set_text_deletion(
        mozc::commands::Capability::DELETE_PRECEDING_TEXT);
    input.mutable_application_info()->set_process_id(static_cast<uint32_t>(getpid()));
    input.mutable_application_info()->set_thread_id(static_cast<uint32_t>(getpid()));

    mozc::commands::Output resp;
    if (!mozc_ipc_call(state->socket_path, input, &resp)) {
        typio_log_error("Mozc CREATE_SESSION IPC failed via %s",
                        mozc_format_socket_path(state->socket_path).c_str());
        state->retry_after_ms = mozc_monotonic_ms() + MOZC_RETRY_BACKOFF_MS;
        return false;
    }

    if ((resp.has_error_code() &&
         resp.error_code() != mozc::commands::Output::SESSION_SUCCESS) ||
        !resp.has_id() || resp.id() == 0) {
        typio_log_error("Mozc CREATE_SESSION returned no valid session id "
                        "(id=%llu error=%d)",
                        resp.has_id() ? static_cast<unsigned long long>(resp.id()) : 0ULL,
                        resp.has_error_code()
                            ? static_cast<int>(resp.error_code())
                            : -1);
        state->retry_after_ms = mozc_monotonic_ms() + MOZC_RETRY_BACKOFF_MS;
        return false;
    }

    *session_id = resp.id();
    state->retry_after_ms = 0;
    return true;
}

static void mozc_delete_session(TypioMozcState *state, uint64_t session_id) {
    mozc::commands::Input input;
    input.set_type(mozc::commands::Input::DELETE_SESSION);
    input.set_id(session_id);

    mozc::commands::Output output;
    mozc_ipc_call(state->socket_path, input, &output);
}

static bool mozc_send_key(TypioMozcState *state, uint64_t session_id,
                          const mozc::commands::KeyEvent &key,
                          mozc::commands::Output *output) {
    mozc::commands::Input input;
    input.set_type(mozc::commands::Input::SEND_KEY);
    input.set_id(session_id);
    *input.mutable_key() = key;

    if (!mozc_ipc_call(state->socket_path, input, output)) {
        return false;
    }
    return true;
}

static bool mozc_update_session_mode(TypioMozcSession *session,
                                     const mozc::commands::Output &output) {
    if (!session) {
        return false;
    }

    if (output.has_mode()) {
        session->mode = output.mode();
        return true;
    }

    if (output.has_status() && output.status().has_mode()) {
        session->mode = output.status().mode();
        return true;
    }

    return false;
}

static bool mozc_activate_session(TypioMozcSession *session,
                                  TypioInputContext *ctx) {
    if (!session || !session->state || session->session_id == 0) {
        return false;
    }

    mozc::commands::KeyEvent key;
    key.set_special_key(mozc::commands::KeyEvent::ON);
    key.set_mode(mozc::commands::HIRAGANA);
    key.add_modifier_keys(mozc::commands::KeyEvent::KEY_DOWN);

    mozc::commands::Output output;
    if (!mozc_send_key(session->state, session->session_id, key, &output)) {
        return false;
    }

    mozc_update_session_mode(session, output);
    if (ctx) {
        mozc_sync_output(output, ctx, session);
    }

    return !mozc_mode_is_ascii(session->mode);
}

static bool mozc_send_command(TypioMozcState *state, uint64_t session_id,
                              const mozc::commands::SessionCommand &session_cmd,
                              mozc::commands::Output *output) {
    mozc::commands::Input input;
    input.set_type(mozc::commands::Input::SEND_COMMAND);
    input.set_id(session_id);
    *input.mutable_command() = session_cmd;

    if (!mozc_ipc_call(state->socket_path, input, output)) {
        return false;
    }
    return true;
}

/* -- keysym → Mozc KeyEvent conversion ------------------------------------ */

static bool mozc_map_special_key(uint32_t keysym,
                                 mozc::commands::KeyEvent::SpecialKey *out) {
    using SK = mozc::commands::KeyEvent;
    switch (keysym) {
    case TYPIO_KEY_space:     *out = SK::SPACE;     return true;
    case TYPIO_KEY_Return:    *out = SK::ENTER;     return true;
    case TYPIO_KEY_KP_Enter:  *out = SK::ENTER;     return true;
    case TYPIO_KEY_BackSpace: *out = SK::BACKSPACE;  return true;
    case TYPIO_KEY_Delete:    *out = SK::DEL;       return true;
    case TYPIO_KEY_Escape:    *out = SK::ESCAPE;    return true;
    case TYPIO_KEY_Tab:       *out = SK::TAB;       return true;
    case TYPIO_KEY_Left:      *out = SK::LEFT;      return true;
    case TYPIO_KEY_Right:     *out = SK::RIGHT;     return true;
    case TYPIO_KEY_Up:        *out = SK::UP;        return true;
    case TYPIO_KEY_Down:      *out = SK::DOWN;      return true;
    case TYPIO_KEY_Home:      *out = SK::HOME;      return true;
    case TYPIO_KEY_End:       *out = SK::END;       return true;
    case TYPIO_KEY_Page_Up:   *out = SK::PAGE_UP;   return true;
    case TYPIO_KEY_Page_Down: *out = SK::PAGE_DOWN; return true;
    case TYPIO_KEY_F1:  *out = SK::F1;  return true;
    case TYPIO_KEY_F2:  *out = SK::F2;  return true;
    case TYPIO_KEY_F3:  *out = SK::F3;  return true;
    case TYPIO_KEY_F4:  *out = SK::F4;  return true;
    case TYPIO_KEY_F5:  *out = SK::F5;  return true;
    case TYPIO_KEY_F6:  *out = SK::F6;  return true;
    case TYPIO_KEY_F7:  *out = SK::F7;  return true;
    case TYPIO_KEY_F8:  *out = SK::F8;  return true;
    case TYPIO_KEY_F9:  *out = SK::F9;  return true;
    case TYPIO_KEY_F10: *out = SK::F10; return true;
    case TYPIO_KEY_F11: *out = SK::F11; return true;
    case TYPIO_KEY_F12: *out = SK::F12; return true;
    /* Japanese-specific keys (XKB keysyms) */
    case 0xff22: *out = SK::MUHENKAN; return true; /* Muhenkan */
    case 0xff23: *out = SK::HENKAN;   return true; /* Henkan */
    case 0xff24: *out = SK::HENKAN;   return true; /* Romaji / Henkan alias */
    case 0xff25: *out = SK::KANA;     return true; /* Hiragana */
    case 0xff27: *out = SK::KANA;     return true; /* Hiragana_Katakana */
    case 0xff2a: *out = SK::HANKAKU;  return true; /* Zenkaku_Hankaku */
    case 0xff30: *out = SK::ON;       return true; /* Eisu_toggle */
    case 0xff31: *out = SK::KANA;     return true; /* Hangul / used as Kana */
    default: return false;
    }
}

static bool mozc_build_key_event(const TypioKeyEvent *event,
                                 mozc::commands::KeyEvent *key) {
    using MK = mozc::commands::KeyEvent;

    /* modifier-only keys are still sent to Mozc for mode toggling */
    bool has_content = false;

    /* special key? */
    MK::SpecialKey sk;
    if (mozc_map_special_key(event->keysym, &sk)) {
        key->set_special_key(sk);
        has_content = true;
    }

    /* printable ASCII */
    if (!has_content && event->keysym >= 0x20 && event->keysym <= 0x7e) {
        key->set_key_code(event->keysym);
        has_content = true;
    }

    /* modifier flags */
    if (event->modifiers & TYPIO_MOD_SHIFT) {
        key->add_modifier_keys(MK::SHIFT);
    }
    if (event->modifiers & TYPIO_MOD_CTRL) {
        key->add_modifier_keys(MK::CTRL);
    }
    if (event->modifiers & TYPIO_MOD_ALT) {
        key->add_modifier_keys(MK::ALT);
    }

    /* key direction */
    if (event->type == TYPIO_EVENT_KEY_RELEASE) {
        key->add_modifier_keys(MK::KEY_UP);
    }

    /* modifier-only keypresses (Shift, Ctrl, etc.) */
    if (!has_content && typio_key_event_is_modifier_only(event)) {
        has_content = true;
    }

    return has_content;
}

/* -- sync Mozc output → Typio context ------------------------------------- */

static void mozc_clear_state(TypioInputContext *ctx) {
    typio_input_context_clear_preedit(ctx);
    typio_input_context_clear_candidates(ctx);
}

static bool mozc_sync_output(const mozc::commands::Output &output,
                             TypioInputContext *ctx,
                             TypioMozcSession *session) {
    bool composing = false;

    /* committed text */
    if (output.has_result() &&
        output.result().type() == mozc::commands::Result::STRING &&
        !output.result().value().empty()) {
        typio_input_context_commit(ctx, output.result().value().c_str());
    }

    /* preedit */
    if (output.has_preedit() && output.preedit().segment_size() > 0) {
        const auto &preedit = output.preedit();
        int seg_count = preedit.segment_size();

        auto *segments = static_cast<TypioPreeditSegment *>(
            calloc(static_cast<size_t>(seg_count), sizeof(TypioPreeditSegment)));
        if (segments) {
            for (int i = 0; i < seg_count; ++i) {
                segments[i].text = preedit.segment(i).value().c_str();
                segments[i].format =
                    preedit.segment(i).annotation() ==
                            mozc::commands::Preedit::Segment::HIGHLIGHT
                        ? TYPIO_PREEDIT_HIGHLIGHT
                        : TYPIO_PREEDIT_UNDERLINE;
            }

            TypioPreedit tp = {
                .segments = segments,
                .segment_count = static_cast<size_t>(seg_count),
                .cursor_pos = preedit.has_cursor() ? static_cast<int>(preedit.cursor()) : 0,
            };
            typio_input_context_set_preedit(ctx, &tp);
            free(segments);
            composing = true;
        }
    } else {
        typio_input_context_clear_preedit(ctx);
    }

    /* candidates */
    if (output.has_candidate_window() && output.candidate_window().candidate_size() > 0) {
        const auto &cands = output.candidate_window();
        int count = cands.candidate_size();

        auto *items = static_cast<TypioCandidate *>(
            calloc(static_cast<size_t>(count), sizeof(TypioCandidate)));
        auto **labels = static_cast<char **>(
            calloc(static_cast<size_t>(count), sizeof(char *)));

        if (items && labels) {
            int page_size = cands.has_page_size() ? static_cast<int>(cands.page_size())
                                                  : MOZC_DEFAULT_PAGE_SIZE;
            int total = cands.has_size() ? static_cast<int>(cands.size()) : count;
            int focused = cands.has_focused_index()
                              ? static_cast<int>(cands.focused_index())
                              : -1;

            /* compute pagination */
            int first_index = count > 0 && cands.candidate(0).has_index()
                                  ? static_cast<int>(cands.candidate(0).index())
                                  : 0;

            for (int i = 0; i < count; ++i) {
                const auto &cw = cands.candidate(i);
                items[i].text = cw.value().c_str();
                items[i].comment =
                    (cw.has_annotation() && cw.annotation().has_description())
                        ? cw.annotation().description().c_str()
                        : nullptr;

                /* label: shortcut from annotation, or positional number */
                if (cw.has_annotation() && cw.annotation().has_shortcut()) {
                    items[i].label = cw.annotation().shortcut().c_str();
                } else {
                    char buf[16];
                    snprintf(buf, sizeof(buf), "%d", (i % page_size) + 1);
                    labels[i] = typio_strdup(buf);
                    items[i].label = labels[i];
                }
            }

            TypioCandidateList list = {
                .candidates = items,
                .count = static_cast<size_t>(count),
                .page = page_size > 0 ? first_index / page_size : 0,
                .page_size = page_size,
                .total = total,
                .selected = focused >= 0 ? focused - first_index : -1,
                .has_prev = first_index > 0,
                .has_next = first_index + count < total,
                .content_signature = 0,
            };
            typio_input_context_set_candidates(ctx, &list);
            composing = true;
        }

        if (labels) {
            for (int i = 0; i < count; ++i) {
                free(labels[i]);
            }
        }
        free(labels);
        free(items);
    } else {
        typio_input_context_clear_candidates(ctx);
    }

    /* track composition mode for status icon */
    mozc_update_session_mode(session, output);

    return composing;
}

/* -- session property management ------------------------------------------ */

static void mozc_free_session(void *data) {
    auto *session = static_cast<TypioMozcSession *>(data);
    if (!session) {
        return;
    }
    if (session->state && session->session_id != 0) {
        mozc_delete_session(session->state, session->session_id);
    }
    free(session);
}

static TypioMozcSession *mozc_get_session(TypioEngine *engine,
                                          TypioInputContext *ctx,
                                          bool create) {
    auto *state = static_cast<TypioMozcState *>(typio_engine_get_user_data(engine));
    if (!state) {
        return nullptr;
    }

    auto *session = static_cast<TypioMozcSession *>(
        typio_input_context_get_property(ctx, MOZC_SESSION_KEY));
    if (session && session->session_id != 0) {
        return session;
    }

    if (!create) {
        return nullptr;
    }

    session = static_cast<TypioMozcSession *>(calloc(1, sizeof(*session)));
    if (!session) {
        return nullptr;
    }
    session->state = state;
    session->mode = mozc::commands::DIRECT;

    if (!mozc_create_session(state, &session->session_id)) {
        free(session);
        typio_log_error("Failed to create Mozc session");
        return nullptr;
    }

    typio_input_context_set_property(ctx, MOZC_SESSION_KEY, session,
                                     mozc_free_session);

    if (!mozc_activate_session(session, ctx)) {
        typio_log_warning("Mozc session stayed in ASCII/direct mode after activation");
    }

    return session;
}

/* -- config --------------------------------------------------------------- */

static void mozc_free_config(TypioMozcConfig *config) {
    free(config->server_path);
    memset(config, 0, sizeof(*config));
}

static TypioResult mozc_load_config(TypioEngine *engine,
                                    TypioMozcConfig *config) {
    memset(config, 0, sizeof(*config));
    config->server_path = typio_strdup(MOZC_SERVER_PATH);
    config->page_size = MOZC_DEFAULT_PAGE_SIZE;

    if (!config->server_path) {
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    if (!engine || !engine->instance) {
        return TYPIO_OK;
    }

    TypioConfig *engine_config =
        typio_instance_get_engine_config(engine->instance, "mozc");
    if (!engine_config) {
        return TYPIO_OK;
    }

    const char *server =
        typio_config_get_string(engine_config, "server_path", nullptr);

    if (server && *server) {
        free(config->server_path);
        config->server_path = typio_strdup(server);
    }

    typio_config_free(engine_config);

    if (!config->server_path) {
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }
    return TYPIO_OK;
}

/* -- engine ops ----------------------------------------------------------- */

static TypioResult mozc_init(TypioEngine *engine,
                             [[maybe_unused]] TypioInstance *instance) {
    auto *state = new (std::nothrow) TypioMozcState();
    if (!state) {
        return TYPIO_ERROR_OUT_OF_MEMORY;
    }

    TypioResult result = mozc_load_config(engine, &state->config);
    if (result != TYPIO_OK) {
        delete state;
        return result;
    }

    state->socket_path = mozc_get_socket_path();
    if (state->socket_path.empty()) {
        typio_log_error("Cannot determine Mozc socket path");
        mozc_free_config(&state->config);
        delete state;
        return TYPIO_ERROR;
    }

    typio_log_info("Mozc socket: %s",
                   mozc_format_socket_path(state->socket_path).c_str());
    typio_engine_set_user_data(engine, state);
    return TYPIO_OK;
}

static void mozc_destroy(TypioEngine *engine) {
    auto *state = static_cast<TypioMozcState *>(typio_engine_get_user_data(engine));
    if (!state) {
        return;
    }

    mozc_free_config(&state->config);
    delete state;
    typio_engine_set_user_data(engine, nullptr);
}

static void mozc_focus_in(TypioEngine *engine, TypioInputContext *ctx) {
    TypioMozcSession *session = mozc_get_session(engine, ctx, true);
    if (session && mozc_mode_is_ascii(session->mode)) {
        mozc_activate_session(session, ctx);
    }
}

static void mozc_focus_out(TypioEngine *engine, TypioInputContext *ctx) {
    TypioMozcSession *session = mozc_get_session(engine, ctx, false);
    if (!session) {
        return;
    }

    /* submit any in-progress composition */
    mozc::commands::SessionCommand sc;
    sc.set_type(mozc::commands::SessionCommand::REVERT);
    mozc::commands::Output output;
    mozc_send_command(session->state, session->session_id, sc, &output);
    mozc_clear_state(ctx);
}

static void mozc_reset(TypioEngine *engine, TypioInputContext *ctx) {
    TypioMozcSession *session = mozc_get_session(engine, ctx, false);
    if (!session) {
        mozc_clear_state(ctx);
        return;
    }

    mozc::commands::SessionCommand sc;
    sc.set_type(mozc::commands::SessionCommand::RESET_CONTEXT);
    mozc::commands::Output output;
    mozc_send_command(session->state, session->session_id, sc, &output);
    mozc_clear_state(ctx);
}

static TypioKeyProcessResult mozc_process_key(TypioEngine *engine,
                                              TypioInputContext *ctx,
                                              const TypioKeyEvent *event) {
    if (!engine || !ctx || !event) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    /* Escape with active composition → clear */
    if (event->type == TYPIO_EVENT_KEY_PRESS &&
        typio_key_event_is_escape(event)) {
        const TypioPreedit *preedit = typio_input_context_get_preedit(ctx);
        const TypioCandidateList *candidates =
            typio_input_context_get_candidates(ctx);
        if ((preedit && preedit->segment_count > 0) ||
            (candidates && candidates->count > 0)) {
            mozc_reset(engine, ctx);
            return TYPIO_KEY_HANDLED;
        }
    }

    TypioMozcSession *session = mozc_get_session(engine, ctx, true);
    if (!session) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    if (event->type == TYPIO_EVENT_KEY_PRESS &&
        mozc_mode_is_ascii(session->mode) &&
        !typio_key_event_is_modifier_only(event)) {
        mozc_activate_session(session, ctx);
    }

    mozc::commands::KeyEvent key;
    if (!mozc_build_key_event(event, &key)) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    mozc::commands::Output output;
    if (!mozc_send_key(session->state, session->session_id, key, &output)) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    if (!output.consumed()) {
        return TYPIO_KEY_NOT_HANDLED;
    }

    bool has_commit = output.has_result() &&
                      output.result().type() == mozc::commands::Result::STRING &&
                      !output.result().value().empty();
    bool composing = mozc_sync_output(output, ctx, session);

    if (has_commit) {
        return TYPIO_KEY_COMMITTED;
    }
    if (composing) {
        return TYPIO_KEY_COMPOSING;
    }
    return TYPIO_KEY_HANDLED;
}

static TypioResult mozc_reload_config(TypioEngine *engine) {
    auto *state = static_cast<TypioMozcState *>(typio_engine_get_user_data(engine));
    if (!state) {
        return TYPIO_ERROR_NOT_INITIALIZED;
    }

    if (!engine->instance) {
        return TYPIO_OK;
    }

    TypioConfig *engine_config =
        typio_instance_get_engine_config(engine->instance, "mozc");
    if (!engine_config) {
        return TYPIO_OK;
    }

    const char *server = typio_config_get_string(engine_config, "server_path", nullptr);

    if (server && *server) {
        free(state->config.server_path);
        state->config.server_path = typio_strdup(server);
    }

    typio_config_free(engine_config);
    return TYPIO_OK;
}

static const TypioEngineMode mozc_mode_hiragana = {
    .mode_class = TYPIO_MODE_CLASS_NATIVE,
    .mode_id = "hiragana",
    .display_label = "\u3042",
    .icon_name = "typio-mozc",
};

static const TypioEngineMode mozc_mode_katakana = {
    .mode_class = TYPIO_MODE_CLASS_NATIVE,
    .mode_id = "katakana",
    .display_label = "\u30A2",
    .icon_name = "typio-mozc-katakana",
};

static const TypioEngineMode mozc_mode_half_katakana = {
    .mode_class = TYPIO_MODE_CLASS_NATIVE,
    .mode_id = "half_katakana",
    .display_label = "\uFF71",
    .icon_name = "typio-mozc-half-katakana",
};

static const TypioEngineMode mozc_mode_direct = {
    .mode_class = TYPIO_MODE_CLASS_LATIN,
    .mode_id = "direct",
    .display_label = "A",
    .icon_name = "typio-mozc-direct",
};

static const TypioEngineMode mozc_mode_half_ascii = {
    .mode_class = TYPIO_MODE_CLASS_LATIN,
    .mode_id = "half_ascii",
    .display_label = "A",
    .icon_name = "typio-mozc-half-ascii",
};

static const TypioEngineMode mozc_mode_full_ascii = {
    .mode_class = TYPIO_MODE_CLASS_LATIN,
    .mode_id = "full_ascii",
    .display_label = "\uFF21",
    .icon_name = "typio-mozc-full-ascii",
};

static const TypioEngineMode *mozc_mode_for_composition(
    mozc::commands::CompositionMode mode) {
    switch (mode) {
    case mozc::commands::DIRECT:
        return &mozc_mode_direct;
    case mozc::commands::HALF_ASCII:
        return &mozc_mode_half_ascii;
    case mozc::commands::FULL_ASCII:
        return &mozc_mode_full_ascii;
    case mozc::commands::FULL_KATAKANA:
        return &mozc_mode_katakana;
    case mozc::commands::HALF_KATAKANA:
        return &mozc_mode_half_katakana;
    default:
        return &mozc_mode_hiragana;
    }
}

static const TypioEngineMode *mozc_get_mode(TypioEngine *engine,
                                             TypioInputContext *ctx) {
    TypioMozcSession *session = mozc_get_session(engine, ctx, false);
    if (!session) {
        return &mozc_mode_hiragana;
    }

    return mozc_mode_for_composition(session->mode);
}

/* -- engine definition ---------------------------------------------------- */

static const TypioEngineInfo mozc_engine_info = {
    .name = "mozc",
    .display_name = "Mozc",
    .description = "Japanese input engine powered by Mozc.",
    .version = TYPIO_VERSION,
    .author = "Typio",
    .icon = "typio-mozc",
    .language = "ja_JP",
    .type = TYPIO_ENGINE_TYPE_KEYBOARD,
    .capabilities = TYPIO_CAP_PREEDIT | TYPIO_CAP_CANDIDATES |
                    TYPIO_CAP_PREDICTION | TYPIO_CAP_LEARNING,
    .api_version = TYPIO_API_VERSION,
};

/* clang-format off */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static const TypioEngineOps mozc_engine_ops = {
    .init = mozc_init,
    .destroy = mozc_destroy,
    .focus_in = mozc_focus_in,
    .focus_out = mozc_focus_out,
    .reset = mozc_reset,
    .process_key = mozc_process_key,
    .reload_config = mozc_reload_config,
    .get_mode = mozc_get_mode,
};
#pragma GCC diagnostic pop
/* clang-format on */

extern "C" {

const TypioEngineInfo *typio_engine_get_info(void) {
    return &mozc_engine_info;
}

TypioEngine *typio_engine_create(void) {
    return typio_engine_new(&mozc_engine_info, &mozc_engine_ops);
}

} /* extern "C" */

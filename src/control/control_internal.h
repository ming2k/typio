#ifndef TYPIO_CONTROL_INTERNAL_H
#define TYPIO_CONTROL_INTERNAL_H

#include <gio/gio.h>
#include <gtk/gtk.h>
#include "control_bind.h"

enum {
    WHISPER_MODEL_COUNT = 5,
    SHERPA_MODEL_COUNT = 4,
    VOICE_BACKEND_COUNT = 2,
};

struct TypioControl;

typedef struct ModelInfo {
    const char *name;
    const char *display_name;
    const char *size_label;
    gint64 expected_size;
    const char *url;
    const char *filename;
    const char *extract_dir;
} ModelInfo;

typedef struct ModelRow {
    struct TypioControl *control;
    const ModelInfo *info;
    GtkWidget *row_box;
    GtkLabel *status_label;
    GtkButton *action_button;
    GtkProgressBar *progress;
    GSubprocess *download_proc;
    guint progress_timer;
    char *installed_path;
    char *tmp_path;
    char *base_dir;
} ModelRow;

typedef struct TypioControl {
    GtkApplication *app;
    GtkWidget *window;
    GtkLabel *service_status_label;
    GtkLabel *availability_label;
    GtkLabel *config_status_label;
    GtkLabel *engine_label;
    GtkDropDown *engine_dropdown;
    GtkStringList *engine_model;
    GtkDropDown *popup_theme_dropdown;
    GtkStringList *popup_theme_model;
    GtkDropDown *candidate_layout_dropdown;
    GtkStringList *candidate_layout_model;
    GtkSpinButton *font_size_spin;
    GtkSwitch *notifications_enable_switch;
    GtkSwitch *notifications_startup_switch;
    GtkSwitch *notifications_runtime_switch;
    GtkSwitch *notifications_voice_switch;
    GtkSpinButton *notifications_cooldown_spin;
    GtkStack *engine_config_stack;
    GtkWidget *engine_config_title;
    GtkDropDown *rime_schema_dropdown;
    GtkStringList *rime_schema_model;
    GtkSpinButton *rime_page_size_spin;
    GtkSpinButton *mozc_page_size_spin;
    GtkDropDown *voice_backend_dropdown;
    GtkStringList *voice_backend_model;
    GtkDropDown *voice_model_dropdown;
    GtkStringList *voice_model_list;
    GtkWidget *whisper_models_frame;
    GtkWidget *sherpa_models_frame;
    GtkButton *shortcut_switch_engine_btn;
    GtkButton *shortcut_voice_ptt_btn;
    GtkButton *shortcut_recording_btn;
    GtkTextBuffer *config_buffer;
    GtkButton *apply_config_button;
    GtkButton *cancel_config_button;
    GDBusProxy *proxy;
    guint name_watch_id;
    gboolean updating_ui;
    gboolean config_seeded;
    gboolean config_dirty;
    gboolean submitting_config;
    char *committed_config_text;
    ControlBinding bindings[20];
    size_t binding_count;
    ModelRow whisper_rows[WHISPER_MODEL_COUNT];
    char *whisper_dir;
    ModelRow sherpa_rows[SHERPA_MODEL_COUNT];
    char *sherpa_dir;
} TypioControl;

const char *control_voice_backend_id(TypioControl *control, guint idx);
guint control_voice_backend_index(TypioControl *control, const char *id);
gboolean is_voice_backend_name(const char *name);
void voice_update_model_sections(TypioControl *control);
void control_refresh_voice_models(TypioControl *control);
GtkWidget *control_build_whisper_model_section(TypioControl *control);
GtkWidget *control_build_sherpa_model_section(TypioControl *control);
void control_models_cleanup(TypioControl *control);

void control_sync_form_from_buffer(TypioControl *control);
void control_sync_buffer_from_form(TypioControl *control);
void control_stage_form_change(TypioControl *control);
void control_refresh_voice_models_from_stage(TypioControl *control);

void on_apply_config_clicked(GtkButton *button, gpointer user_data);
void on_cancel_config_clicked(GtkButton *button, gpointer user_data);
void on_form_spin_changed(GtkSpinButton *spin, gpointer user_data);
void on_voice_backend_changed(GObject *object, GParamSpec *pspec, gpointer user_data);
void on_display_dropdown_changed(GObject *object, GParamSpec *pspec, gpointer user_data);
void on_display_spin_changed(GtkSpinButton *spin, gpointer user_data);
void on_display_switch_changed(GObject *object, GParamSpec *pspec, gpointer user_data);
void on_engine_selected(GObject *object, GParamSpec *pspec, gpointer user_data);
void control_clear_proxy(TypioControl *control);
void control_refresh_from_proxy(TypioControl *control);

GtkWidget *control_build_window(TypioControl *control, GtkApplication *app);
GtkWidget *control_wrap_page_scroller(GtkWidget *child);
GtkWidget *control_build_display_page(TypioControl *control);
GtkWidget *control_build_shortcuts_page(TypioControl *control);
GtkWidget *control_build_engines_page(TypioControl *control);

#endif

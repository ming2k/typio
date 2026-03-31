#include "control_widgets.h"

#include <glib.h>

#define TYPIO_CONTROL_CSS_RESOURCE_PATH "/com/hihusky/typio/control/typio-control.css"

static void control_sync_css_color_scheme(GtkCssProvider *provider) {
    GtkSettings *settings;
    GtkInterfaceColorScheme scheme = GTK_INTERFACE_COLOR_SCHEME_DEFAULT;

    if (!provider) {
        return;
    }

    settings = gtk_settings_get_default();
    if (!settings) {
        return;
    }

    g_object_get(settings, "gtk-interface-color-scheme", &scheme, nullptr);
    g_object_set(provider, "prefers-color-scheme", scheme, nullptr);
    g_debug("control_apply_css: prefers-color-scheme=%d", (int)scheme);
}

void control_apply_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    g_debug("control_apply_css: loading embedded css from %s",
            TYPIO_CONTROL_CSS_RESOURCE_PATH);
    gtk_css_provider_load_from_resource(provider, TYPIO_CONTROL_CSS_RESOURCE_PATH);
    control_sync_css_color_scheme(provider);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    g_object_unref(provider);
}

void control_name_widget(GtkWidget *widget, const char *name) {
    if (!widget || !name || !*name) {
        return;
    }

    gtk_widget_set_name(widget, name);
    g_object_set_data_full(G_OBJECT(widget), "typio-debug-name", g_strdup(name), g_free);
}

static char *control_slugify(const char *text) {
    GString *slug;
    gboolean last_dash = FALSE;

    if (!text || !*text) {
        return g_strdup("unnamed");
    }

    slug = g_string_new(NULL);
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (g_ascii_isalnum(*p)) {
            g_string_append_c(slug, (char)g_ascii_tolower(*p));
            last_dash = FALSE;
        } else if (!last_dash && slug->len > 0) {
            g_string_append_c(slug, '-');
            last_dash = TRUE;
        }
    }

    while (slug->len > 0 && slug->str[slug->len - 1] == '-') {
        g_string_truncate(slug, slug->len - 1);
    }

    if (slug->len == 0) {
        g_string_assign(slug, "unnamed");
    }

    return g_string_free(slug, FALSE);
}

char *control_build_debug_name(const char *prefix, const char *token) {
    char *slug = control_slugify(token);
    char *name;

    if (prefix && *prefix) {
        name = g_strdup_printf("%s-%s", prefix, slug);
    } else {
        name = g_strdup(slug);
    }

    g_free(slug);
    return name;
}

GtkWidget *control_create_section_header_named(const char *name,
                                               const char *title,
                                               const char *description) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *title_label = gtk_label_new(title);
    GtkWidget *description_label = gtk_label_new(description);
    char *title_name = NULL;
    char *description_name = NULL;

    gtk_widget_add_css_class(box, "section");
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(description_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(description_label), TRUE);
    gtk_widget_add_css_class(title_label, "section-title");
    gtk_widget_add_css_class(description_label, "section-description");
    control_name_widget(box, name);
    if (name && *name) {
        title_name = g_strdup_printf("%s-title", name);
        description_name = g_strdup_printf("%s-description", name);
        control_name_widget(title_label, title_name);
        control_name_widget(description_label, description_name);
    }

    gtk_box_append(GTK_BOX(box), title_label);
    gtk_box_append(GTK_BOX(box), description_label);
    g_free(title_name);
    g_free(description_name);
    return box;
}

GtkWidget *control_create_section_header(const char *title, const char *description) {
    return control_create_section_header_named(NULL, title, description);
}

GtkWidget *control_create_panel_box_named(const char *name, gint spacing) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing);
    gtk_widget_add_css_class(box, "panel");
    control_name_widget(box, name);
    return box;
}

GtkWidget *control_create_panel_box(gint spacing) {
    return control_create_panel_box_named(NULL, spacing);
}

GtkWidget *control_create_page_shell_named(const char *name) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_add_css_class(box, "page-shell");
    control_name_widget(box, name);
    return box;
}

GtkWidget *control_create_page_shell(void) {
    return control_create_page_shell_named(NULL);
}

GtkWidget *control_create_preferences_list_named(const char *name) {
    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(list, "preferences");
    control_name_widget(list, name);
    return list;
}

GtkWidget *control_create_preferences_list(void) {
    return control_create_preferences_list_named(NULL);
}

GtkWidget *control_create_empty_note_named(const char *name, const char *text) {
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_add_css_class(label, "empty-note");
    control_name_widget(label, name);
    return label;
}

GtkWidget *control_create_empty_note(const char *text) {
    return control_create_empty_note_named(NULL, text);
}

GtkWidget *control_create_preference_row_named(const char *name,
                                               const char *title,
                                               const char *description,
                                               GtkWidget *suffix) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *shell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *title_label = gtk_label_new(title);
    GtkWidget *description_label = gtk_label_new(description);
    char *shell_name = NULL;
    char *text_name = NULL;
    char *title_name = NULL;
    char *description_name = NULL;
    char *suffix_name = NULL;

    gtk_widget_add_css_class(shell, "preference-row");
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(description_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(description_label), TRUE);
    gtk_widget_add_css_class(title_label, "preference-title");
    gtk_widget_add_css_class(description_label, "preference-description");
    gtk_widget_set_hexpand(text_box, TRUE);
    control_name_widget(row, name);
    if (name && *name) {
        shell_name = g_strdup_printf("%s-shell", name);
        text_name = g_strdup_printf("%s-text", name);
        title_name = g_strdup_printf("%s-title", name);
        description_name = g_strdup_printf("%s-description", name);
        control_name_widget(shell, shell_name);
        control_name_widget(text_box, text_name);
        control_name_widget(title_label, title_name);
        control_name_widget(description_label, description_name);
    }

    gtk_box_append(GTK_BOX(text_box), title_label);
    gtk_box_append(GTK_BOX(text_box), description_label);
    gtk_box_append(GTK_BOX(shell), text_box);

    if (suffix) {
        gtk_widget_set_valign(suffix, GTK_ALIGN_CENTER);
        gtk_widget_set_halign(suffix, GTK_ALIGN_END);
        if (!GTK_IS_SWITCH(suffix)) {
            gtk_widget_set_size_request(suffix, 190, -1);
        }
        if (name && *name &&
            !g_object_get_data(G_OBJECT(suffix), "typio-debug-name")) {
            suffix_name = g_strdup_printf("%s-suffix", name);
            control_name_widget(suffix, suffix_name);
        }
        gtk_box_append(GTK_BOX(shell), suffix);
    }

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), shell);
    g_free(shell_name);
    g_free(text_name);
    g_free(title_name);
    g_free(description_name);
    g_free(suffix_name);
    return row;
}

GtkWidget *control_create_preference_row(const char *title,
                                         const char *description,
                                         GtkWidget *suffix) {
    return control_create_preference_row_named(NULL, title, description, suffix);
}

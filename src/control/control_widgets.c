#include "control_widgets.h"

static const char *control_css =
    "window.control-root,\n"
    ".control-root {\n"
    "  background: linear-gradient(180deg, alpha(@headerbar_bg_color, 0.96), alpha(@window_bg_color, 1.0) 180px);\n"
    "  color: @window_fg_color;\n"
    "}\n"
    ".control-shell {\n"
    "  background: transparent;\n"
    "}\n"
    ".control-headerbar {\n"
    "  background: alpha(@headerbar_bg_color, 0.92);\n"
    "  box-shadow: none;\n"
    "}\n"
    ".view-switcher button {\n"
    "  min-height: 34px;\n"
    "  padding: 0 16px;\n"
    "  border-radius: 999px;\n"
    "}\n"
    ".summary-strip {\n"
    "  margin: 18px 20px 12px 20px;\n"
    "  padding: 18px 20px;\n"
    "  border-radius: 18px;\n"
    "  background: alpha(@card_bg_color, 0.96);\n"
    "}\n"
    ".summary-title {\n"
    "  font-size: 1.15rem;\n"
    "  font-weight: 700;\n"
    "}\n"
    ".muted-label {\n"
    "  opacity: 0.72;\n"
    "}\n"
    ".metric-value {\n"
    "  font-size: 1.05rem;\n"
    "  font-weight: 700;\n"
    "}\n"
    ".status-pill {\n"
    "  padding: 4px 10px;\n"
    "  border-radius: 999px;\n"
    "  font-weight: 700;\n"
    "}\n"
    ".status-pill.status-online {\n"
    "  background: alpha(@success_color, 0.16);\n"
    "  color: shade(@success_color, 0.8);\n"
    "}\n"
    ".status-pill.status-offline {\n"
    "  background: alpha(@warning_color, 0.18);\n"
    "  color: shade(@warning_color, 0.75);\n"
    "}\n"
    ".page-shell {\n"
    "  padding: 0 20px 20px 20px;\n"
    "}\n"
    ".section {\n"
    "  margin-top: 6px;\n"
    "}\n"
    ".section-title {\n"
    "  font-size: 1rem;\n"
    "  font-weight: 700;\n"
    "}\n"
    ".section-description {\n"
    "  opacity: 0.72;\n"
    "}\n"
    ".panel {\n"
    "  padding: 14px;\n"
    "  border-radius: 18px;\n"
    "  background: alpha(@card_bg_color, 0.96);\n"
    "}\n"
    ".preferences {\n"
    "  background: transparent;\n"
    "}\n"
    ".preferences row {\n"
    "  background: transparent;\n"
    "  border-radius: 14px;\n"
    "  margin: 0 0 8px 0;\n"
    "}\n"
    ".preferences row:last-child {\n"
    "  margin-bottom: 0;\n"
    "}\n"
    ".preference-row {\n"
    "  padding: 14px 16px;\n"
    "  border-radius: 14px;\n"
    "  background: alpha(@view_bg_color, 0.72);\n"
    "}\n"
    ".preference-title {\n"
    "  font-weight: 650;\n"
    "}\n"
    ".preference-description {\n"
    "  opacity: 0.72;\n"
    "}\n"
    ".engine-config {\n"
    "  margin-top: 10px;\n"
    "}\n"
    ".empty-note {\n"
    "  opacity: 0.72;\n"
    "}\n"
    ".inline-status {\n"
    "  opacity: 0.78;\n"
    "  font-weight: 600;\n"
    "}\n"
    ".footer-bar {\n"
    "  padding: 12px 20px 20px 20px;\n"
    "}\n";

void control_apply_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();

    gtk_css_provider_load_from_string(provider, control_css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

GtkWidget *control_create_section_header(const char *title, const char *description) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *title_label = gtk_label_new(title);
    GtkWidget *description_label = gtk_label_new(description);

    gtk_widget_add_css_class(box, "section");
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(description_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(description_label), TRUE);
    gtk_widget_add_css_class(title_label, "section-title");
    gtk_widget_add_css_class(description_label, "section-description");

    gtk_box_append(GTK_BOX(box), title_label);
    gtk_box_append(GTK_BOX(box), description_label);
    return box;
}

GtkWidget *control_create_panel_box(gint spacing) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing);
    gtk_widget_add_css_class(box, "panel");
    return box;
}

GtkWidget *control_create_page_shell(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_add_css_class(box, "page-shell");
    return box;
}

GtkWidget *control_create_preferences_list(void) {
    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(list, "preferences");
    return list;
}

GtkWidget *control_create_empty_note(const char *text) {
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_add_css_class(label, "empty-note");
    return label;
}

GtkWidget *control_create_preference_row(const char *title,
                                         const char *description,
                                         GtkWidget *suffix) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *shell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *title_label = gtk_label_new(title);
    GtkWidget *description_label = gtk_label_new(description);

    gtk_widget_add_css_class(shell, "preference-row");
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(description_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(description_label), TRUE);
    gtk_widget_add_css_class(title_label, "preference-title");
    gtk_widget_add_css_class(description_label, "preference-description");
    gtk_widget_set_hexpand(text_box, TRUE);

    gtk_box_append(GTK_BOX(text_box), title_label);
    gtk_box_append(GTK_BOX(text_box), description_label);
    gtk_box_append(GTK_BOX(shell), text_box);

    if (suffix) {
        gtk_widget_set_valign(suffix, GTK_ALIGN_CENTER);
        gtk_widget_set_halign(suffix, GTK_ALIGN_END);
        if (!GTK_IS_SWITCH(suffix)) {
            gtk_widget_set_size_request(suffix, 190, -1);
        }
        gtk_box_append(GTK_BOX(shell), suffix);
    }

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), shell);
    return row;
}

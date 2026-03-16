#include "control_internal.h"
#include "control_widgets.h"

GtkWidget *control_build_window(TypioControl *control, GtkApplication *app) {
    GtkWidget *window = gtk_application_window_new(app);
    GtkWidget *shell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *headerbar = gtk_header_bar_new();
    GtkWidget *page_stack = gtk_stack_new();
    GtkWidget *switcher = gtk_stack_switcher_new();
    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    gtk_window_set_title(GTK_WINDOW(window), "Typio Control");
    gtk_window_set_default_size(GTK_WINDOW(window), 920, 680);
    gtk_widget_set_size_request(window, 720, 520);
    gtk_widget_add_css_class(window, "control-root");
    control_name_widget(window, "control-window");
    control_apply_css();

    gtk_widget_add_css_class(shell, "control-shell");
    control_name_widget(shell, "control-window-shell");
    gtk_widget_set_hexpand(shell, TRUE);
    gtk_widget_set_vexpand(shell, TRUE);
    gtk_window_set_child(GTK_WINDOW(window), shell);

    gtk_widget_add_css_class(headerbar, "control-headerbar");
    control_name_widget(headerbar, "control-headerbar");
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(headerbar), TRUE);
    gtk_widget_add_css_class(switcher, "view-switcher");
    control_name_widget(switcher, "control-page-switcher");
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(page_stack));
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(headerbar), switcher);
    gtk_window_set_titlebar(GTK_WINDOW(window), headerbar);

    gtk_stack_set_transition_type(GTK_STACK(page_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_hexpand(page_stack, TRUE);
    gtk_widget_set_vexpand(page_stack, TRUE);
    gtk_widget_add_css_class(page_stack, "control-shell");
    control_name_widget(page_stack, "control-page-stack");
    gtk_stack_add_titled(GTK_STACK(page_stack),
                         control_wrap_page_scroller(control_build_display_page(control)),
                         "display", "Appearance");
    gtk_stack_add_titled(GTK_STACK(page_stack),
                         control_wrap_page_scroller(control_build_engines_page(control)),
                         "engines", "Engines");
    gtk_stack_add_titled(GTK_STACK(page_stack),
                         control_wrap_page_scroller(control_build_shortcuts_page(control)),
                         "shortcuts", "Shortcuts");
    gtk_box_append(GTK_BOX(shell), page_stack);

    gtk_widget_add_css_class(footer, "footer-bar");
    control_name_widget(footer, "control-footer");
    control->config_status_label = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_xalign(control->config_status_label, 0.0f);
    gtk_label_set_wrap(control->config_status_label, TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(control->config_status_label), "inline-status");
    control_name_widget(GTK_WIDGET(control->config_status_label), "control-config-status-label");
    gtk_widget_set_visible(GTK_WIDGET(control->config_status_label), FALSE);
    gtk_box_append(GTK_BOX(footer), GTK_WIDGET(control->config_status_label));
    gtk_box_append(GTK_BOX(shell), footer);

    return window;
}

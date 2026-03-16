#include "control_internal.h"
#include "control_widgets.h"

GtkWidget *control_build_window(TypioControl *control, GtkApplication *app) {
    GtkWidget *window = gtk_application_window_new(app);
    GtkWidget *shell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *headerbar = gtk_header_bar_new();
    GtkWidget *page_stack = gtk_stack_new();
    GtkWidget *switcher = gtk_stack_switcher_new();
    GtkWidget *summary = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    GtkWidget *summary_lead = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *summary_meta = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    GtkWidget *service_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *engine_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *title = gtk_label_new("Typio Control");
    GtkWidget *subtitle = gtk_label_new("Session status, engine selection, and input preferences.");
    GtkWidget *service_caption = gtk_label_new("Service");
    GtkWidget *engine_caption = gtk_label_new("Active engine");
    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    gtk_window_set_title(GTK_WINDOW(window), "Typio Control");
    gtk_window_set_default_size(GTK_WINDOW(window), 920, 680);
    gtk_widget_set_size_request(window, 720, 520);
    gtk_widget_add_css_class(window, "control-root");
    control_apply_css();

    gtk_widget_add_css_class(shell, "control-shell");
    gtk_window_set_child(GTK_WINDOW(window), shell);

    gtk_widget_add_css_class(headerbar, "control-headerbar");
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(headerbar), TRUE);
    gtk_widget_add_css_class(switcher, "view-switcher");
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(page_stack));
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(headerbar), switcher);
    gtk_window_set_titlebar(GTK_WINDOW(window), headerbar);

    gtk_widget_add_css_class(summary, "summary-strip");
    gtk_widget_set_hexpand(summary_lead, TRUE);

    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0f);
    gtk_widget_add_css_class(title, "summary-title");
    gtk_widget_add_css_class(subtitle, "muted-label");
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_box_append(GTK_BOX(summary_lead), title);
    gtk_box_append(GTK_BOX(summary_lead), subtitle);

    control->service_status_label = GTK_LABEL(gtk_label_new("Connected"));
    gtk_widget_add_css_class(GTK_WIDGET(control->service_status_label), "status-pill");
    gtk_widget_add_css_class(GTK_WIDGET(control->service_status_label), "status-online");
    control->availability_label = GTK_LABEL(gtk_label_new("Typio service is available on the session bus."));
    gtk_label_set_xalign(control->availability_label, 0.0f);
    gtk_label_set_wrap(control->availability_label, TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(control->availability_label), "muted-label");
    gtk_label_set_xalign(GTK_LABEL(service_caption), 0.0f);
    gtk_widget_add_css_class(service_caption, "muted-label");
    gtk_box_append(GTK_BOX(service_box), service_caption);
    gtk_box_append(GTK_BOX(service_box), GTK_WIDGET(control->service_status_label));
    gtk_box_append(GTK_BOX(service_box), GTK_WIDGET(control->availability_label));

    control->engine_label = GTK_LABEL(gtk_label_new("None"));
    gtk_label_set_xalign(control->engine_label, 0.0f);
    gtk_label_set_xalign(GTK_LABEL(engine_caption), 0.0f);
    gtk_widget_add_css_class(engine_caption, "muted-label");
    gtk_widget_add_css_class(GTK_WIDGET(control->engine_label), "metric-value");
    gtk_box_append(GTK_BOX(engine_box), engine_caption);
    gtk_box_append(GTK_BOX(engine_box), GTK_WIDGET(control->engine_label));

    gtk_box_append(GTK_BOX(summary_meta), service_box);
    gtk_box_append(GTK_BOX(summary_meta), engine_box);
    gtk_box_append(GTK_BOX(summary), summary_lead);
    gtk_box_append(GTK_BOX(summary), summary_meta);
    gtk_box_append(GTK_BOX(shell), summary);

    gtk_stack_set_transition_type(GTK_STACK(page_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_hexpand(page_stack, TRUE);
    gtk_widget_set_vexpand(page_stack, TRUE);
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
    control->config_status_label = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_xalign(control->config_status_label, 0.0f);
    gtk_label_set_wrap(control->config_status_label, TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(control->config_status_label), "inline-status");
    gtk_widget_set_visible(GTK_WIDGET(control->config_status_label), FALSE);
    gtk_box_append(GTK_BOX(footer), GTK_WIDGET(control->config_status_label));

    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(footer), spacer);

    control->cancel_config_button = GTK_BUTTON(gtk_button_new_with_label("Cancel"));
    g_signal_connect(control->cancel_config_button, "clicked",
                     G_CALLBACK(on_cancel_config_clicked), control);
    gtk_box_append(GTK_BOX(footer), GTK_WIDGET(control->cancel_config_button));

    control->apply_config_button = GTK_BUTTON(gtk_button_new_with_label("Apply"));
    g_signal_connect(control->apply_config_button, "clicked",
                     G_CALLBACK(on_apply_config_clicked), control);
    gtk_widget_add_css_class(GTK_WIDGET(control->apply_config_button), "suggested-action");
    gtk_box_append(GTK_BOX(footer), GTK_WIDGET(control->apply_config_button));
    gtk_box_append(GTK_BOX(shell), footer);

    return window;
}

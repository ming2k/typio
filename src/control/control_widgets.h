#ifndef TYPIO_CONTROL_WIDGETS_H
#define TYPIO_CONTROL_WIDGETS_H

#include <gtk/gtk.h>

void control_apply_css(void);

GtkWidget *control_create_section_header(const char *title, const char *description);
GtkWidget *control_create_panel_box(gint spacing);
GtkWidget *control_create_page_shell(void);
GtkWidget *control_create_preferences_list(void);
GtkWidget *control_create_empty_note(const char *text);
GtkWidget *control_create_preference_row(const char *title,
                                         const char *description,
                                         GtkWidget *suffix);

#endif

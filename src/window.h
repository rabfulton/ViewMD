#ifndef MARKYD_WINDOW_H
#define MARKYD_WINDOW_H

#include <gtk/gtk.h>

typedef struct _MarkydApp MarkydApp;
typedef struct _MarkydEditor MarkydEditor;

typedef struct _MarkydWindow {
  GtkWidget *window;
  GtkWidget *header_bar;
  GtkWidget *btn_open;
  GtkWidget *btn_refresh;
  GtkWidget *btn_settings;
  GtkWidget *search_revealer;
  GtkWidget *search_entry;
  GtkWidget *btn_search_prev;
  GtkWidget *btn_search_next;
  GtkWidget *lbl_search_status;
  GtkWidget *lbl_title;
  GtkWidget *scroll;
  MarkydEditor *editor;
  MarkydApp *app;
  GArray *search_matches;
  gint search_current_index;
} MarkydWindow;

/* Lifecycle */
MarkydWindow *markyd_window_new(MarkydApp *app);
void markyd_window_free(MarkydWindow *win);

/* Visibility */
void markyd_window_show(MarkydWindow *win);
void markyd_window_hide(MarkydWindow *win);
void markyd_window_toggle(MarkydWindow *win);
gboolean markyd_window_is_visible(MarkydWindow *win);

/* Styling */
void markyd_window_apply_css(MarkydWindow *win);

#endif /* MARKYD_WINDOW_H */

#include "editor.h"
#include "markdown.h"
#include <string.h>

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event,
                                  gpointer user_data);
static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event,
                                 gpointer user_data);
static gboolean on_leave_notify(GtkWidget *widget, GdkEventCrossing *event,
                                gpointer user_data);
static void apply_markdown(MarkydEditor *self);
static void schedule_markdown_apply(MarkydEditor *self);
static void render_table_widgets(MarkydEditor *self);

static const gchar *TABLE_WIDGET_DATA_KEY = "viewmd-table-widget";

static gchar *get_url_from_iter_tags(GtkTextIter *iter) {
  GSList *tags;
  gchar *url = NULL;

  if (!iter) {
    return NULL;
  }

  tags = gtk_text_iter_get_tags(iter);
  for (GSList *node = tags; node != NULL; node = node->next) {
    GtkTextTag *tag = GTK_TEXT_TAG(node->data);
    const gchar *stored =
        (const gchar *)g_object_get_data(G_OBJECT(tag), VIEWMD_LINK_URL_DATA);
    if (stored && stored[0] != '\0') {
      url = g_strdup(stored);
      break;
    }
  }
  g_slist_free(tags);
  return url;
}

static gboolean get_link_url_at_iter(GtkTextBuffer *buffer, GtkTextIter *at,
                                     gchar **out_url) {
  GtkTextIter hit;

  if (!buffer || !at || !out_url) {
    return FALSE;
  }
  *out_url = NULL;

  hit = *at;
  *out_url = get_url_from_iter_tags(&hit);
  if (*out_url) {
    return TRUE;
  }

  hit = *at;
  if (gtk_text_iter_backward_char(&hit)) {
    *out_url = get_url_from_iter_tags(&hit);
    if (*out_url) {
      return TRUE;
    }
  }

  hit = *at;
  if (gtk_text_iter_forward_char(&hit)) {
    *out_url = get_url_from_iter_tags(&hit);
    if (*out_url) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean scroll_to_markdown_anchor(MarkydEditor *self,
                                          const gchar *fragment) {
  gchar *mark_name;
  GtkTextMark *mark;

  if (!self || !self->buffer || !fragment) {
    return FALSE;
  }

  if (fragment[0] == '\0') {
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(self->buffer, &start);
    gtk_text_buffer_place_cursor(self->buffer, &start);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(self->text_view), &start, 0.2,
                                 FALSE, 0.0, 0.0);
    return TRUE;
  }

  mark_name = markdown_anchor_mark_name(fragment);
  mark = gtk_text_buffer_get_mark(self->buffer, mark_name);
  g_free(mark_name);
  if (!mark) {
    return FALSE;
  }

  {
    GtkTextIter at;
    gtk_text_buffer_get_iter_at_mark(self->buffer, &at, mark);
    gtk_text_buffer_place_cursor(self->buffer, &at);
  }
  gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(self->text_view), mark, 0.2, FALSE,
                               0.0, 0.0);
  return TRUE;
}

static void set_link_cursor(MarkydEditor *self, gboolean active) {
  GdkWindow *win = gtk_text_view_get_window(GTK_TEXT_VIEW(self->text_view),
                                            GTK_TEXT_WINDOW_TEXT);
  if (!win) {
    return;
  }

  if (active) {
    GdkDisplay *display = gdk_window_get_display(win);
    GdkCursor *cursor = gdk_cursor_new_from_name(display, "pointer");
    if (!cursor) {
      cursor = gdk_cursor_new_for_display(display, GDK_HAND2);
    }
    gdk_window_set_cursor(win, cursor);
    if (cursor) {
      g_object_unref(cursor);
    }
  } else {
    gdk_window_set_cursor(win, NULL);
  }
}

static void apply_markdown(MarkydEditor *self) {
  if (!self) {
    return;
  }

  self->updating_tags = TRUE;
  markdown_apply_tags(self->buffer,
                      self->source_content ? self->source_content : "");
  render_table_widgets(self);
  self->updating_tags = FALSE;
}

static void render_table_widgets(MarkydEditor *self) {
  GtkTextIter iter;
  GtkTextIter end;

  if (!self || !self->buffer || !self->text_view) {
    return;
  }

  gtk_text_buffer_get_bounds(self->buffer, &iter, &end);
  while (!gtk_text_iter_equal(&iter, &end)) {
    GtkTextChildAnchor *anchor = gtk_text_iter_get_child_anchor(&iter);
    if (anchor &&
        g_object_get_data(G_OBJECT(anchor), VIEWMD_TABLE_ANCHOR_DATA) != NULL) {
      GtkWidget *table = g_object_get_data(G_OBJECT(anchor), TABLE_WIDGET_DATA_KEY);
      if (!table) {
        table = markdown_create_table_widget(anchor);
        if (table) {
          gtk_text_view_add_child_at_anchor(GTK_TEXT_VIEW(self->text_view), table,
                                            anchor);
          gtk_widget_show_all(table);
          g_object_set_data(G_OBJECT(anchor), TABLE_WIDGET_DATA_KEY, table);
        }
      }
    }
    gtk_text_iter_forward_char(&iter);
  }
}

static gboolean apply_markdown_idle(gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;
  self->markdown_idle_id = 0;
  apply_markdown(self);
  return G_SOURCE_REMOVE;
}

static void schedule_markdown_apply(MarkydEditor *self) {
  if (!self || self->updating_tags || self->markdown_idle_id != 0) {
    return;
  }

  self->markdown_idle_id =
      g_idle_add_full(G_PRIORITY_LOW, apply_markdown_idle, self, NULL);
}

void markyd_editor_refresh(MarkydEditor *self) { schedule_markdown_apply(self); }

MarkydEditor *markyd_editor_new(MarkydApp *app) {
  MarkydEditor *self = g_new0(MarkydEditor, 1);

  self->app = app;
  self->source_content = g_strdup("");
  self->updating_tags = FALSE;
  self->markdown_idle_id = 0;

  self->text_view = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(self->text_view),
                              GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(self->text_view), 16);
  gtk_text_view_set_right_margin(GTK_TEXT_VIEW(self->text_view), 16);
  gtk_text_view_set_top_margin(GTK_TEXT_VIEW(self->text_view), 16);
  gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(self->text_view), 16);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(self->text_view), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(self->text_view), FALSE);

  self->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->text_view));
  markdown_init_tags(self->buffer);

  gtk_widget_add_events(self->text_view, GDK_POINTER_MOTION_MASK |
                                            GDK_LEAVE_NOTIFY_MASK |
                                            GDK_BUTTON_RELEASE_MASK);
  g_signal_connect(self->text_view, "button-release-event",
                   G_CALLBACK(on_button_release), self);
  g_signal_connect(self->text_view, "motion-notify-event",
                   G_CALLBACK(on_motion_notify), self);
  g_signal_connect(self->text_view, "leave-notify-event",
                   G_CALLBACK(on_leave_notify), self);

  return self;
}

void markyd_editor_free(MarkydEditor *self) {
  if (!self) {
    return;
  }
  if (self->markdown_idle_id != 0) {
    g_source_remove(self->markdown_idle_id);
    self->markdown_idle_id = 0;
  }
  g_free(self->source_content);
  g_free(self);
}

void markyd_editor_set_content(MarkydEditor *self, const gchar *content) {
  if (!self) {
    return;
  }

  g_free(self->source_content);
  self->source_content = g_strdup(content ? content : "");
  schedule_markdown_apply(self);
}

gchar *markyd_editor_get_content(MarkydEditor *self) {
  if (!self) {
    return g_strdup("");
  }
  return g_strdup(self->source_content ? self->source_content : "");
}

GtkWidget *markyd_editor_get_widget(MarkydEditor *self) {
  return self->text_view;
}

void markyd_editor_focus(MarkydEditor *self) {
  GtkTextMark *insert_mark;

  if (!self || !self->text_view || !self->buffer) {
    return;
  }

  gtk_widget_grab_focus(self->text_view);
  insert_mark = gtk_text_buffer_get_insert(self->buffer);
  if (insert_mark) {
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(self->text_view), insert_mark,
                                 0.0, FALSE, 0.0, 0.0);
  }
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event,
                                  gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;
  GtkTextIter iter;
  gint bx, by;
  gchar *url = NULL;
  GError *error = NULL;

  if (event->button != 1) {
    return FALSE;
  }

  gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(widget),
                                        GTK_TEXT_WINDOW_TEXT, (gint)event->x,
                                        (gint)event->y, &bx, &by);
  gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(widget), &iter, bx, by);

  if (!get_link_url_at_iter(self->buffer, &iter, &url)) {
    return FALSE;
  }

  if (url[0] == '#') {
    gchar *fragment = g_strdup(url + 1);
    gchar *space = strpbrk(fragment, " \t");
    if (space) {
      *space = '\0';
    }
    if (!scroll_to_markdown_anchor(self, fragment)) {
      g_printerr("Anchor not found: '%s'\n", url);
    }
    g_free(fragment);
    g_free(url);
    return TRUE;
  }

  if (g_uri_parse_scheme(url) == NULL) {
    gchar *with_scheme = g_strdup_printf("https://%s", url);
    g_free(url);
    url = with_scheme;
  }

  GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
  if (!gtk_show_uri_on_window(GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel)
                                                      : NULL,
                              url, GDK_CURRENT_TIME, &error)) {
    if (error) {
      g_printerr("Failed to open link '%s': %s\n", url, error->message);
      g_clear_error(&error);
    }
  }

  g_free(url);
  return TRUE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event,
                                 gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;
  GtkTextIter iter;
  gint bx, by;
  gchar *url = NULL;

  gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(widget),
                                        GTK_TEXT_WINDOW_TEXT, (gint)event->x,
                                        (gint)event->y, &bx, &by);
  gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(widget), &iter, bx, by);

  set_link_cursor(self, get_link_url_at_iter(self->buffer, &iter, &url));
  g_free(url);
  return FALSE;
}

static gboolean on_leave_notify(GtkWidget *widget, GdkEventCrossing *event,
                                gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;
  (void)widget;
  (void)event;
  set_link_cursor(self, FALSE);
  return FALSE;
}

#include "editor.h"
#include "app.h"
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
static void render_image_widgets(MarkydEditor *self);
static void render_table_widgets(MarkydEditor *self);
static void refresh_image_widget_scales(MarkydEditor *self);
static gboolean resolve_image_source_path(MarkydEditor *self, const gchar *src,
                                          gchar **out_path);
static void on_text_view_size_allocate(GtkWidget *widget, GtkAllocation *allocation,
                                       gpointer user_data);

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
  render_image_widgets(self);
  render_table_widgets(self);
  refresh_image_widget_scales(self);
  self->updating_tags = FALSE;
}

static gboolean resolve_image_source_path(MarkydEditor *self, const gchar *src,
                                          gchar **out_path) {
  gchar *path = NULL;

  if (out_path) {
    *out_path = NULL;
  }
  if (!self || !src || src[0] == '\0' || !out_path) {
    return FALSE;
  }

  if (g_uri_parse_scheme(src) != NULL) {
    if (g_str_has_prefix(src, "file://")) {
      path = g_filename_from_uri(src, NULL, NULL);
    } else {
      return FALSE;
    }
  } else if (g_path_is_absolute(src)) {
    path = g_strdup(src);
  } else {
    const gchar *current_path = markyd_app_get_current_path(self->app);
    if (current_path && current_path[0] != '\0') {
      gchar *dir = g_path_get_dirname(current_path);
      path = g_build_filename(dir, src, NULL);
      g_free(dir);
    } else {
      path = g_strdup(src);
    }
  }

  if (!path) {
    return FALSE;
  }
  if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
    g_free(path);
    return FALSE;
  }

  *out_path = path;
  return TRUE;
}

static gint get_image_max_width(MarkydEditor *self) {
  GtkAllocation alloc;
  gint width;

  if (!self || !self->text_view) {
    return 0;
  }

  gtk_widget_get_allocation(self->text_view, &alloc);
  width = alloc.width - gtk_text_view_get_left_margin(GTK_TEXT_VIEW(self->text_view)) -
          gtk_text_view_get_right_margin(GTK_TEXT_VIEW(self->text_view)) - 24;
  return MAX(width, 64);
}

static void scale_image_widget(GtkWidget *widget, gint max_width) {
  GtkWidget *image;
  GdkPixbuf *orig;
  gint ow;
  gint oh;

  if (!widget || max_width <= 0) {
    return;
  }

  image = g_object_get_data(G_OBJECT(widget), "viewmd-image-widget-child");
  orig = g_object_get_data(G_OBJECT(widget), "viewmd-image-orig-pixbuf");
  if (!GTK_IS_IMAGE(image) || !GDK_IS_PIXBUF(orig)) {
    return;
  }

  ow = gdk_pixbuf_get_width(orig);
  oh = gdk_pixbuf_get_height(orig);
  if (ow <= 0 || oh <= 0) {
    return;
  }

  if (ow > max_width) {
    gint nh = (gint)(((gdouble)oh * (gdouble)max_width) / (gdouble)ow);
    GdkPixbuf *scaled =
        gdk_pixbuf_scale_simple(orig, max_width, MAX(nh, 1), GDK_INTERP_BILINEAR);
    gtk_image_set_from_pixbuf(GTK_IMAGE(image), scaled);
    if (scaled) {
      g_object_unref(scaled);
    }
  } else {
    gtk_image_set_from_pixbuf(GTK_IMAGE(image), orig);
  }
}

static void refresh_image_widget_scales(MarkydEditor *self) {
  GtkTextIter iter;
  GtkTextIter end;
  gint max_width;

  if (!self || !self->buffer) {
    return;
  }

  max_width = get_image_max_width(self);
  gtk_text_buffer_get_bounds(self->buffer, &iter, &end);
  while (!gtk_text_iter_equal(&iter, &end)) {
    GtkTextChildAnchor *anchor = gtk_text_iter_get_child_anchor(&iter);
    if (anchor &&
        g_object_get_data(G_OBJECT(anchor), VIEWMD_IMAGE_ANCHOR_DATA) != NULL) {
      GtkWidget *image_widget =
          g_object_get_data(G_OBJECT(anchor), VIEWMD_IMAGE_WIDGET_DATA);
      if (image_widget) {
        scale_image_widget(image_widget, max_width);
      }
    }
    gtk_text_iter_forward_char(&iter);
  }
}

static void render_image_widgets(MarkydEditor *self) {
  GtkTextIter iter;
  GtkTextIter end;
  gint max_width;

  if (!self || !self->buffer || !self->text_view) {
    return;
  }

  max_width = get_image_max_width(self);
  gtk_text_buffer_get_bounds(self->buffer, &iter, &end);
  while (!gtk_text_iter_equal(&iter, &end)) {
    GtkTextChildAnchor *anchor = gtk_text_iter_get_child_anchor(&iter);
    if (anchor &&
        g_object_get_data(G_OBJECT(anchor), VIEWMD_IMAGE_ANCHOR_DATA) != NULL) {
      GtkWidget *image_widget =
          g_object_get_data(G_OBJECT(anchor), VIEWMD_IMAGE_WIDGET_DATA);
      if (!image_widget) {
        const gchar *src =
            g_object_get_data(G_OBJECT(anchor), VIEWMD_IMAGE_SRC_DATA);
        const gchar *alt =
            g_object_get_data(G_OBJECT(anchor), VIEWMD_IMAGE_ALT_DATA);
        gchar *path = NULL;

        if (resolve_image_source_path(self, src, &path)) {
          GError *err = NULL;
          GdkPixbuf *orig = gdk_pixbuf_new_from_file(path, &err);
          if (orig) {
            GtkWidget *event_box = gtk_event_box_new();
            GtkWidget *image = gtk_image_new();
            gtk_event_box_set_visible_window(GTK_EVENT_BOX(event_box), FALSE);
            gtk_widget_set_halign(event_box, GTK_ALIGN_START);
            gtk_container_add(GTK_CONTAINER(event_box), image);
            g_object_set_data(G_OBJECT(event_box), "viewmd-image-widget-child",
                              image);
            g_object_set_data_full(G_OBJECT(event_box), "viewmd-image-orig-pixbuf",
                                   orig, g_object_unref);
            scale_image_widget(event_box, max_width);
            image_widget = event_box;
          } else if (err) {
            g_error_free(err);
          }
        }

        if (!image_widget) {
          GtkWidget *fallback = gtk_label_new(alt && alt[0] != '\0' ? alt : src);
          gtk_widget_set_halign(fallback, GTK_ALIGN_START);
          gtk_style_context_add_class(gtk_widget_get_style_context(fallback),
                                      "dim-label");
          image_widget = fallback;
        }

        gtk_text_view_add_child_at_anchor(GTK_TEXT_VIEW(self->text_view),
                                          image_widget, anchor);
        gtk_widget_show_all(image_widget);
        g_object_set_data(G_OBJECT(anchor), VIEWMD_IMAGE_WIDGET_DATA, image_widget);
        g_free(path);
      } else {
        scale_image_widget(image_widget, max_width);
      }
    }
    gtk_text_iter_forward_char(&iter);
  }
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
      GtkWidget *table =
          g_object_get_data(G_OBJECT(anchor), VIEWMD_TABLE_WIDGET_DATA);
      if (!table) {
        table = markdown_create_table_widget(anchor);
        if (table) {
          gtk_text_view_add_child_at_anchor(GTK_TEXT_VIEW(self->text_view), table,
                                            anchor);
          gtk_widget_show_all(table);
          g_object_set_data(G_OBJECT(anchor), VIEWMD_TABLE_WIDGET_DATA, table);
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
  g_signal_connect(self->text_view, "size-allocate",
                   G_CALLBACK(on_text_view_size_allocate), self);

  return self;
}

static void on_text_view_size_allocate(GtkWidget *widget, GtkAllocation *allocation,
                                       gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;
  (void)widget;
  (void)allocation;
  refresh_image_widget_scales(self);
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

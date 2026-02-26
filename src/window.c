#include "window.h"
#include "app.h"
#include "config.h"
#include "editor.h"
#include "markdown.h"

typedef struct {
  gint start_offset;
  gint end_offset;
  GtkTextChildAnchor *table_anchor;
  gint table_row;
  gint table_col;
} SearchMatch;

#define TAG_SEARCH_MATCH "viewmd_search_match"
#define TAG_SEARCH_CURRENT "viewmd_search_current"

static void on_open_clicked(GtkButton *button, gpointer user_data);
static void on_refresh_clicked(GtkButton *button, gpointer user_data);
static void on_settings_clicked(GtkButton *button, gpointer user_data);
static void on_search_changed(GtkEditable *editable, gpointer user_data);
static void on_search_prev_clicked(GtkButton *button, gpointer user_data);
static void on_search_next_clicked(GtkButton *button, gpointer user_data);
static gboolean on_search_entry_key_press(GtkWidget *widget, GdkEventKey *event,
                                          gpointer user_data);
static void on_editor_buffer_changed(GtkTextBuffer *buffer, gpointer user_data);
static void ensure_search_tags(MarkydWindow *self);
static void update_search_tag_styles(MarkydWindow *self, const gchar *match_bg,
                                     const gchar *match_fg,
                                     const gchar *current_bg,
                                     const gchar *current_fg);
static void clear_search_matches(MarkydWindow *self);
static void update_search_matches(MarkydWindow *self);
static void jump_to_search_match(MarkydWindow *self, gint index,
                                 gboolean scroll_to_match);
static void clear_table_search_highlight(MarkydWindow *self, gboolean clear_match,
                                         gboolean clear_current);
static void apply_table_search_match_highlight(MarkydWindow *self);
static gboolean resolve_table_match_location(MarkydWindow *self, gint start_offset,
                                             gint end_offset,
                                             GtkTextChildAnchor **out_anchor,
                                             gint *out_row, gint *out_col);
static GtkWidget *lookup_table_cell_widget(GtkWidget *table_widget, gint row,
                                           gint col);
static void set_table_cell_highlight(GtkWidget *cell, gboolean match,
                                     gboolean current);
static gboolean scroll_to_table_cell(MarkydWindow *self, GtkWidget *cell);
static void show_search_ui(MarkydWindow *self);
static void hide_search_ui(MarkydWindow *self);
static gboolean on_key_press_event(GtkWidget *widget, GdkEventKey *event,
                                   gpointer user_data);
static gboolean on_configure_event(GtkWidget *widget, GdkEventConfigure *event,
                                   gpointer user_data);
static gboolean on_window_state_event(GtkWidget *widget,
                                      GdkEventWindowState *event,
                                      gpointer user_data);

static void on_font_size_changed(GtkSpinButton *spin, gpointer user_data);
static void on_font_family_changed(GtkComboBoxText *combo, gpointer user_data);
static void on_theme_changed(GtkComboBoxText *combo, gpointer user_data);
static gchar *rgba_to_hex(const GdkRGBA *rgba);
static void init_color_button(GtkColorButton *btn, const gchar *color_str);
static void on_color_set(GtkColorButton *btn, gpointer user_data);
static GtkWidget *create_settings_dialog(MarkydApp *app);

static gboolean geometry_debug_enabled(void) {
  const gchar *v = g_getenv("VIEWMD_DEBUG_GEOMETRY");
  if (!v) {
    v = g_getenv("TRAYMD_DEBUG_GEOMETRY");
  }
  return v && v[0] != '\0' && g_strcmp0(v, "0") != 0;
}

static void ensure_search_tags(MarkydWindow *self) {
  GtkTextTagTable *table;

  if (!self || !self->editor || !self->editor->buffer) {
    return;
  }

  table = gtk_text_buffer_get_tag_table(self->editor->buffer);
  if (!table) {
    return;
  }

  if (!gtk_text_tag_table_lookup(table, TAG_SEARCH_MATCH)) {
    gtk_text_buffer_create_tag(self->editor->buffer, TAG_SEARCH_MATCH, "weight",
                               PANGO_WEIGHT_BOLD, NULL);
  }
  if (!gtk_text_tag_table_lookup(table, TAG_SEARCH_CURRENT)) {
    gtk_text_buffer_create_tag(self->editor->buffer, TAG_SEARCH_CURRENT, "weight",
                               PANGO_WEIGHT_BOLD, NULL);
  }
}

static void update_search_tag_styles(MarkydWindow *self, const gchar *match_bg,
                                     const gchar *match_fg,
                                     const gchar *current_bg,
                                     const gchar *current_fg) {
  GtkTextTagTable *table;
  GtkTextTag *tag;

  if (!self || !self->editor || !self->editor->buffer) {
    return;
  }

  table = gtk_text_buffer_get_tag_table(self->editor->buffer);
  if (!table) {
    return;
  }

  tag = gtk_text_tag_table_lookup(table, TAG_SEARCH_MATCH);
  if (tag) {
    g_object_set(tag, "background", match_bg, "foreground", match_fg, NULL);
  }

  tag = gtk_text_tag_table_lookup(table, TAG_SEARCH_CURRENT);
  if (tag) {
    g_object_set(tag, "background", current_bg, "foreground", current_fg, NULL);
  }
}

static void clear_search_matches(MarkydWindow *self) {
  GtkTextIter start;
  GtkTextIter end;

  if (!self || !self->editor || !self->editor->buffer) {
    return;
  }

  gtk_text_buffer_get_bounds(self->editor->buffer, &start, &end);
  gtk_text_buffer_remove_tag_by_name(self->editor->buffer, TAG_SEARCH_MATCH,
                                     &start, &end);
  gtk_text_buffer_remove_tag_by_name(self->editor->buffer, TAG_SEARCH_CURRENT,
                                     &start, &end);
  clear_table_search_highlight(self, TRUE, TRUE);

  if (self->search_matches) {
    g_array_set_size(self->search_matches, 0);
  }
  self->search_current_index = -1;

  if (self->lbl_search_status) {
    gtk_label_set_text(GTK_LABEL(self->lbl_search_status), "");
  }
  if (self->btn_search_prev) {
    gtk_widget_set_sensitive(self->btn_search_prev, FALSE);
  }
  if (self->btn_search_next) {
    gtk_widget_set_sensitive(self->btn_search_next, FALSE);
  }
}

static void set_table_cell_highlight(GtkWidget *cell, gboolean match,
                                     gboolean current) {
  GtkStyleContext *style;

  if (!cell) {
    return;
  }

  style = gtk_widget_get_style_context(cell);
  if (match) {
    gtk_style_context_add_class(style, VIEWMD_TABLE_CELL_MATCH_CLASS);
  } else {
    gtk_style_context_remove_class(style, VIEWMD_TABLE_CELL_MATCH_CLASS);
  }

  if (current) {
    gtk_style_context_add_class(style, VIEWMD_TABLE_CELL_CURRENT_CLASS);
  } else {
    gtk_style_context_remove_class(style, VIEWMD_TABLE_CELL_CURRENT_CLASS);
  }
}

static gboolean scroll_to_table_cell(MarkydWindow *self, GtkWidget *cell) {
  gint x = 0;
  gint y = 0;
  gdouble doc_x;
  gdouble doc_y;
  GtkAllocation alloc;
  GtkAdjustment *hadj;
  GtkAdjustment *vadj;

  if (!self || !cell || !self->editor || !self->editor->text_view || !self->scroll) {
    return FALSE;
  }

  if (!gtk_widget_translate_coordinates(cell, self->editor->text_view, 0, 0, &x,
                                        &y)) {
    return FALSE;
  }

  gtk_widget_get_allocation(cell, &alloc);
  hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(self->scroll));
  vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(self->scroll));
  doc_x = x + (hadj ? gtk_adjustment_get_value(hadj) : 0.0);
  doc_y = y + (vadj ? gtk_adjustment_get_value(vadj) : 0.0);

  if (hadj) {
    gdouble target = MAX(0.0, doc_x - 16.0);
    gdouble max_val =
        gtk_adjustment_get_upper(hadj) - gtk_adjustment_get_page_size(hadj);
    gtk_adjustment_set_value(hadj, CLAMP(target, 0.0, MAX(0.0, max_val)));
  }
  if (vadj) {
    gdouble page = gtk_adjustment_get_page_size(vadj);
    gdouble target = MAX(0.0, doc_y - (page * 0.25));
    gdouble max_val =
        gtk_adjustment_get_upper(vadj) - gtk_adjustment_get_page_size(vadj);
    gtk_adjustment_set_value(vadj, CLAMP(target, 0.0, MAX(0.0, max_val)));
  }

  return TRUE;
}

static GtkWidget *lookup_table_cell_widget(GtkWidget *table_widget, gint row,
                                           gint col) {
  GList *wrapper_children;
  GtkWidget *found = NULL;

  if (!table_widget || !GTK_IS_CONTAINER(table_widget)) {
    return NULL;
  }

  wrapper_children = gtk_container_get_children(GTK_CONTAINER(table_widget));
  for (GList *w = wrapper_children; w != NULL && !found; w = w->next) {
    GtkWidget *child = GTK_WIDGET(w->data);
    if (!GTK_IS_GRID(child)) {
      continue;
    }

    GList *grid_children = gtk_container_get_children(GTK_CONTAINER(child));
    for (GList *g = grid_children; g != NULL; g = g->next) {
      GtkWidget *cell = GTK_WIDGET(g->data);
      gint cell_row = GPOINTER_TO_INT(
          g_object_get_data(G_OBJECT(cell), VIEWMD_TABLE_CELL_ROW_DATA));
      gint cell_col = GPOINTER_TO_INT(
          g_object_get_data(G_OBJECT(cell), VIEWMD_TABLE_CELL_COL_DATA));
      if (cell_row == row && cell_col == col) {
        found = cell;
        break;
      }
    }
    g_list_free(grid_children);
  }
  g_list_free(wrapper_children);

  return found;
}

static void clear_table_search_highlight(MarkydWindow *self, gboolean clear_match,
                                         gboolean clear_current) {
  GtkTextIter iter;
  GtkTextIter end;

  if (!self || !self->editor || !self->editor->buffer) {
    return;
  }

  gtk_text_buffer_get_bounds(self->editor->buffer, &iter, &end);
  while (!gtk_text_iter_equal(&iter, &end)) {
    GtkTextChildAnchor *anchor = gtk_text_iter_get_child_anchor(&iter);
    if (anchor &&
        g_object_get_data(G_OBJECT(anchor), VIEWMD_TABLE_ANCHOR_DATA) != NULL) {
      GtkWidget *table_widget =
          g_object_get_data(G_OBJECT(anchor), VIEWMD_TABLE_WIDGET_DATA);
      if (table_widget && GTK_IS_CONTAINER(table_widget)) {
        GList *wrapper_children =
            gtk_container_get_children(GTK_CONTAINER(table_widget));
        for (GList *w = wrapper_children; w != NULL; w = w->next) {
          GtkWidget *child = GTK_WIDGET(w->data);
          if (!GTK_IS_GRID(child)) {
            continue;
          }
          GList *grid_children = gtk_container_get_children(GTK_CONTAINER(child));
          for (GList *g = grid_children; g != NULL; g = g->next) {
            GtkWidget *cell = GTK_WIDGET(g->data);
            if (clear_match) {
              gtk_style_context_remove_class(gtk_widget_get_style_context(cell),
                                             VIEWMD_TABLE_CELL_MATCH_CLASS);
            }
            if (clear_current) {
              gtk_style_context_remove_class(gtk_widget_get_style_context(cell),
                                             VIEWMD_TABLE_CELL_CURRENT_CLASS);
            }
          }
          g_list_free(grid_children);
        }
        g_list_free(wrapper_children);
      }
    }
    gtk_text_iter_forward_char(&iter);
  }
}

static gboolean resolve_table_match_location(MarkydWindow *self, gint start_offset,
                                             gint end_offset,
                                             GtkTextChildAnchor **out_anchor,
                                             gint *out_row, gint *out_col) {
  GtkTextIter iter;
  GtkTextIter end;

  if (out_anchor) {
    *out_anchor = NULL;
  }
  if (out_row) {
    *out_row = -1;
  }
  if (out_col) {
    *out_col = -1;
  }

  if (!self || !self->editor || !self->editor->buffer ||
      end_offset <= start_offset) {
    return FALSE;
  }

  gtk_text_buffer_get_bounds(self->editor->buffer, &iter, &end);
  while (!gtk_text_iter_equal(&iter, &end)) {
    GtkTextChildAnchor *anchor = gtk_text_iter_get_child_anchor(&iter);
    if (anchor) {
      ViewmdTableSearchIndex *index = g_object_get_data(
          G_OBJECT(anchor), VIEWMD_TABLE_SEARCH_INDEX_DATA);
      if (index && start_offset < index->end_offset &&
          end_offset > index->start_offset) {
        if (out_anchor) {
          *out_anchor = anchor;
        }

        if (index->cells) {
          ViewmdTableSearchCellRange *overlap_cell = NULL;
          for (guint i = 0; i < index->cells->len; i++) {
            ViewmdTableSearchCellRange *cell =
                &g_array_index(index->cells, ViewmdTableSearchCellRange, i);
            if (start_offset >= cell->start_offset && start_offset < cell->end_offset) {
              if (out_row) {
                *out_row = cell->row;
              }
              if (out_col) {
                *out_col = cell->col;
              }
              return TRUE;
            }
            if (start_offset < cell->end_offset && end_offset > cell->start_offset) {
              overlap_cell = cell;
            }
          }
          if (overlap_cell) {
            if (out_row) {
              *out_row = overlap_cell->row;
            }
            if (out_col) {
              *out_col = overlap_cell->col;
            }
            return TRUE;
          }
        }

        return TRUE;
      }
    }
    gtk_text_iter_forward_char(&iter);
  }

  return FALSE;
}

static void apply_table_search_match_highlight(MarkydWindow *self) {
  if (!self || !self->search_matches) {
    return;
  }

  clear_table_search_highlight(self, TRUE, FALSE);

  for (guint i = 0; i < self->search_matches->len; i++) {
    SearchMatch *match = &g_array_index(self->search_matches, SearchMatch, i);
    GtkWidget *table_widget;
    GtkWidget *cell;

    if (!match->table_anchor || match->table_row < 0 || match->table_col < 0) {
      continue;
    }

    table_widget =
        g_object_get_data(G_OBJECT(match->table_anchor), VIEWMD_TABLE_WIDGET_DATA);
    cell = lookup_table_cell_widget(table_widget, match->table_row, match->table_col);
    if (cell) {
      set_table_cell_highlight(cell, TRUE, FALSE);
    }
  }
}

static void jump_to_search_match(MarkydWindow *self, gint index,
                                 gboolean scroll_to_match) {
  GtkTextIter start;
  GtkTextIter end;
  SearchMatch *match;
  gchar *status;

  if (!self || !self->editor || !self->editor->buffer || !self->search_matches ||
      self->search_matches->len == 0) {
    return;
  }

  if (index < 0 || index >= (gint)self->search_matches->len) {
    return;
  }

  gtk_text_buffer_get_bounds(self->editor->buffer, &start, &end);
  gtk_text_buffer_remove_tag_by_name(self->editor->buffer, TAG_SEARCH_CURRENT,
                                     &start, &end);
  clear_table_search_highlight(self, FALSE, TRUE);

  match = &g_array_index(self->search_matches, SearchMatch, index);
  if (match->table_anchor && match->table_row >= 0 && match->table_col >= 0) {
    GtkWidget *table_widget =
        g_object_get_data(G_OBJECT(match->table_anchor), VIEWMD_TABLE_WIDGET_DATA);
    GtkWidget *cell =
        lookup_table_cell_widget(table_widget, match->table_row, match->table_col);
    if (cell) {
      set_table_cell_highlight(cell, TRUE, TRUE);
    }
  } else {
    gtk_text_buffer_get_iter_at_offset(self->editor->buffer, &start,
                                       match->start_offset);
    gtk_text_buffer_get_iter_at_offset(self->editor->buffer, &end,
                                       match->end_offset);
    gtk_text_buffer_apply_tag_by_name(self->editor->buffer, TAG_SEARCH_CURRENT,
                                      &start, &end);
    gtk_text_buffer_place_cursor(self->editor->buffer, &start);
  }

  if (scroll_to_match && self->editor->text_view) {
    if (match->table_anchor) {
      gboolean scrolled_to_cell = FALSE;
      if (match->table_row >= 0 && match->table_col >= 0) {
        GtkWidget *table_widget = g_object_get_data(G_OBJECT(match->table_anchor),
                                                    VIEWMD_TABLE_WIDGET_DATA);
        GtkWidget *cell = lookup_table_cell_widget(table_widget, match->table_row,
                                                   match->table_col);
        if (cell) {
          scrolled_to_cell = scroll_to_table_cell(self, cell);
        }
      }
      if (!scrolled_to_cell) {
        GtkTextIter anchor_iter;
        gtk_text_buffer_get_iter_at_child_anchor(self->editor->buffer, &anchor_iter,
                                                 match->table_anchor);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(self->editor->text_view),
                                     &anchor_iter, 0.2, FALSE, 0.0, 0.0);
      }
    } else {
      gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(self->editor->text_view), &start,
                                   0.2, FALSE, 0.0, 0.0);
    }
  }

  self->search_current_index = index;
  status = g_strdup_printf("%d/%u", index + 1, self->search_matches->len);
  gtk_label_set_text(GTK_LABEL(self->lbl_search_status), status);
  g_free(status);
}

static void update_search_matches(MarkydWindow *self) {
  const gchar *query;
  GtkTextIter iter;
  GtkTextIter match_start;
  GtkTextIter match_end;
  GtkTextIter end;

  if (!self || !self->editor || !self->editor->buffer || !self->search_entry) {
    return;
  }

  query = gtk_entry_get_text(GTK_ENTRY(self->search_entry));
  clear_search_matches(self);

  if (!query || query[0] == '\0') {
    return;
  }

  ensure_search_tags(self);
  gtk_text_buffer_get_start_iter(self->editor->buffer, &iter);
  gtk_text_buffer_get_end_iter(self->editor->buffer, &end);

  while (gtk_text_iter_forward_search(&iter, query,
                                      GTK_TEXT_SEARCH_CASE_INSENSITIVE |
                                          GTK_TEXT_SEARCH_TEXT_ONLY,
                                      &match_start, &match_end, &end)) {
    SearchMatch match = {gtk_text_iter_get_offset(&match_start),
                         gtk_text_iter_get_offset(&match_end), NULL, -1, -1};
    gtk_text_buffer_apply_tag_by_name(self->editor->buffer, TAG_SEARCH_MATCH,
                                      &match_start, &match_end);
    resolve_table_match_location(self, match.start_offset, match.end_offset,
                                 &match.table_anchor, &match.table_row,
                                 &match.table_col);
    g_array_append_val(self->search_matches, match);
    iter = match_end;
  }

  if (self->search_matches->len == 0) {
    gtk_label_set_text(GTK_LABEL(self->lbl_search_status), "0 matches");
    return;
  }

  gtk_widget_set_sensitive(self->btn_search_prev, TRUE);
  gtk_widget_set_sensitive(self->btn_search_next, TRUE);
  apply_table_search_match_highlight(self);
  jump_to_search_match(self, 0, TRUE);
}

static void show_search_ui(MarkydWindow *self) {
  if (!self || !self->search_revealer || !self->search_entry) {
    return;
  }

  gtk_revealer_set_reveal_child(GTK_REVEALER(self->search_revealer), TRUE);
  gtk_widget_grab_focus(self->search_entry);
  gtk_editable_select_region(GTK_EDITABLE(self->search_entry), 0, -1);

  if (gtk_entry_get_text_length(GTK_ENTRY(self->search_entry)) > 0) {
    update_search_matches(self);
  }
}

static void hide_search_ui(MarkydWindow *self) {
  if (!self || !self->search_revealer || !self->search_entry) {
    return;
  }

  gtk_revealer_set_reveal_child(GTK_REVEALER(self->search_revealer), FALSE);
  gtk_entry_set_text(GTK_ENTRY(self->search_entry), "");
  clear_search_matches(self);
  markyd_editor_focus(self->editor);
}

static void on_search_changed(GtkEditable *editable, gpointer user_data) {
  MarkydWindow *self = (MarkydWindow *)user_data;
  (void)editable;
  update_search_matches(self);
}

static void on_search_prev_clicked(GtkButton *button, gpointer user_data) {
  MarkydWindow *self = (MarkydWindow *)user_data;
  gint index;

  (void)button;

  if (!self || !self->search_matches || self->search_matches->len == 0) {
    return;
  }

  index = self->search_current_index - 1;
  if (index < 0) {
    index = (gint)self->search_matches->len - 1;
  }
  jump_to_search_match(self, index, TRUE);
}

static void on_search_next_clicked(GtkButton *button, gpointer user_data) {
  MarkydWindow *self = (MarkydWindow *)user_data;
  gint index;

  (void)button;

  if (!self || !self->search_matches || self->search_matches->len == 0) {
    return;
  }

  index = self->search_current_index + 1;
  if (index >= (gint)self->search_matches->len) {
    index = 0;
  }
  jump_to_search_match(self, index, TRUE);
}

static gboolean on_search_entry_key_press(GtkWidget *widget, GdkEventKey *event,
                                          gpointer user_data) {
  MarkydWindow *self = (MarkydWindow *)user_data;
  gboolean shift_pressed;

  (void)widget;

  if (!event) {
    return FALSE;
  }

  shift_pressed = (event->state & GDK_SHIFT_MASK) != 0;
  if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
    if (shift_pressed) {
      on_search_prev_clicked(NULL, self);
    } else {
      on_search_next_clicked(NULL, self);
    }
    return TRUE;
  }

  if (event->keyval == GDK_KEY_Escape) {
    hide_search_ui(self);
    return TRUE;
  }

  return FALSE;
}

static void on_editor_buffer_changed(GtkTextBuffer *buffer, gpointer user_data) {
  MarkydWindow *self = (MarkydWindow *)user_data;
  (void)buffer;

  if (!self || !self->search_revealer || !self->search_entry) {
    return;
  }

  if (!gtk_revealer_get_reveal_child(GTK_REVEALER(self->search_revealer))) {
    return;
  }

  if (gtk_entry_get_text_length(GTK_ENTRY(self->search_entry)) == 0) {
    clear_search_matches(self);
    return;
  }

  update_search_matches(self);
}

MarkydWindow *markyd_window_new(MarkydApp *app) {
  MarkydWindow *self = g_new0(MarkydWindow, 1);
  GtkWidget *left_buttons;
  GtkWidget *main_box;
  GtkWidget *search_box;

  self->app = app;

  self->window = gtk_application_window_new(app->gtk_app);
  gtk_window_set_title(GTK_WINDOW(self->window), "ViewMD");

  gtk_window_set_default_size(GTK_WINDOW(self->window), config->window_width,
                              config->window_height);

  if (config->window_x >= 0 && config->window_y >= 0) {
    gtk_window_move(GTK_WINDOW(self->window), config->window_x,
                    config->window_y);
  }

  if (geometry_debug_enabled()) {
    g_printerr("ViewMD geometry init: x=%d y=%d w=%d h=%d maximized=%d\n",
               config->window_x, config->window_y, config->window_width,
               config->window_height, config->window_maximized);
  }

  if (config->window_maximized) {
    gtk_window_maximize(GTK_WINDOW(self->window));
  }

  g_signal_connect(self->window, "key-press-event",
                   G_CALLBACK(on_key_press_event), self);
  g_signal_connect(self->window, "configure-event",
                   G_CALLBACK(on_configure_event), self);
  g_signal_connect(self->window, "window-state-event",
                   G_CALLBACK(on_window_state_event), self);

  self->header_bar = gtk_header_bar_new();
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(self->header_bar), TRUE);
  gtk_window_set_titlebar(GTK_WINDOW(self->window), self->header_bar);

  self->lbl_title = gtk_label_new("ViewMD");
  gtk_widget_set_halign(self->lbl_title, GTK_ALIGN_CENTER);
  gtk_header_bar_set_custom_title(GTK_HEADER_BAR(self->header_bar),
                                  self->lbl_title);

  self->btn_open =
      gtk_button_new_from_icon_name("document-open-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(self->btn_open, "Open Markdown Document");
  g_signal_connect(self->btn_open, "clicked", G_CALLBACK(on_open_clicked), self);

  self->btn_refresh =
      gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(self->btn_refresh, "Reload Current Document");
  g_signal_connect(self->btn_refresh, "clicked", G_CALLBACK(on_refresh_clicked),
                   self);

  self->btn_settings =
      gtk_button_new_from_icon_name("emblem-system-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(self->btn_settings, "Settings");
  g_signal_connect(self->btn_settings, "clicked", G_CALLBACK(on_settings_clicked),
                   self);

  left_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(left_buttons), self->btn_open, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(left_buttons), self->btn_refresh, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(left_buttons), self->btn_settings, FALSE, FALSE, 0);
  gtk_header_bar_pack_start(GTK_HEADER_BAR(self->header_bar), left_buttons);

  main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(self->window), main_box);

  self->search_revealer = gtk_revealer_new();
  gtk_revealer_set_transition_type(GTK_REVEALER(self->search_revealer),
                                   GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
  gtk_revealer_set_reveal_child(GTK_REVEALER(self->search_revealer), FALSE);
  gtk_box_pack_start(GTK_BOX(main_box), self->search_revealer, FALSE, FALSE, 0);

  search_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_margin_top(search_box, 6);
  gtk_widget_set_margin_bottom(search_box, 6);
  gtk_widget_set_margin_start(search_box, 8);
  gtk_widget_set_margin_end(search_box, 8);
  gtk_container_add(GTK_CONTAINER(self->search_revealer), search_box);

  self->search_entry = gtk_search_entry_new();
  gtk_widget_set_hexpand(self->search_entry, TRUE);
  gtk_widget_set_tooltip_text(self->search_entry, "Find in document");
  g_signal_connect(self->search_entry, "changed", G_CALLBACK(on_search_changed),
                   self);
  g_signal_connect(self->search_entry, "key-press-event",
                   G_CALLBACK(on_search_entry_key_press), self);
  gtk_box_pack_start(GTK_BOX(search_box), self->search_entry, TRUE, TRUE, 0);

  self->btn_search_prev =
      gtk_button_new_from_icon_name("go-up-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(self->btn_search_prev, "Previous Match");
  g_signal_connect(self->btn_search_prev, "clicked",
                   G_CALLBACK(on_search_prev_clicked), self);
  gtk_widget_set_sensitive(self->btn_search_prev, FALSE);
  gtk_box_pack_start(GTK_BOX(search_box), self->btn_search_prev, FALSE, FALSE, 0);

  self->btn_search_next =
      gtk_button_new_from_icon_name("go-down-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(self->btn_search_next, "Next Match");
  g_signal_connect(self->btn_search_next, "clicked",
                   G_CALLBACK(on_search_next_clicked), self);
  gtk_widget_set_sensitive(self->btn_search_next, FALSE);
  gtk_box_pack_start(GTK_BOX(search_box), self->btn_search_next, FALSE, FALSE, 0);

  self->lbl_search_status = gtk_label_new("");
  gtk_widget_set_halign(self->lbl_search_status, GTK_ALIGN_END);
  gtk_box_pack_start(GTK_BOX(search_box), self->lbl_search_status, FALSE, FALSE, 0);

  self->scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(self->scroll),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX(main_box), self->scroll, TRUE, TRUE, 0);

  self->editor = markyd_editor_new(app);
  gtk_container_add(GTK_CONTAINER(self->scroll),
                    markyd_editor_get_widget(self->editor));
  self->search_matches = g_array_new(FALSE, FALSE, sizeof(SearchMatch));
  self->search_current_index = -1;
  g_signal_connect(self->editor->buffer, "changed",
                   G_CALLBACK(on_editor_buffer_changed), self);
  ensure_search_tags(self);

  markyd_window_apply_css(self);

  gtk_widget_show_all(self->window);

  return self;
}

void markyd_window_apply_css(MarkydWindow *self) {
  static GtkCssProvider *css = NULL;
  gchar *css_str;
  const gchar *bg;
  const gchar *fg;
  const gchar *sel_bg;
  const gchar *table_bg;
  const gchar *table_fg;
  const gchar *table_header_bg;
  const gchar *table_border;
  const gchar *search_match_bg;
  const gchar *search_match_fg;
  const gchar *search_current_bg;
  const gchar *search_current_fg;

  if (css) {
    gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(),
                                                 GTK_STYLE_PROVIDER(css));
    g_object_unref(css);
  }

  css = gtk_css_provider_new();

  if (g_strcmp0(config->theme, "light") == 0) {
    bg = "#ffffff";
    fg = "#111111";
    sel_bg = "#cfe3ff";
    search_match_bg = "#fff3b0";
    search_match_fg = "#111111";
    search_current_bg = "#ffd166";
    search_current_fg = "#111111";
  } else if (g_strcmp0(config->theme, "dark") == 0) {
    bg = "#1e1e1e";
    fg = "#e8e8e8";
    sel_bg = "#264f78";
    search_match_bg = "#3e3a12";
    search_match_fg = "#f4f4e8";
    search_current_bg = "#66551f";
    search_current_fg = "#f4f4e8";
  } else {
    bg = "@theme_base_color";
    fg = "@theme_text_color";
    sel_bg = "@theme_selected_bg_color";
    search_match_bg = "@theme_selected_bg_color";
    search_match_fg = "@theme_selected_fg_color";
    search_current_bg = "@theme_selected_bg_color";
    search_current_fg = "@theme_selected_fg_color";
  }

  table_bg = bg;
  table_fg = fg;
  table_header_bg = table_bg;
  table_border = sel_bg;

  css_str = g_strdup_printf(
      "textview {"
      "  font-family: '%s', 'Inter', 'Noto Sans', sans-serif;"
      "  font-size: %dpt;"
      "  padding: 0px;"
      "  background-color: %s;"
      "  color: %s;"
      "  caret-color: %s;"
      "}"
      "textview text {"
      "  background-color: %s;"
      "  color: %s;"
      "  caret-color: %s;"
      "}"
      "textview text selection {"
      "  background-color: %s;"
      "}"
      "scrolledwindow {"
      "  background-color: %s;"
      "  border: none;"
      "}"
      "window {"
      "  background-color: %s;"
      "}"
      ".viewmd-table-cell {"
      "  background-color: %s;"
      "  border-style: solid;"
      "  border-width: 1px;"
      "  border-color: %s;"
      "}"
      ".viewmd-table-header-cell {"
      "  background-color: %s;"
      "}"
      ".viewmd-table-cell label {"
      "  color: %s;"
      "}"
      ".viewmd-table-cell." VIEWMD_TABLE_CELL_MATCH_CLASS " {"
      "  background-color: %s;"
      "}"
      ".viewmd-table-cell." VIEWMD_TABLE_CELL_MATCH_CLASS " label {"
      "  color: %s;"
      "}"
      ".viewmd-table-cell." VIEWMD_TABLE_CELL_CURRENT_CLASS " {"
      "  background-color: %s;"
      "}"
      ".viewmd-table-cell." VIEWMD_TABLE_CELL_CURRENT_CLASS " label {"
      "  color: %s;"
      "}",
      config->font_family, config->font_size, bg, fg, fg, bg, fg, fg, sel_bg, bg,
      bg, table_bg, table_border, table_header_bg, table_fg, search_match_bg,
      search_match_fg, search_current_bg, search_current_fg);

  gtk_css_provider_load_from_data(css, css_str, -1, NULL);
  g_free(css_str);

  gtk_style_context_add_provider_for_screen(
      gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  update_search_tag_styles(self, search_match_bg, search_match_fg,
                           search_current_bg, search_current_fg);

}

void markyd_window_free(MarkydWindow *self) {
  if (!self)
    return;

  if (self->search_matches) {
    g_array_free(self->search_matches, TRUE);
    self->search_matches = NULL;
  }

  if (self->editor) {
    markyd_editor_free(self->editor);
  }

  if (self->window) {
    gtk_widget_destroy(self->window);
  }

  g_free(self);
}

void markyd_window_show(MarkydWindow *self) {
  gtk_widget_show(self->window);
  if (!config->window_maximized && config->window_x >= 0 && config->window_y >= 0) {
    gtk_window_move(GTK_WINDOW(self->window), config->window_x, config->window_y);
  }
  gtk_window_present(GTK_WINDOW(self->window));
}

void markyd_window_hide(MarkydWindow *self) { gtk_widget_hide(self->window); }

void markyd_window_toggle(MarkydWindow *self) {
  if (markyd_window_is_visible(self)) {
    markyd_window_hide(self);
  } else {
    markyd_window_show(self);
  }
}

gboolean markyd_window_is_visible(MarkydWindow *self) {
  return gtk_widget_get_visible(self->window);
}

static void on_open_clicked(GtkButton *button, gpointer user_data) {
  MarkydWindow *self = (MarkydWindow *)user_data;
  GtkWidget *dialog;
  GtkFileFilter *md_filter;
  gint response;

  (void)button;

  dialog = gtk_file_chooser_dialog_new(
      "Open Markdown Document", GTK_WINDOW(self->window),
      GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Open",
      GTK_RESPONSE_ACCEPT, NULL);

  md_filter = gtk_file_filter_new();
  gtk_file_filter_set_name(md_filter, "Markdown files (*.md, *.markdown)");
  gtk_file_filter_add_pattern(md_filter, "*.md");
  gtk_file_filter_add_pattern(md_filter, "*.markdown");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), md_filter);

  md_filter = gtk_file_filter_new();
  gtk_file_filter_set_name(md_filter, "All files");
  gtk_file_filter_add_pattern(md_filter, "*");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), md_filter);

  response = gtk_dialog_run(GTK_DIALOG(dialog));
  if (response == GTK_RESPONSE_ACCEPT) {
    gchar *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    if (path) {
      if (!markyd_app_open_file(self->app, path)) {
        GtkWidget *error_dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE, "Failed to open document");
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(error_dialog), "%s", path);
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
      }
      g_free(path);
    }
  }

  gtk_widget_destroy(dialog);
}

static void on_refresh_clicked(GtkButton *button, gpointer user_data) {
  MarkydWindow *self = (MarkydWindow *)user_data;
  const gchar *path;

  (void)button;

  path = markyd_app_get_current_path(self->app);
  if (!path || path[0] == '\0') {
    return;
  }

  if (!markyd_app_open_file(self->app, path)) {
    GtkWidget *error_dialog = gtk_message_dialog_new(
        GTK_WINDOW(self->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
        GTK_BUTTONS_CLOSE, "Failed to reload document");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(error_dialog),
                                             "%s", path);
    gtk_dialog_run(GTK_DIALOG(error_dialog));
    gtk_widget_destroy(error_dialog);
  }
}

static void on_settings_clicked(GtkButton *button, gpointer user_data) {
  MarkydWindow *self = (MarkydWindow *)user_data;
  GtkWidget *dialog;
  gint response;

  (void)button;

  dialog = create_settings_dialog(self->app);
  response = gtk_dialog_run(GTK_DIALOG(dialog));

  if (response == GTK_RESPONSE_APPLY || response == GTK_RESPONSE_OK) {
    config_save(config);
    markyd_window_apply_css(self);
    markdown_update_accent_tags(self->editor->buffer);
    markyd_editor_refresh(self->editor);
  }

  gtk_widget_destroy(dialog);
}

static gboolean on_key_press_event(GtkWidget *widget, GdkEventKey *event,
                                   gpointer user_data) {
  MarkydWindow *self = (MarkydWindow *)user_data;

  if (!event || !self) {
    return FALSE;
  }

  if ((event->state & GDK_CONTROL_MASK) != 0 &&
      (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F)) {
    show_search_ui(self);
    return TRUE;
  }

  if (event->keyval == GDK_KEY_Escape &&
      self->search_revealer &&
      gtk_revealer_get_reveal_child(GTK_REVEALER(self->search_revealer))) {
    hide_search_ui(self);
    return TRUE;
  }

  if (event->keyval == GDK_KEY_Escape) {
    gtk_window_close(GTK_WINDOW(widget));
    return TRUE;
  }

  return FALSE;
}

static gboolean on_configure_event(GtkWidget *widget, GdkEventConfigure *event,
                                   gpointer user_data) {
  MarkydWindow *self = (MarkydWindow *)user_data;
  (void)self;

  GdkWindow *gdk_window = gtk_widget_get_window(widget);
  if (gdk_window) {
    GdkWindowState state = gdk_window_get_state(gdk_window);
    if (state & GDK_WINDOW_STATE_MAXIMIZED) {
      return FALSE;
    }
  } else if (config->window_maximized) {
    return FALSE;
  }

  {
    gint x, y;
    gint width, height;
    gint gtk_w = 0, gtk_h = 0;
    GdkRectangle frame_extents;
    gboolean have_extents = FALSE;

    gtk_window_get_position(GTK_WINDOW(widget), &x, &y);
    config->window_x = x;
    config->window_y = y;

    gtk_window_get_size(GTK_WINDOW(widget), &gtk_w, &gtk_h);

    width = gtk_w > 0 ? gtk_w : event->width;
    height = gtk_h > 0 ? gtk_h : event->height;

    if (gdk_window) {
      gdk_window_get_frame_extents(gdk_window, &frame_extents);
      have_extents = TRUE;
    }

    config->window_width = width;
    config->window_height = height;

    if (geometry_debug_enabled()) {
      if (have_extents) {
        g_printerr(
            "ViewMD configure: event=%dx%d gtk=%dx%d saved=%dx%d frame=%dx%d at (%d,%d)\n",
            event->width, event->height, gtk_w, gtk_h, width, height,
            frame_extents.width, frame_extents.height, x, y);
      } else {
        g_printerr("ViewMD configure: event=%dx%d gtk=%dx%d saved=%dx%d at (%d,%d)\n",
                   event->width, event->height, gtk_w, gtk_h, width, height, x,
                   y);
      }
      g_printerr("ViewMD saved: x=%d y=%d w=%d h=%d\n", config->window_x,
                 config->window_y, config->window_width, config->window_height);
    }
  }

  return FALSE;
}

static gboolean on_window_state_event(GtkWidget *widget,
                                      GdkEventWindowState *event,
                                      gpointer user_data) {
  (void)widget;
  (void)user_data;

  config->window_maximized =
      (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;

  return FALSE;
}

static void on_font_size_changed(GtkSpinButton *spin, gpointer user_data) {
  (void)user_data;
  config->font_size = gtk_spin_button_get_value_as_int(spin);
}

static void on_font_family_changed(GtkComboBoxText *combo, gpointer user_data) {
  (void)user_data;
  g_free(config->font_family);
  config->font_family = gtk_combo_box_text_get_active_text(combo);
}

static void on_theme_changed(GtkComboBoxText *combo, gpointer user_data) {
  (void)user_data;
  g_free(config->theme);
  config->theme = gtk_combo_box_text_get_active_text(combo);
}

static gchar *rgba_to_hex(const GdkRGBA *rgba) {
  guint r = (guint)(CLAMP(rgba->red, 0.0, 1.0) * 255.0 + 0.5);
  guint g = (guint)(CLAMP(rgba->green, 0.0, 1.0) * 255.0 + 0.5);
  guint b = (guint)(CLAMP(rgba->blue, 0.0, 1.0) * 255.0 + 0.5);
  return g_strdup_printf("#%02X%02X%02X", r, g, b);
}

static void init_color_button(GtkColorButton *btn, const gchar *color_str) {
  GdkRGBA rgba;
  if (color_str && gdk_rgba_parse(&rgba, color_str)) {
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(btn), &rgba);
  }
}

static void on_color_set(GtkColorButton *btn, gpointer user_data) {
  gchar **target = (gchar **)user_data;
  GdkRGBA rgba;

  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(btn), &rgba);
  g_free(*target);
  *target = rgba_to_hex(&rgba);
}

static GtkWidget *create_settings_dialog(MarkydApp *app) {
  GtkWidget *dialog;
  GtkWidget *content;
  GtkWidget *grid;
  GtkWidget *label;
  GtkWidget *font_family_combo;
  GtkWidget *font_size_spin;
  GtkWidget *theme_combo;
  GtkWidget *h1_color_btn;
  GtkWidget *h2_color_btn;
  GtkWidget *h3_color_btn;
  GtkWidget *bullet_color_btn;
  gint row = 0;

  dialog = gtk_dialog_new_with_buttons(
      "ViewMD Settings", GTK_WINDOW(app->window->window),
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel",
      GTK_RESPONSE_CANCEL, "_Apply", GTK_RESPONSE_APPLY, NULL);

  gtk_window_set_default_size(GTK_WINDOW(dialog), 400, -1);

  content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  gtk_container_set_border_width(GTK_CONTAINER(content), 16);
  gtk_widget_set_margin_bottom(content, 12);

  grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
  gtk_container_add(GTK_CONTAINER(content), grid);

  label = gtk_label_new("Theme:");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

  theme_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_combo), "dark");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_combo), "light");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_combo), "system");

  if (g_strcmp0(config->theme, "light") == 0)
    gtk_combo_box_set_active(GTK_COMBO_BOX(theme_combo), 1);
  else if (g_strcmp0(config->theme, "system") == 0)
    gtk_combo_box_set_active(GTK_COMBO_BOX(theme_combo), 2);
  else
    gtk_combo_box_set_active(GTK_COMBO_BOX(theme_combo), 0);

  g_signal_connect(theme_combo, "changed", G_CALLBACK(on_theme_changed), NULL);
  gtk_widget_set_hexpand(theme_combo, TRUE);
  gtk_grid_attach(GTK_GRID(grid), theme_combo, 1, row++, 1, 1);

  label = gtk_label_new("Font:");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

  font_family_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_family_combo),
                                 "Cantarell");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_family_combo),
                                 "Inter");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_family_combo),
                                 "Noto Sans");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_family_combo),
                                 "Ubuntu");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_family_combo),
                                 "Roboto");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_family_combo),
                                 "Monospace");

  {
    gint font_idx = 0;
    const gchar *fonts[] = {"Cantarell", "Inter",     "Noto Sans", "Ubuntu",
                            "Roboto",    "Monospace", NULL};
    for (gint i = 0; fonts[i]; i++) {
      if (g_strcmp0(config->font_family, fonts[i]) == 0) {
        font_idx = i;
        break;
      }
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(font_family_combo), font_idx);
  }

  g_signal_connect(font_family_combo, "changed",
                   G_CALLBACK(on_font_family_changed), NULL);
  gtk_widget_set_hexpand(font_family_combo, TRUE);
  gtk_grid_attach(GTK_GRID(grid), font_family_combo, 1, row++, 1, 1);

  label = gtk_label_new("Font Size:");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

  font_size_spin = gtk_spin_button_new_with_range(8, 48, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(font_size_spin), config->font_size);
  g_signal_connect(font_size_spin, "value-changed",
                   G_CALLBACK(on_font_size_changed), NULL);
  gtk_grid_attach(GTK_GRID(grid), font_size_spin, 1, row++, 1, 1);

  {
    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_grid_attach(GTK_GRID(grid), separator, 0, row++, 2, 1);
  }

  label = gtk_label_new("Heading 1:");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  h1_color_btn = gtk_color_button_new();
  init_color_button(GTK_COLOR_BUTTON(h1_color_btn), config->h1_color);
  gtk_widget_set_halign(h1_color_btn, GTK_ALIGN_START);
  g_signal_connect(h1_color_btn, "color-set", G_CALLBACK(on_color_set),
                   &config->h1_color);
  gtk_grid_attach(GTK_GRID(grid), h1_color_btn, 1, row++, 1, 1);

  label = gtk_label_new("Heading 2:");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  h2_color_btn = gtk_color_button_new();
  init_color_button(GTK_COLOR_BUTTON(h2_color_btn), config->h2_color);
  gtk_widget_set_halign(h2_color_btn, GTK_ALIGN_START);
  g_signal_connect(h2_color_btn, "color-set", G_CALLBACK(on_color_set),
                   &config->h2_color);
  gtk_grid_attach(GTK_GRID(grid), h2_color_btn, 1, row++, 1, 1);

  label = gtk_label_new("Heading 3:");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  h3_color_btn = gtk_color_button_new();
  init_color_button(GTK_COLOR_BUTTON(h3_color_btn), config->h3_color);
  gtk_widget_set_halign(h3_color_btn, GTK_ALIGN_START);
  g_signal_connect(h3_color_btn, "color-set", G_CALLBACK(on_color_set),
                   &config->h3_color);
  gtk_grid_attach(GTK_GRID(grid), h3_color_btn, 1, row++, 1, 1);

  label = gtk_label_new("List bullet:");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  bullet_color_btn = gtk_color_button_new();
  init_color_button(GTK_COLOR_BUTTON(bullet_color_btn),
                    config->list_bullet_color);
  gtk_widget_set_halign(bullet_color_btn, GTK_ALIGN_START);
  g_signal_connect(bullet_color_btn, "color-set", G_CALLBACK(on_color_set),
                   &config->list_bullet_color);
  gtk_grid_attach(GTK_GRID(grid), bullet_color_btn, 1, row++, 1, 1);

  gtk_widget_show_all(dialog);

  return dialog;
}

#include "window.h"
#include "app.h"
#include "config.h"
#include "editor.h"
#include "markdown.h"

static void on_open_clicked(GtkButton *button, gpointer user_data);
static void on_refresh_clicked(GtkButton *button, gpointer user_data);
static void on_settings_clicked(GtkButton *button, gpointer user_data);
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

MarkydWindow *markyd_window_new(MarkydApp *app) {
  MarkydWindow *self = g_new0(MarkydWindow, 1);
  GtkWidget *left_buttons;

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

  self->scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(self->scroll),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(self->window), self->scroll);

  self->editor = markyd_editor_new(app);
  gtk_container_add(GTK_CONTAINER(self->scroll),
                    markyd_editor_get_widget(self->editor));

  markyd_window_apply_css(self);

  gtk_widget_show_all(self->header_bar);
  gtk_widget_show_all(self->scroll);

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
  } else if (g_strcmp0(config->theme, "dark") == 0) {
    bg = "#1e1e1e";
    fg = "#e8e8e8";
    sel_bg = "#264f78";
  } else {
    bg = "@theme_base_color";
    fg = "@theme_text_color";
    sel_bg = "@theme_selected_bg_color";
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
      "}",
      config->font_family, config->font_size, bg, fg, fg, bg, fg, fg, sel_bg, bg,
      bg, table_bg, table_border, table_header_bg, table_fg);

  gtk_css_provider_load_from_data(css, css_str, -1, NULL);
  g_free(css_str);

  gtk_style_context_add_provider_for_screen(
      gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  (void)self;
}

void markyd_window_free(MarkydWindow *self) {
  if (!self)
    return;

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
  (void)user_data;
  (void)widget;

  if (event && event->keyval == GDK_KEY_Escape) {
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

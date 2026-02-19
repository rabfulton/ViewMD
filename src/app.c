#include "app.h"
#include "config.h"
#include "editor.h"
#include "window.h"

/* Global app instance */
MarkydApp *app = NULL;

static void on_activate(GtkApplication *gtk_app, gpointer user_data);
static void on_open(GtkApplication *gtk_app, GFile **files, gint n_files,
                    const gchar *hint, gpointer user_data);
static void markyd_app_update_window_title(MarkydApp *self);
static void markyd_app_ensure_window(MarkydApp *self);

MarkydApp *markyd_app_new(void) {
  MarkydApp *self = g_new0(MarkydApp, 1);

  config = config_new();
  config_load(config);

  GApplicationFlags flags =
#if GLIB_CHECK_VERSION(2, 74, 0)
      G_APPLICATION_DEFAULT_FLAGS;
#else
      G_APPLICATION_FLAGS_NONE;
#endif
  flags = (GApplicationFlags)(flags | G_APPLICATION_NON_UNIQUE |
                              G_APPLICATION_HANDLES_OPEN);

  self->gtk_app = gtk_application_new("org.viewmd.app", flags);
  self->current_file_path = NULL;

  g_signal_connect(self->gtk_app, "activate", G_CALLBACK(on_activate), self);
  g_signal_connect(self->gtk_app, "open", G_CALLBACK(on_open), self);

  app = self;
  return self;
}

void markyd_app_free(MarkydApp *self) {
  if (!self)
    return;

  if (self->window) {
    markyd_window_free(self->window);
  }

  g_free(self->current_file_path);
  g_object_unref(self->gtk_app);

  config_save(config);
  config_free(config);
  config = NULL;

  g_free(self);
  app = NULL;
}

int markyd_app_run(MarkydApp *self, int argc, char **argv) {
  return g_application_run(G_APPLICATION(self->gtk_app), argc, argv);
}

static void markyd_app_update_window_title(MarkydApp *self) {
  gchar *title;

  if (!self || !self->window || !self->window->window) {
    return;
  }

  if (self->current_file_path && self->current_file_path[0] != '\0') {
    gchar *base = g_path_get_basename(self->current_file_path);
    title = g_strdup_printf("ViewMD - %s", base);
    g_free(base);
  } else {
    title = g_strdup("ViewMD");
  }

  gtk_window_set_title(GTK_WINDOW(self->window->window), title);
  g_free(title);
}

static void on_activate(GtkApplication *gtk_app, gpointer user_data) {
  MarkydApp *self = (MarkydApp *)user_data;

  (void)gtk_app;
  markyd_app_ensure_window(self);
  markyd_window_show(self->window);
}

static void on_open(GtkApplication *gtk_app, GFile **files, gint n_files,
                    const gchar *hint, gpointer user_data) {
  MarkydApp *self = (MarkydApp *)user_data;
  gboolean opened = FALSE;

  (void)gtk_app;
  (void)hint;

  markyd_app_ensure_window(self);

  for (gint i = 0; i < n_files; i++) {
    gchar *path = g_file_get_path(files[i]);
    if (!path) {
      continue;
    }
    if (markyd_app_open_file(self, path)) {
      opened = TRUE;
      g_free(path);
      break;
    }
    g_free(path);
  }

  if (!opened && n_files > 0) {
    g_printerr("ViewMD: unable to open provided file(s)\n");
  }

  markyd_window_show(self->window);
}

static void markyd_app_ensure_window(MarkydApp *self) {
  if (self->window) {
    return;
  }

  self->window = markyd_window_new(self);
  self->editor = self->window->editor;

  markyd_editor_set_content(self->editor,
                            "# ViewMD\n\nUse the Open button to load a markdown document.");
  markyd_app_update_window_title(self);
}

gboolean markyd_app_open_file(MarkydApp *self, const gchar *path) {
  gchar *content = NULL;
  GError *error = NULL;

  if (!self || !self->editor || !path || path[0] == '\0') {
    return FALSE;
  }

  if (!g_file_get_contents(path, &content, NULL, &error)) {
    if (error) {
      g_printerr("Failed to load markdown file '%s': %s\n", path, error->message);
      g_error_free(error);
    }
    return FALSE;
  }

  markyd_editor_set_content(self->editor, content);
  g_free(content);

  g_free(self->current_file_path);
  self->current_file_path = g_strdup(path);

  markyd_app_update_window_title(self);
  return TRUE;
}

const gchar *markyd_app_get_current_path(MarkydApp *self) {
  if (!self) {
    return NULL;
  }
  return self->current_file_path;
}

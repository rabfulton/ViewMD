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
static void stop_file_watch(MarkydApp *self);

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

  stop_file_watch(self);
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

static void stop_file_watch(MarkydApp *self) {
  if (self->watch_reload_id) {
    g_source_remove(self->watch_reload_id);
    self->watch_reload_id = 0;
  }
  if (self->file_monitor) {
    g_signal_handlers_disconnect_by_data(self->file_monitor, self);
    g_file_monitor_cancel(self->file_monitor);
    g_clear_object(&self->file_monitor);
  }
  g_clear_object(&self->watched_file);
}

static gboolean reload_changed_file(gpointer user_data) {
  MarkydApp *self = user_data;
  gchar *content = NULL;
  self->watch_reload_id = 0;

  if (!config->auto_reload || !self->editor || !self->current_file_path ||
      (self->window && !self->window->window)) {
    return G_SOURCE_REMOVE;
  }

  /* A save may temporarily remove the file. Keep the last good preview; the
   * directory monitor will also see the replacement or a later recreation. */
  if (g_file_get_contents(self->current_file_path, &content, NULL, NULL)) {
    gchar *valid = g_utf8_make_valid(content, -1);
    if (g_strcmp0(valid, self->editor->source_content) != 0) {
      markyd_editor_reload_content(self->editor, valid);
    }
    g_free(valid);
    g_free(content);
  }
  return G_SOURCE_REMOVE;
}

static void queue_file_reload(MarkydApp *self) {
  if (self->watch_reload_id)
    g_source_remove(self->watch_reload_id);
  self->watch_reload_id = g_timeout_add(150, reload_changed_file, self);
}

static void on_watched_file_changed(GFileMonitor *monitor, GFile *file,
                                    GFile *other_file, GFileMonitorEvent event,
                                    gpointer user_data) {
  MarkydApp *self = user_data;
  (void)monitor;

  if (!self->watched_file ||
      !((file && g_file_equal(file, self->watched_file)) ||
        (other_file && g_file_equal(other_file, self->watched_file)))) {
    return;
  }
  switch (event) {
  case G_FILE_MONITOR_EVENT_CHANGED:
  case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
  case G_FILE_MONITOR_EVENT_CREATED:
  case G_FILE_MONITOR_EVENT_DELETED:
  case G_FILE_MONITOR_EVENT_MOVED:
  case G_FILE_MONITOR_EVENT_RENAMED:
  case G_FILE_MONITOR_EVENT_MOVED_IN:
  case G_FILE_MONITOR_EVENT_MOVED_OUT:
    queue_file_reload(self);
    break;
  default:
    break;
  }
}

void markyd_app_update_file_watch(MarkydApp *self) {
  GFile *file;
  GFile *directory;
  GError *error = NULL;

  if (!self)
    return;
  if (!config->auto_reload || !self->current_file_path) {
    stop_file_watch(self);
    return;
  }

  file = g_file_new_for_path(self->current_file_path);
  if (self->file_monitor && g_file_equal(file, self->watched_file)) {
    g_object_unref(file);
    return;
  }
  stop_file_watch(self);
  self->watched_file = file;

  /* Watching the parent keeps working when editors save by renaming a new
   * file over the old one. Events for unrelated directory entries are ignored. */
  directory = g_file_get_parent(file);
  if (!directory)
    return;
  self->file_monitor = g_file_monitor_directory(
      directory, G_FILE_MONITOR_WATCH_MOVES, NULL, &error);
  g_object_unref(directory);
  if (!self->file_monitor) {
    g_printerr("ViewMD: could not watch '%s': %s\n", self->current_file_path,
               error ? error->message : "file monitoring unavailable");
    g_clear_error(&error);
    return;
  }
  g_signal_connect(self->file_monitor, "changed",
                   G_CALLBACK(on_watched_file_changed), self);
  /* Catch changes between loading and attaching the monitor, or while the
   * preference was disabled. Identical content does not trigger rendering. */
  queue_file_reload(self);
}

gboolean markyd_app_open_file(MarkydApp *self, const gchar *path) {
  gchar *content = NULL;
  gchar *new_path;
  GError *error = NULL;

  if (!self || !self->editor || !path || path[0] == '\0') {
    return FALSE;
  }

  /* Copy before replacing current_file_path: callers may pass that pointer. */
  new_path = g_canonicalize_filename(path, NULL);
  if (!g_file_get_contents(new_path, &content, NULL, &error)) {
    if (error) {
      g_printerr("Failed to load markdown file '%s': %s\n", path, error->message);
      g_error_free(error);
    }
    g_free(new_path);
    return FALSE;
  }

  markyd_editor_set_content(self->editor, content);
  g_free(content);

  g_free(self->current_file_path);
  self->current_file_path = new_path;

  markyd_app_update_window_title(self);
  markyd_app_update_file_watch(self);
  return TRUE;
}

const gchar *markyd_app_get_current_path(MarkydApp *self) {
  if (!self) {
    return NULL;
  }
  return self->current_file_path;
}

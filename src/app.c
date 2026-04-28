#include "app.h"
#include "config.h"
#include "editor.h"
#include "window.h"
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <glib-unix.h>

/* Global app instance */
MarkydApp *app = NULL;

static void on_activate(GtkApplication *gtk_app, gpointer user_data);
static void on_open(GtkApplication *gtk_app, GFile **files, gint n_files,
                    const gchar *hint, gpointer user_data);
static void markyd_app_update_window_title(MarkydApp *self);
static void markyd_app_ensure_window(MarkydApp *self);
static void markyd_app_stop_watch(MarkydApp *self);
static void markyd_app_start_watch(MarkydApp *self);
static void markyd_app_parse_args(MarkydApp *self, int *argc, char **argv);
static void markyd_app_setup_socket(MarkydApp *self);
static void markyd_app_teardown_socket(MarkydApp *self);

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
  self->watch_mode = FALSE;
  self->file_monitor = NULL;
  self->watch_reload_timeout_id = 0;
  self->socket_mode = FALSE;
  self->socket_path = NULL;
  self->sock_fd = -1;

  g_signal_connect(self->gtk_app, "activate", G_CALLBACK(on_activate), self);
  g_signal_connect(self->gtk_app, "open", G_CALLBACK(on_open), self);

  app = self;
  return self;
}

void markyd_app_free(MarkydApp *self) {
  if (!self)
    return;

  markyd_app_stop_watch(self);
  markyd_app_teardown_socket(self);

  if (self->window) {
    markyd_window_free(self->window);
  }

  g_free(self->current_file_path);
  g_free(self->socket_path);
  g_object_unref(self->gtk_app);

  config_save(config);
  config_free(config);
  config = NULL;

  g_free(self);
  app = NULL;
}

static void markyd_app_parse_args(MarkydApp *self, int *argc, char **argv) {
  for (gint i = 1; i < *argc; i++) {
    gboolean matched = FALSE;
    if (g_strcmp0(argv[i], "--watch") == 0 || g_strcmp0(argv[i], "-w") == 0) {
      self->watch_mode = TRUE;
      matched = TRUE;
    } else if (g_strcmp0(argv[i], "--socket") == 0) {
      self->socket_mode = TRUE;
      matched = TRUE;
    }
    if (matched) {
      for (gint j = i; j < *argc - 1; j++) {
        argv[j] = argv[j + 1];
      }
      (*argc)--;
      i--;
    }
  }
}

int markyd_app_run(MarkydApp *self, int argc, char **argv) {
  markyd_app_parse_args(self, &argc, argv);
  if (self->socket_mode)
    markyd_app_setup_socket(self);
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

static void markyd_app_stop_watch(MarkydApp *self) {
  if (self->watch_reload_timeout_id) {
    g_source_remove(self->watch_reload_timeout_id);
    self->watch_reload_timeout_id = 0;
  }
  if (self->file_monitor) {
    g_file_monitor_cancel(self->file_monitor);
    g_object_unref(self->file_monitor);
    self->file_monitor = NULL;
  }
}

static gboolean on_watch_reload_timeout(gpointer user_data) {
  MarkydApp *self = (MarkydApp *)user_data;
  self->watch_reload_timeout_id = 0;
  if (self->current_file_path) {
    markyd_app_open_file(self, self->current_file_path);
  }
  return G_SOURCE_REMOVE;
}

static void on_file_changed(GFileMonitor *monitor, GFile *file,
                            GFile *other_file, GFileMonitorEvent event_type,
                            gpointer user_data) {
  MarkydApp *self = (MarkydApp *)user_data;
  (void)monitor;
  (void)file;
  (void)other_file;

  if (event_type != G_FILE_MONITOR_EVENT_CHANGED &&
      event_type != G_FILE_MONITOR_EVENT_CREATED) {
    return;
  }

  /* Debounce: reset timer on every event, reload after quiet period. */
  if (self->watch_reload_timeout_id) {
    g_source_remove(self->watch_reload_timeout_id);
  }
  self->watch_reload_timeout_id =
      g_timeout_add(100, on_watch_reload_timeout, self);
}

static void markyd_app_start_watch(MarkydApp *self) {
  GFile *gfile;
  GError *error = NULL;

  if (!self->watch_mode || !self->current_file_path) {
    return;
  }

  gfile = g_file_new_for_path(self->current_file_path);
  self->file_monitor =
      g_file_monitor_file(gfile, G_FILE_MONITOR_NONE, NULL, &error);
  g_object_unref(gfile);

  if (!self->file_monitor) {
    if (error) {
      g_printerr("ViewMD: could not watch file: %s\n", error->message);
      g_error_free(error);
    }
    return;
  }

  g_signal_connect(self->file_monitor, "changed",
                   G_CALLBACK(on_file_changed), self);
}

static gboolean on_socket_accept(gint fd, GIOCondition cond, gpointer user_data) {
  MarkydApp *self = (MarkydApp *)user_data;
  int client;
  GString *buf;
  char chunk[4096];
  ssize_t n;

  (void)cond;

  client = accept(fd, NULL, NULL);
  if (client < 0) {
    return G_SOURCE_CONTINUE;
  }

  buf = g_string_new(NULL);
  while ((n = read(client, chunk, sizeof(chunk))) > 0) {
    g_string_append_len(buf, chunk, n);
  }
  close(client);

  if (self->editor) {
    const gchar *raw = buf->str;
    gint cursor_line = -1;
    /* Optional first line: "CURSOR:<line>\n" — strip it and record the line. */
    if (g_str_has_prefix(raw, "CURSOR:")) {
      const gchar *nl = strchr(raw, '\n');
      if (nl) {
        cursor_line = (gint)g_ascii_strtoll(raw + 7, NULL, 10);
        raw = nl + 1;
      }
    }
    {
      const gchar *dbg = g_getenv("VIEWMD_DEBUG_SCROLL");
      if (dbg && dbg[0] != '\0' && g_strcmp0(dbg, "0") != 0)
        g_printerr("socket: cursor_line=%d content_len=%zu\n",
                   cursor_line, strlen(raw));
    }
    self->editor->pending_cursor_line = cursor_line;
    markyd_editor_set_content(self->editor, raw);
  }
  g_string_free(buf, TRUE);
  return G_SOURCE_CONTINUE;
}

static void markyd_app_setup_socket(MarkydApp *self) {
  struct sockaddr_un addr;

  self->socket_path = g_strdup_printf("/tmp/viewmd-%d.sock", (int)getpid());
  self->sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (self->sock_fd < 0) {
    g_printerr("ViewMD: failed to create socket\n");
    return;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, self->socket_path, sizeof(addr.sun_path) - 1);

  unlink(self->socket_path);
  if (bind(self->sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
      listen(self->sock_fd, 4) < 0) {
    g_printerr("ViewMD: failed to bind socket %s\n", self->socket_path);
    close(self->sock_fd);
    self->sock_fd = -1;
    return;
  }

  g_unix_fd_add(self->sock_fd, G_IO_IN, on_socket_accept, self);
  g_print("VIEWMD_SOCKET=%s\n", self->socket_path);
  fflush(stdout);
}

static void markyd_app_teardown_socket(MarkydApp *self) {
  if (self->sock_fd >= 0) {
    close(self->sock_fd);
    self->sock_fd = -1;
  }
  if (self->socket_path) {
    unlink(self->socket_path);
  }
}

static gchar *read_stdin_content(void) {
  GIOChannel *channel;
  gchar *content = NULL;
  gsize length = 0;
  GError *error = NULL;

  /* Refuse to block waiting on a terminal — viewmd only reads stdin from pipes. */
  if (isatty(STDIN_FILENO)) {
    g_printerr("ViewMD: '-' requires piped input (stdin is a terminal)\n");
    return NULL;
  }

  channel = g_io_channel_unix_new(STDIN_FILENO);
  g_io_channel_set_encoding(channel, NULL, NULL); /* binary */
  if (g_io_channel_read_to_end(channel, &content, &length, &error) != G_IO_STATUS_NORMAL) {
    if (error) {
      g_printerr("ViewMD: failed to read stdin: %s\n", error->message);
      g_error_free(error);
    }
    g_free(content);
    content = NULL;
  }
  g_io_channel_unref(channel);
  return content;
}

gboolean markyd_app_open_file(MarkydApp *self, const gchar *path) {
  gchar *content = NULL;
  GError *error = NULL;
  gboolean from_stdin;

  if (!self || !self->editor || !path || path[0] == '\0') {
    return FALSE;
  }

  from_stdin = (g_strcmp0(path, "-") == 0);

  if (from_stdin) {
    content = read_stdin_content();
  } else {
    if (!g_file_get_contents(path, &content, NULL, &error)) {
      if (error) {
        g_printerr("Failed to load markdown file '%s': %s\n", path, error->message);
        g_error_free(error);
      }
      return FALSE;
    }
  }

  markyd_editor_set_content(self->editor, content ? content : "");
  g_free(content);

  if (!from_stdin) {
    gboolean path_changed = (g_strcmp0(self->current_file_path, path) != 0);
    g_free(self->current_file_path);
    self->current_file_path = g_strdup(path);

    /* Only restart monitor when the path actually changes; a toolbar
       reload on the same file must keep the existing monitor alive. */
    if (path_changed) {
      markyd_app_stop_watch(self);
      markyd_app_start_watch(self);
    }
  }

  markyd_app_update_window_title(self);
  return TRUE;
}

const gchar *markyd_app_get_current_path(MarkydApp *self) {
  if (!self) {
    return NULL;
  }
  return self->current_file_path;
}

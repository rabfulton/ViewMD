#include "config.h"
#include <errno.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

/* Global config instance */
MarkydConfig *config = NULL;

static gchar *config_path = NULL;

MarkydConfig *config_new(void) {
  MarkydConfig *cfg = g_new0(MarkydConfig, 1);

  /* Default values */
  cfg->window_x = -1;
  cfg->window_y = -1;
  cfg->window_width = 600;
  cfg->window_height = 700;
  cfg->window_maximized = FALSE;

  cfg->font_family = g_strdup("Cantarell");
  cfg->font_size = 16;
  cfg->theme = g_strdup("dark");

  /* Markdown accent defaults (match previous hardcoded colors) */
  cfg->h1_color = g_strdup("#61AFEF");
  cfg->h2_color = g_strdup("#C678DD");
  cfg->h3_color = g_strdup("#E5C07B");
  cfg->list_bullet_color = g_strdup("#61AFEF");

  cfg->line_numbers = FALSE;
  cfg->word_wrap = TRUE;

  return cfg;
}

void config_free(MarkydConfig *cfg) {
  if (!cfg)
    return;
  g_free(cfg->font_family);
  g_free(cfg->theme);
  g_free(cfg->h1_color);
  g_free(cfg->h2_color);
  g_free(cfg->h3_color);
  g_free(cfg->list_bullet_color);
  g_free(cfg);
}

const gchar *config_get_path(void) {
  if (!config_path) {
    const gchar *config_dir = g_get_user_config_dir();
    gchar *new_app_dir = g_build_filename(config_dir, "viewmd", NULL);

    g_mkdir_with_parents(new_app_dir, 0755);
    config_path = g_build_filename(new_app_dir, "config.ini", NULL);

    g_free(new_app_dir);
  }
  return config_path;
}

gboolean config_load(MarkydConfig *cfg) {
  GKeyFile *keyfile;
  GError *error = NULL;
  const gchar *path;

  path = config_get_path();
  keyfile = g_key_file_new();

  if (!g_key_file_load_from_file(keyfile, path, G_KEY_FILE_NONE, &error)) {
    /* File doesn't exist or is invalid - use defaults */
    g_clear_error(&error);
    g_key_file_free(keyfile);
    return FALSE;
  }

  /* Window geometry */
  if (g_key_file_has_key(keyfile, "Window", "x", NULL))
    cfg->window_x = g_key_file_get_integer(keyfile, "Window", "x", NULL);
  if (g_key_file_has_key(keyfile, "Window", "y", NULL))
    cfg->window_y = g_key_file_get_integer(keyfile, "Window", "y", NULL);
  if (g_key_file_has_key(keyfile, "Window", "width", NULL))
    cfg->window_width =
        g_key_file_get_integer(keyfile, "Window", "width", NULL);
  if (g_key_file_has_key(keyfile, "Window", "height", NULL))
    cfg->window_height =
        g_key_file_get_integer(keyfile, "Window", "height", NULL);
  if (g_key_file_has_key(keyfile, "Window", "maximized", NULL))
    cfg->window_maximized =
        g_key_file_get_boolean(keyfile, "Window", "maximized", NULL);

  /* Appearance */
  if (g_key_file_has_key(keyfile, "Appearance", "font_family", NULL)) {
    g_free(cfg->font_family);
    cfg->font_family =
        g_key_file_get_string(keyfile, "Appearance", "font_family", NULL);
  }
  if (g_key_file_has_key(keyfile, "Appearance", "font_size", NULL))
    cfg->font_size =
        g_key_file_get_integer(keyfile, "Appearance", "font_size", NULL);
  if (g_key_file_has_key(keyfile, "Appearance", "theme", NULL)) {
    g_free(cfg->theme);
    cfg->theme = g_key_file_get_string(keyfile, "Appearance", "theme", NULL);
  }

  /* Markdown accents */
  if (g_key_file_has_key(keyfile, "Markdown", "h1_color", NULL)) {
    g_free(cfg->h1_color);
    cfg->h1_color =
        g_key_file_get_string(keyfile, "Markdown", "h1_color", NULL);
  }
  if (g_key_file_has_key(keyfile, "Markdown", "h2_color", NULL)) {
    g_free(cfg->h2_color);
    cfg->h2_color =
        g_key_file_get_string(keyfile, "Markdown", "h2_color", NULL);
  }
  if (g_key_file_has_key(keyfile, "Markdown", "h3_color", NULL)) {
    g_free(cfg->h3_color);
    cfg->h3_color =
        g_key_file_get_string(keyfile, "Markdown", "h3_color", NULL);
  }
  if (g_key_file_has_key(keyfile, "Markdown", "list_bullet_color", NULL)) {
    g_free(cfg->list_bullet_color);
    cfg->list_bullet_color = g_key_file_get_string(
        keyfile, "Markdown", "list_bullet_color", NULL);
  }

  /* Editor */
  if (g_key_file_has_key(keyfile, "Editor", "word_wrap", NULL))
    cfg->word_wrap =
        g_key_file_get_boolean(keyfile, "Editor", "word_wrap", NULL);

  g_key_file_free(keyfile);
  return TRUE;
}

gboolean config_save(MarkydConfig *cfg) {
  GKeyFile *keyfile;
  GError *error = NULL;
  const gchar *path;
  gchar *data;
  gsize length;

  keyfile = g_key_file_new();
  path = config_get_path();

  /* Window geometry */
  g_key_file_set_integer(keyfile, "Window", "x", cfg->window_x);
  g_key_file_set_integer(keyfile, "Window", "y", cfg->window_y);
  g_key_file_set_integer(keyfile, "Window", "width", cfg->window_width);
  g_key_file_set_integer(keyfile, "Window", "height", cfg->window_height);
  g_key_file_set_boolean(keyfile, "Window", "maximized", cfg->window_maximized);

  /* Appearance */
  g_key_file_set_string(keyfile, "Appearance", "font_family", cfg->font_family);
  g_key_file_set_integer(keyfile, "Appearance", "font_size", cfg->font_size);
  g_key_file_set_string(keyfile, "Appearance", "theme", cfg->theme);

  /* Markdown accents */
  g_key_file_set_string(keyfile, "Markdown", "h1_color", cfg->h1_color);
  g_key_file_set_string(keyfile, "Markdown", "h2_color", cfg->h2_color);
  g_key_file_set_string(keyfile, "Markdown", "h3_color", cfg->h3_color);
  g_key_file_set_string(keyfile, "Markdown", "list_bullet_color",
                        cfg->list_bullet_color);

  /* Editor */
  g_key_file_set_boolean(keyfile, "Editor", "word_wrap", cfg->word_wrap);

  data = g_key_file_to_data(keyfile, &length, &error);
  if (error) {
    g_printerr("Failed to serialize config: %s\n", error->message);
    g_error_free(error);
    g_key_file_free(keyfile);
    return FALSE;
  }

  if (!g_file_set_contents(path, data, length, &error)) {
    g_printerr("Failed to save config: %s\n", error->message);
    g_error_free(error);
    g_free(data);
    g_key_file_free(keyfile);
    return FALSE;
  }

  g_free(data);
  g_key_file_free(keyfile);
  return TRUE;
}

#ifndef MARKYD_CONFIG_H
#define MARKYD_CONFIG_H

#include <glib.h>

/* Settings structure */
typedef struct _MarkydConfig {
  /* Window geometry */
  gint window_x;
  gint window_y;
  gint window_width;
  gint window_height;
  gboolean window_maximized;

  /* Appearance */
  gchar *font_family;
  gint font_size; /* in points */
  gchar *theme;   /* "dark", "light", "system" */

  /* Markdown accent colors */
  gchar *h1_color;
  gchar *h2_color;
  gchar *h3_color;
  gchar *list_bullet_color;

  /* Editor */
  gboolean line_numbers;
  gboolean word_wrap;
} MarkydConfig;

/* Global config instance */
extern MarkydConfig *config;

/* Lifecycle */
MarkydConfig *config_new(void);
void config_free(MarkydConfig *cfg);

/* Load/Save */
gboolean config_load(MarkydConfig *cfg);
gboolean config_save(MarkydConfig *cfg);

/* Get config file path */
const gchar *config_get_path(void);

#endif /* MARKYD_CONFIG_H */

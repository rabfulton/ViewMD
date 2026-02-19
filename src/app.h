#ifndef MARKYD_APP_H
#define MARKYD_APP_H

#include <gtk/gtk.h>

/* Forward declarations */
typedef struct _MarkydWindow MarkydWindow;
typedef struct _MarkydEditor MarkydEditor;

/* Application state */
typedef struct _MarkydApp {
  GtkApplication *gtk_app;
  MarkydWindow *window;
  MarkydEditor *editor;
  gchar *current_file_path;
} MarkydApp;

/* Global app instance */
extern MarkydApp *app;

/* Lifecycle */
MarkydApp *markyd_app_new(void);
void markyd_app_free(MarkydApp *app);
int markyd_app_run(MarkydApp *app, int argc, char **argv);

/* Document management */
gboolean markyd_app_open_file(MarkydApp *app, const gchar *path);

/* Utility */
const gchar *markyd_app_get_current_path(MarkydApp *app);

#endif /* MARKYD_APP_H */

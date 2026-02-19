#include "app.h"
#include <gtk/gtk.h>

int main(int argc, char **argv) {
  MarkydApp *application;
  int status;

  application = markyd_app_new();
  if (!application) {
    g_printerr("Failed to create application\n");
    return 1;
  }

  status = markyd_app_run(application, argc, argv);

  markyd_app_free(application);

  return status;
}

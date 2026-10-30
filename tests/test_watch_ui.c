#include "app.h"
#include "config.h"
#include "editor.h"
#include "window.h"
#include <glib/gstdio.h>
#include <string.h>

typedef struct {
  MarkydApp *application;
  gchar *directory;
  gchar *path;
} Fixture;

static GtkApplication *test_application;

static void pump(guint milliseconds) {
  gint64 until = g_get_monotonic_time() + milliseconds * 1000;
  do {
    while (g_main_context_iteration(NULL, FALSE)) {}
    g_usleep(1000);
  } while (g_get_monotonic_time() < until);
}

static void setup(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  g_remove(config_get_path());
  fixture->application = markyd_app_new();
  /* GTK expects one registered application per process. Share it across
   * fixtures, while each fixture owns fresh ViewMD state and a fresh window. */
  g_object_unref(fixture->application->gtk_app);
  fixture->application->gtk_app = g_object_ref(test_application);
  fixture->application->window = markyd_window_new(fixture->application);
  fixture->application->editor = fixture->application->window->editor;
  fixture->directory = g_dir_make_tmp("viewmd-watch-ui-XXXXXX", NULL);
  fixture->path = g_build_filename(fixture->directory, "document.md", NULL);
  GString *content = g_string_new(NULL);
  for (guint i = 0; i < 200; i++) {
    g_string_append_printf(content, "Paragraph %u: some text to read.\n\n", i);
  }
  g_assert_true(g_file_set_contents(fixture->path, content->str, -1, NULL));
  g_string_free(content, TRUE);
  g_assert_true(markyd_app_open_file(fixture->application, fixture->path));
  pump(250);
}

static void teardown(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  if (fixture->application) {
    gtk_widget_destroy(fixture->application->window->window);
    markyd_app_free(fixture->application);
  }
  g_remove(fixture->path);
  g_rmdir(fixture->directory);
  g_free(fixture->path);
  g_free(fixture->directory);
}

static void test_scroll_position(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  MarkydEditor *editor = fixture->application->editor;
  GtkAdjustment *vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(editor->text_view));
  g_assert_cmpfloat(gtk_adjustment_get_upper(vadj), >,
                   gtk_adjustment_get_page_size(vadj) + 400);
  gtk_adjustment_set_value(vadj, 400);
  pump(50);
  gdouble position = gtk_adjustment_get_value(vadj);
  gchar *changed = g_strconcat(editor->source_content, "A new paragraph.\n", NULL);
  g_assert_true(g_file_set_contents(fixture->path, changed, -1, NULL));
  pump(450);
  g_assert_cmpstr(editor->source_content, ==, changed);
  g_assert_cmpfloat_with_epsilon(gtk_adjustment_get_value(vadj), position, 2.0);
  g_assert_cmpuint(editor->scroll_restore_id, ==, 0);
  g_free(changed);
}

static GtkWidget *find_toggle(GtkWidget *widget) {
  if (GTK_IS_CHECK_BUTTON(widget) &&
      g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)),
                "Automatically reload changed files") == 0) {
    return widget;
  }
  if (!GTK_IS_CONTAINER(widget))
    return NULL;
  GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
  GtkWidget *result = NULL;
  for (GList *item = children; item && !result; item = item->next)
    result = find_toggle(item->data);
  g_list_free(children);
  return result;
}

typedef struct {
  gboolean enabled;
  gint response;
} DialogResponse;

static gboolean respond_to_settings(gpointer data) {
  DialogResponse *response = data;
  GList *windows = gtk_window_list_toplevels();
  gboolean found = FALSE;
  for (GList *item = windows; item; item = item->next) {
    if (GTK_IS_DIALOG(item->data) &&
        g_strcmp0(gtk_window_get_title(item->data), "ViewMD Settings") == 0) {
      GtkWidget *toggle = find_toggle(item->data);
      g_assert_nonnull(toggle);
      gboolean previous = config->auto_reload;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle), response->enabled);
      g_assert_cmpint(config->auto_reload, ==, previous);
      gtk_dialog_response(GTK_DIALOG(item->data), response->response);
      found = TRUE;
      break;
    }
  }
  g_list_free(windows);
  g_assert_true(found);
  return G_SOURCE_REMOVE;
}

static void change_setting(Fixture *fixture, gboolean enabled, gint response) {
  DialogResponse action = {enabled, response};
  g_timeout_add(50, respond_to_settings, &action);
  g_signal_emit_by_name(fixture->application->window->btn_settings, "clicked");
}

static void test_settings(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  change_setting(fixture, FALSE, GTK_RESPONSE_CANCEL);
  g_assert_true(config->auto_reload);
  g_assert_nonnull(fixture->application->file_monitor);
  change_setting(fixture, FALSE, GTK_RESPONSE_APPLY);
  g_assert_false(config->auto_reload);
  g_assert_null(fixture->application->file_monitor);
  MarkydConfig *saved = config_new();
  g_assert_true(config_load(saved));
  g_assert_false(saved->auto_reload);
  config_free(saved);
  change_setting(fixture, TRUE, GTK_RESPONSE_APPLY);
  g_assert_true(config->auto_reload);
  g_assert_nonnull(fixture->application->file_monitor);
}

static void test_pending_restore_cleanup(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  MarkydEditor *editor = fixture->application->editor;
  gchar *changed = g_strconcat(editor->source_content, "Changed", NULL);
  markyd_editor_reload_content(editor, changed);
  g_free(changed);
  while (editor->markdown_idle_id)
    g_main_context_iteration(NULL, FALSE);
  g_assert_cmpuint(editor->scroll_restore_id, !=, 0);
  gtk_widget_destroy(fixture->application->window->window);
  markyd_app_free(fixture->application);
  fixture->application = NULL;
  pump(200);
}

int main(int argc, char **argv) {
  gchar *config_directory = g_dir_make_tmp("viewmd-watch-ui-config-XXXXXX", NULL);
  g_setenv("XDG_CONFIG_HOME", config_directory, TRUE);
  gtk_init(&argc, &argv);
  g_test_init(&argc, &argv, NULL);
  test_application = gtk_application_new("org.viewmd.watchtests", G_APPLICATION_NON_UNIQUE);
  GError *error = NULL;
  g_assert_true(g_application_register(G_APPLICATION(test_application), NULL, &error));
  g_assert_no_error(error);
#define ADD_TEST(name, function) \
  g_test_add("/watch-ui/" name, Fixture, NULL, setup, function, teardown)
  ADD_TEST("scroll-position", test_scroll_position);
  ADD_TEST("settings-apply-cancel", test_settings);
  ADD_TEST("pending-restore-cleanup", test_pending_restore_cleanup);
  gint result = g_test_run();
  g_object_unref(test_application);
  g_remove(config_get_path());
  gchar *app_directory = g_path_get_dirname(config_get_path());
  g_rmdir(app_directory);
  g_free(app_directory);
  g_rmdir(config_directory);
  g_free(config_directory);
  return result;
}

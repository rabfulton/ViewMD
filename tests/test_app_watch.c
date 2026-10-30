#include "app.h"
#include "config.h"
#include "editor.h"
#include "window.h"
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

/* Exercise the real application and filesystem monitors without a display.
 * Only the view is replaced; rendering has its own tests. */
static guint reload_count;
MarkydWindow *markyd_window_new(MarkydApp *application) {
  (void)application;
  g_assert_not_reached();
}
void markyd_window_free(MarkydWindow *window) { (void)window; }
void markyd_window_show(MarkydWindow *window) { (void)window; }
void markyd_editor_set_content(MarkydEditor *editor, const gchar *content) {
  g_free(editor->source_content);
  editor->source_content = g_utf8_make_valid(content, -1);
}
void markyd_editor_reload_content(MarkydEditor *editor, const gchar *content) {
  reload_count++;
  markyd_editor_set_content(editor, content);
}

typedef struct {
  MarkydApp *application;
  MarkydEditor *editor;
  gchar *directory;
  gchar *path;
} Fixture;

static void pump(guint milliseconds) {
  gint64 until = g_get_monotonic_time() + milliseconds * 1000;
  do {
    while (g_main_context_iteration(NULL, FALSE)) {}
    g_usleep(1000);
  } while (g_get_monotonic_time() < until);
}

static void wait_for_content(Fixture *fixture, const gchar *expected) {
  gint64 until = g_get_monotonic_time() + 2500000;
  while (g_strcmp0(fixture->editor->source_content, expected) != 0 &&
         g_get_monotonic_time() < until) {
    pump(5);
  }
  g_assert_cmpstr(fixture->editor->source_content, ==, expected);
}

static void write_file(const gchar *path, const gchar *content) {
  GError *error = NULL;
  g_assert_true(g_file_set_contents(path, content, -1, &error));
  g_assert_no_error(error);
}

static void write_in_place(const gchar *path, const gchar *content) {
  gint fd = g_open(path, O_WRONLY | O_TRUNC, 0600);
  g_assert_cmpint(fd, >=, 0);
  g_assert_cmpint(write(fd, content, strlen(content)), ==, (gssize)strlen(content));
  g_assert_cmpint(close(fd), ==, 0);
}

static void setup(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  g_remove(config_get_path());
  fixture->application = markyd_app_new();
  fixture->editor = g_new0(MarkydEditor, 1);
  fixture->application->editor = fixture->editor;
  fixture->directory = g_dir_make_tmp("viewmd-watch-test-XXXXXX", NULL);
  g_assert_nonnull(fixture->directory);
  fixture->path = g_build_filename(fixture->directory, "document.md", NULL);
  write_file(fixture->path, "Original");
  reload_count = 0;
}

static void teardown(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  if (fixture->application)
    markyd_app_free(fixture->application);
  g_free(fixture->editor->source_content);
  g_free(fixture->editor);
  GDir *directory = g_dir_open(fixture->directory, 0, NULL);
  const gchar *name;
  while ((name = g_dir_read_name(directory))) {
    gchar *path = g_build_filename(fixture->directory, name, NULL);
    g_assert_cmpint(g_remove(path), ==, 0);
    g_free(path);
  }
  g_dir_close(directory);
  g_assert_cmpint(g_rmdir(fixture->directory), ==, 0);
  g_free(fixture->directory);
  g_free(fixture->path);
}

static void open_document(Fixture *fixture) {
  g_assert_true(markyd_app_open_file(fixture->application, fixture->path));
  g_assert_nonnull(fixture->application->file_monitor);
}

static void test_preference(Fixture *fixture, gconstpointer unused) {
  (void)fixture; (void)unused;
  g_assert_true(config->auto_reload);
  config->auto_reload = FALSE;
  g_assert_true(config_save(config));
  MarkydConfig *loaded = config_new();
  g_assert_true(config_load(loaded));
  g_assert_false(loaded->auto_reload);
  config_free(loaded);
  config->auto_reload = TRUE;
  g_assert_true(config_save(config));
  loaded = config_new();
  g_assert_true(config_load(loaded));
  g_assert_true(loaded->auto_reload);
  config_free(loaded);

  write_file(config_get_path(), "[Appearance]\ntheme=light\n");
  loaded = config_new();
  g_assert_true(config_load(loaded));
  g_assert_true(loaded->auto_reload);
  config_free(loaded);
  write_file(config_get_path(), "[Editor]\nauto_reload=invalid\n");
  loaded = config_new();
  g_assert_true(config_load(loaded));
  g_assert_true(loaded->auto_reload);
  config_free(loaded);
}

static void test_in_place_saves(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  open_document(fixture);
  write_in_place(fixture->path, "First");
  write_in_place(fixture->path, "Second");
  write_in_place(fixture->path, "Final");
  wait_for_content(fixture, "Final");
  g_assert_cmpuint(reload_count, ==, 1);
  write_in_place(fixture->path, "Next save");
  wait_for_content(fixture, "Next save");
}

static void replace_file(Fixture *fixture, const gchar *content) {
  gchar *temporary = g_build_filename(fixture->directory, ".save.tmp", NULL);
  write_file(temporary, content);
  g_assert_cmpint(g_rename(temporary, fixture->path), ==, 0);
  g_free(temporary);
}

static void test_atomic_saves(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  open_document(fixture);
  replace_file(fixture, "Replacement");
  wait_for_content(fixture, "Replacement");
  replace_file(fixture, "Another replacement");
  wait_for_content(fixture, "Another replacement");
}

static void test_delete_recreate(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  open_document(fixture);
  g_assert_cmpint(g_remove(fixture->path), ==, 0);
  pump(350);
  g_assert_cmpstr(fixture->editor->source_content, ==, "Original");
  write_file(fixture->path, "Recreated");
  wait_for_content(fixture, "Recreated");
  write_in_place(fixture->path, "");
  wait_for_content(fixture, "");
}

static void test_switch_files(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  open_document(fixture);
  write_in_place(fixture->path, "Old pending change");
  pump(20);
  gchar *other = g_build_filename(fixture->directory, "other.md", NULL);
  write_file(other, "Other document");
  g_assert_true(markyd_app_open_file(fixture->application, other));
  write_in_place(fixture->path, "Old later change");
  pump(350);
  g_assert_cmpstr(fixture->editor->source_content, ==, "Other document");
  g_assert_cmpuint(reload_count, ==, 0);
  write_in_place(other, "New document changed");
  wait_for_content(fixture, "New document changed");
  g_free(other);
}

static void test_disable_reenable(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  open_document(fixture);
  config->auto_reload = FALSE;
  markyd_app_update_file_watch(fixture->application);
  g_assert_null(fixture->application->file_monitor);
  g_assert_cmpuint(fixture->application->watch_reload_id, ==, 0);
  write_in_place(fixture->path, "Changed while disabled");
  pump(350);
  g_assert_cmpstr(fixture->editor->source_content, ==, "Original");
  config->auto_reload = TRUE;
  markyd_app_update_file_watch(fixture->application);
  wait_for_content(fixture, "Changed while disabled");
  write_in_place(fixture->path, "Pending when disabled");
  pump(20);
  config->auto_reload = FALSE;
  markyd_app_update_file_watch(fixture->application);
  pump(350);
  g_assert_cmpstr(fixture->editor->source_content, ==, "Changed while disabled");
  g_assert_true(markyd_app_open_file(fixture->application, fixture->path));
  g_assert_cmpstr(fixture->editor->source_content, ==, "Pending when disabled");
  g_assert_null(fixture->application->file_monitor);
}

static void test_ignore_unchanged(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  open_document(fixture);
  gchar *other = g_build_filename(fixture->directory, "unrelated.md", NULL);
  write_file(other, "Unrelated");
  write_in_place(fixture->path, "Original");
  pump(350);
  g_assert_cmpuint(reload_count, ==, 0);
  g_free(other);
}

static void test_manual_reload(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  open_document(fixture);
  GFileMonitor *monitor = g_object_ref(fixture->application->file_monitor);
  const gchar *path = markyd_app_get_current_path(fixture->application);
  g_assert_true(markyd_app_open_file(fixture->application, path));
  g_assert_cmpstr(fixture->application->current_file_path, ==, fixture->path);
  g_assert_true(fixture->application->file_monitor == monitor);
  gchar *missing = g_build_filename(fixture->directory, "missing.md", NULL);
  g_assert_false(markyd_app_open_file(fixture->application, missing));
  g_assert_cmpstr(fixture->application->current_file_path, ==, fixture->path);
  g_assert_true(fixture->application->file_monitor == monitor);
  write_in_place(fixture->path, "Still watched");
  wait_for_content(fixture, "Still watched");
  g_free(missing);
  g_object_unref(monitor);
}

static void test_cleanup_pending(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  open_document(fixture);
  write_in_place(fixture->path, "Pending");
  pump(20);
  markyd_app_free(fixture->application);
  fixture->application = NULL;
  pump(350);
  g_assert_cmpuint(reload_count, ==, 0);
}

int main(int argc, char **argv) {
  gchar *config_directory = g_dir_make_tmp("viewmd-watch-config-XXXXXX", NULL);
  g_assert_nonnull(config_directory);
  g_setenv("XDG_CONFIG_HOME", config_directory, TRUE);
  g_test_init(&argc, &argv, NULL);
#define ADD_TEST(name, function) \
  g_test_add("/watch/" name, Fixture, NULL, setup, function, teardown)
  ADD_TEST("preference", test_preference);
  ADD_TEST("in-place-saves", test_in_place_saves);
  ADD_TEST("atomic-saves", test_atomic_saves);
  ADD_TEST("delete-recreate", test_delete_recreate);
  ADD_TEST("switch-files", test_switch_files);
  ADD_TEST("disable-reenable", test_disable_reenable);
  ADD_TEST("ignore-unchanged", test_ignore_unchanged);
  ADD_TEST("manual-reload", test_manual_reload);
  ADD_TEST("cleanup-pending", test_cleanup_pending);
  gint result = g_test_run();
  g_remove(config_get_path());
  gchar *app_directory = g_path_get_dirname(config_get_path());
  g_rmdir(app_directory);
  g_free(app_directory);
  g_rmdir(config_directory);
  g_free(config_directory);
  return result;
}

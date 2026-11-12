#include "app.h"
#include "config.h"
#include "editor.h"
#include "markdown.h"
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

static GtkWidget *find_toggle(GtkWidget *widget, const gchar *label) {
  if (GTK_IS_CHECK_BUTTON(widget) &&
      g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), label) == 0) {
    return widget;
  }
  if (!GTK_IS_CONTAINER(widget))
    return NULL;
  GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
  GtkWidget *result = NULL;
  for (GList *item = children; item && !result; item = item->next)
    result = find_toggle(item->data, label);
  g_list_free(children);
  return result;
}

typedef struct {
  const gchar *label;
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
      GtkWidget *toggle = find_toggle(item->data, response->label);
      g_assert_nonnull(toggle);
      gboolean previous = config->auto_reload;
      gboolean previous_front_matter = config->hide_front_matter;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle), response->enabled);
      g_assert_cmpint(config->auto_reload, ==, previous);
      g_assert_cmpint(config->hide_front_matter, ==, previous_front_matter);
      gtk_dialog_response(GTK_DIALOG(item->data), response->response);
      found = TRUE;
      break;
    }
  }
  g_list_free(windows);
  g_assert_true(found);
  return G_SOURCE_REMOVE;
}

static void change_setting(Fixture *fixture, const gchar *label,
                           gboolean enabled, gint response) {
  DialogResponse action = {label, enabled, response};
  g_timeout_add(50, respond_to_settings, &action);
  g_signal_emit_by_name(fixture->application->window->btn_settings, "clicked");
}

static void test_settings(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  change_setting(fixture, "Automatically reload changed files", FALSE,
                  GTK_RESPONSE_CANCEL);
  g_assert_true(config->auto_reload);
  g_assert_nonnull(fixture->application->file_monitor);
  change_setting(fixture, "Automatically reload changed files", FALSE,
                  GTK_RESPONSE_APPLY);
  g_assert_false(config->auto_reload);
  g_assert_null(fixture->application->file_monitor);
  MarkydConfig *saved = config_new();
  g_assert_true(config_load(saved));
  g_assert_false(saved->auto_reload);
  config_free(saved);
  change_setting(fixture, "Automatically reload changed files", TRUE,
                  GTK_RESPONSE_APPLY);
  g_assert_true(config->auto_reload);
  g_assert_nonnull(fixture->application->file_monitor);
}

static void assert_front_matter_hidden(MarkydEditor *editor, gboolean hidden) {
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds(editor->buffer, &start, &end);
  gchar *text = gtk_text_buffer_get_text(editor->buffer, &start, &end, FALSE);
  g_assert_nonnull(strstr(text, "Body"));
  g_assert_cmpint(strstr(text, "title: Metadata") == NULL, ==, hidden);
  g_free(text);
}

static void test_front_matter_settings(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  MarkydEditor *editor = fixture->application->editor;
  const gchar *source = "---\ntitle: Metadata\n---\n# Body\n";
  markyd_editor_set_content(editor, source);
  pump(50);
  g_assert_true(config->hide_front_matter);
  assert_front_matter_hidden(editor, TRUE);
  change_setting(fixture, "Hide YAML front matter", FALSE, GTK_RESPONSE_CANCEL);
  pump(50);
  g_assert_true(config->hide_front_matter);
  assert_front_matter_hidden(editor, TRUE);
  change_setting(fixture, "Hide YAML front matter", FALSE, GTK_RESPONSE_APPLY);
  pump(50);
  g_assert_false(config->hide_front_matter);
  assert_front_matter_hidden(editor, FALSE);
  MarkydConfig *saved = config_new();
  g_assert_true(config_load(saved));
  g_assert_false(saved->hide_front_matter);
  config_free(saved);
  change_setting(fixture, "Hide YAML front matter", TRUE, GTK_RESPONSE_APPLY);
  pump(50);
  g_assert_true(config->hide_front_matter);
  assert_front_matter_hidden(editor, TRUE);
  g_assert_cmpstr(editor->source_content, ==, source);
  g_assert_true(config->auto_reload);
  g_assert_nonnull(fixture->application->file_monitor);
}

static GtkWidget *find_footnote_label(GtkWidget *widget) {
  if (GTK_IS_LABEL(widget) && strstr(gtk_label_get_label(GTK_LABEL(widget)),
                                    "viewmd-footnote:note-1") &&
      g_strcmp0(gtk_label_get_text(GTK_LABEL(widget)), "Cell[1] and [^a]") == 0)
    return widget;
  if (!GTK_IS_CONTAINER(widget))
    return NULL;
  GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
  GtkWidget *found = NULL;
  for (GList *item = children; item && !found; item = item->next)
    found = find_footnote_label(item->data);
  g_list_free(children);
  return found;
}

static void test_footnote_navigation(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  MarkydEditor *editor = fixture->application->editor;
  GString *source = g_string_new("Intro[^a].\n\n| Table |\n|---|\n"
                                 "| Cell[^a] and `[^a]` |\n");
  for (guint i = 1; i < 40; i++)
    g_string_append_printf(source, "| Row %u%s |\n", i, i == 35 ? "[^a]" : "");
  g_string_append_c(source, '\n');
  for (guint i = 0; i < 100; i++)
    g_string_append_printf(source, "Paragraph %u.\n\n", i);
  g_string_append(source, "[^a]: A **formatted** note.\n");
  markyd_editor_set_content(editor, source->str);
  pump(150);
  GtkWidget *label = find_footnote_label(editor->text_view);
  g_assert_nonnull(label);
  g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(label)), ==, "Cell[1] and [^a]");
  PangoAttrList *attributes = pango_layout_get_attributes(gtk_label_get_layout(GTK_LABEL(label)));
  PangoAttrIterator *iterator = pango_attr_list_get_iterator(attributes);
  do {
    gint start, end;
    pango_attr_iterator_range(iterator, &start, &end);
    if (start <= 4 && end > 4) {
      PangoAttribute *underline = pango_attr_iterator_get(iterator, PANGO_ATTR_UNDERLINE);
      g_assert_nonnull(underline);
      g_assert_cmpint(((PangoAttrInt *)underline)->value, ==, PANGO_UNDERLINE_NONE);
      break;
    }
  } while (pango_attr_iterator_next(iterator));
  pango_attr_iterator_destroy(iterator);
  gboolean handled = FALSE;
  g_signal_emit_by_name(label, "activate-link", "viewmd-footnote:note-1", &handled);
  g_assert_true(handled);
  pump(150);
  GtkTextIter at, expected;
  gtk_text_buffer_get_iter_at_mark(editor->buffer, &at,
                                  gtk_text_buffer_get_insert(editor->buffer));
  gtk_text_buffer_get_iter_at_mark(editor->buffer, &expected,
      gtk_text_buffer_get_mark(editor->buffer, "viewmd-footnote:note-1"));
  g_assert_cmpint(gtk_text_iter_get_offset(&at), ==, gtk_text_iter_get_offset(&expected));
  GtkAdjustment *vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(editor->text_view));
  g_assert_cmpfloat(gtk_adjustment_get_value(vadj), >, 500);
  g_assert_true(markyd_editor_follow_footnote(editor, "viewmd-footnote:ref-1-2"));
  pump(300);
  gtk_text_buffer_get_iter_at_mark(editor->buffer, &at,
                                  gtk_text_buffer_get_insert(editor->buffer));
  gtk_text_buffer_get_iter_at_mark(editor->buffer, &expected,
      gtk_text_buffer_get_mark(editor->buffer, "viewmd-footnote:ref-1-2"));
  g_assert_cmpint(gtk_text_iter_get_offset(&at), ==, gtk_text_iter_get_offset(&expected));
  g_assert_cmpfloat(gtk_adjustment_get_value(vadj), <, 200);
  g_assert_true(markyd_editor_follow_footnote(editor, "viewmd-footnote:ref-1-3"));
  pump(300);
  g_assert_cmpfloat(gtk_adjustment_get_value(vadj), >, 500);
  g_assert_cmpfloat(gtk_adjustment_get_value(vadj), <, 2000);
  /* Hidden table search text must contain display text, not markup or URIs. */
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds(editor->buffer, &start, &end);
  gchar *text = gtk_text_buffer_get_text(editor->buffer, &start, &end, TRUE);
  g_assert_nonnull(strstr(text, "Cell[1] and [^a]"));
  g_assert_null(strstr(text, "<a href"));
  g_assert_null(strstr(text, "viewmd-footnote:"));
  g_assert_cmpstr(editor->source_content, ==, source->str);
  g_free(text);
  g_string_free(source, TRUE);
}

static void test_footnote_superscript_zoom(Fixture *fixture, gconstpointer unused) {
  (void)unused;
  MarkydEditor *editor = fixture->application->editor;
  markyd_editor_set_content(editor, "Text[^source].\n\n[^source]: Note.\n");
  pump(50);
  GtkTextTag *tag = gtk_text_tag_table_lookup(
      gtk_text_buffer_get_tag_table(editor->buffer), VIEWMD_FOOTNOTE_REFERENCE_TAG);
  gint small_rise, large_rise;
  config->font_size = 12;
  markyd_window_apply_css(fixture->application->window);
  pump(50);
  g_object_get(tag, "rise", &small_rise, NULL);
  config->font_size = 24;
  markyd_window_apply_css(fixture->application->window);
  pump(50);
  g_object_get(tag, "rise", &large_rise, NULL);
  g_assert_cmpint(small_rise, >, 0);
  g_assert_cmpint(large_rise, >, small_rise * 1.5);
  GtkTextIter at;
  gtk_text_buffer_get_iter_at_offset(editor->buffer, &at, 4);
  g_assert_true(gtk_text_iter_has_tag(&at, tag));
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
  ADD_TEST("front-matter-settings", test_front_matter_settings);
  ADD_TEST("footnote-navigation", test_footnote_navigation);
  ADD_TEST("footnote-superscript-zoom", test_footnote_superscript_zoom);
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

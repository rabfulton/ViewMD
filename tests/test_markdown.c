#include "config.h"
#include "markdown.h"
#include <string.h>

static GtkTextBuffer *new_buffer(void) {
  GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
  markdown_init_tags(buffer);
  return buffer;
}

static gchar *buffer_text(GtkTextBuffer *buffer) {
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static void assert_text(GtkTextBuffer *buffer, const gchar *expected) {
  gchar *text = buffer_text(buffer);
  g_assert_cmpstr(text, ==, expected);
  g_free(text);
}

static void test_reload_link_tags(void) {
  GtkTextBuffer *buffer = new_buffer();
  GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
  gint baseline = gtk_text_tag_table_get_size(table);

  for (guint i = 0; i < 50; i++) {
    markdown_apply_tags(buffer, "[One](https://one.example) [Two](#two)");
    g_assert_cmpint(gtk_text_tag_table_get_size(table), ==, baseline + 2);
    GtkTextIter at;
    gtk_text_buffer_get_start_iter(buffer, &at);
    GSList *tags = gtk_text_iter_get_tags(&at);
    gboolean found_url = FALSE;
    for (GSList *item = tags; item; item = item->next) {
      const gchar *url = g_object_get_data(G_OBJECT(item->data),
                                           VIEWMD_LINK_URL_DATA);
      if (g_strcmp0(url, "https://one.example") == 0) {
        found_url = TRUE;
      }
    }
    g_assert_true(found_url);
    g_slist_free(tags);
  }

  markdown_apply_tags(buffer, "No links");
  g_assert_cmpint(gtk_text_tag_table_get_size(table), ==, baseline);
  g_object_unref(buffer);
}

static void test_reload_heading_anchors(void) {
  GtkTextBuffer *buffer = new_buffer();
  markdown_apply_tags(buffer, "# Removed\n\n# Kept\n\n# Kept\n");
  g_assert_nonnull(gtk_text_buffer_get_mark(buffer, "viewmd-anchor-kept-1"));

  for (guint i = 0; i < 3; i++) {
    markdown_apply_tags(buffer, "Intro\n\n# Kept\n");
    g_assert_null(gtk_text_buffer_get_mark(buffer, "viewmd-anchor-removed"));
    g_assert_null(gtk_text_buffer_get_mark(buffer, "viewmd-anchor-kept-1"));
    GtkTextMark *mark = gtk_text_buffer_get_mark(buffer, "viewmd-anchor-kept");
    g_assert_nonnull(mark);
    GtkTextIter at;
    gtk_text_buffer_get_iter_at_mark(buffer, &at, mark);
    g_assert_cmpint(gtk_text_iter_get_offset(&at), ==, 7);
  }

  markdown_apply_tags(buffer, NULL);
  g_assert_null(gtk_text_buffer_get_mark(buffer, "viewmd-anchor-kept"));
  g_object_unref(buffer);
}

static void test_reload_preserves_other_state(void) {
  GtkTextBuffer *buffer = new_buffer();
  GtkTextTag *custom = gtk_text_buffer_create_tag(buffer, "custom", NULL);
  GtkTextTag *anonymous = gtk_text_buffer_create_tag(buffer, NULL, NULL);
  g_object_ref(anonymous);
  GtkTextIter at;
  gtk_text_buffer_get_start_iter(buffer, &at);
  GtkTextMark *bookmark = gtk_text_buffer_create_mark(buffer, "bookmark", &at, TRUE);

  markdown_apply_tags(buffer, "# Heading\n\n[link](https://example.com)");
  markdown_apply_tags(buffer, "# Heading\n\n[link](https://other.example)");
  GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
  g_assert_true(gtk_text_tag_table_lookup(table, "custom") == custom);
  g_assert_true(gtk_text_buffer_get_mark(buffer, "bookmark") == bookmark);
  GtkTextIter end;
  gtk_text_buffer_get_bounds(buffer, &at, &end);
  gtk_text_buffer_apply_tag(buffer, anonymous, &at, &end);
  g_assert_true(gtk_text_iter_has_tag(&at, anonymous));
  g_object_unref(anonymous);
  g_object_unref(buffer);
}

static void test_reload_buffers_independent(void) {
  GtkTextBuffer *first = new_buffer();
  GtkTextBuffer *second = new_buffer();
  markdown_apply_tags(first, "# First\n\n[link](#first)");
  markdown_apply_tags(second, "# Second\n\n[link](#second)");
  gint second_tags = gtk_text_tag_table_get_size(gtk_text_buffer_get_tag_table(second));
  markdown_apply_tags(first, "");
  g_object_unref(first);
  g_assert_nonnull(gtk_text_buffer_get_mark(second, "viewmd-anchor-second"));
  g_assert_cmpint(gtk_text_tag_table_get_size(gtk_text_buffer_get_tag_table(second)),
                  ==, second_tags);
  g_object_unref(second);
}

static void test_task_statuses(void) {
  GtkTextBuffer *buffer = new_buffer();
  markdown_apply_tags(buffer, "- [x] Done\n- [ ] Open\n- [X] Also done\n- Plain\n");
  assert_text(buffer, "☑ Done\n☐ Open\n☑ Also done\n• Plain\n");
  g_object_unref(buffer);
}

static void test_nested_tasks(void) {
  GtkTextBuffer *buffer = new_buffer();
  markdown_apply_tags(buffer, "- [ ] Parent\n  - [x] Child\n  - Plain child\n- [x] Last\n");
  assert_text(buffer, "☐ Parent\n☑ Child\n• Plain child\n☑ Last\n");
  g_object_unref(buffer);
}

static void test_ordered_tasks(void) {
  GtkTextBuffer *buffer = new_buffer();
  markdown_apply_tags(buffer, "9. [ ] Open\n10. Plain\n11. [X] Done\n12. Last\n");
  assert_text(buffer, "☐ Open\n10. Plain\n☑ Done\n12. Last\n");
  g_object_unref(buffer);
}

static void test_task_hanging_indent(void) {
  GtkTextBuffer *buffer = new_buffer();
  markdown_apply_tags(buffer, "- [ ] Task\n  continuation\n");
  assert_text(buffer, "☐ Task\ncontinuation\n");
  GtkTextIter at;
  gtk_text_buffer_get_start_iter(buffer, &at);
  GSList *tags = gtk_text_iter_get_tags(&at);
  gboolean found_indent = FALSE;
  for (GSList *item = tags; item; item = item->next) {
    gint indent = 0;
    g_object_get(item->data, "indent", &indent, NULL);
    if (indent < 0) {
      found_indent = TRUE;
      gtk_text_buffer_get_iter_at_offset(buffer, &at, 7);
      g_assert_true(gtk_text_iter_has_tag(&at, item->data));
    }
  }
  g_assert_true(found_indent);
  g_slist_free(tags);
  g_object_unref(buffer);
}

static void test_entity_names(void) {
  GtkTextBuffer *buffer = new_buffer();
  markdown_apply_tags(buffer,
      "&copy; &mdash; caf&eacute; A&nbsp;B &amp; &lt; &gt; &quot; &apos; "
      "&NotEqualTilde; &Afr; &CounterClockwiseContourIntegral;");
  assert_text(buffer, "© — café A\302\240B & < > \" ' ≂̸ 𝔄 ∳");
  g_object_unref(buffer);
}

static void test_entity_numbers(void) {
  GtkTextBuffer *buffer = new_buffer();
  markdown_apply_tags(buffer, "&#169; &#x2014; &#X1F600; &#0; &#xD800; "
                               "&#xDFFF; &#1114112; &#xFFFFFF;");
  assert_text(buffer, "© — 😀 � � � � �");
  g_object_unref(buffer);
}

static void test_entity_literals(void) {
  GtkTextBuffer *buffer = new_buffer();
  const gchar *text = "&MadeUpEntity; &COPYwrong; &copy &#; &#x; &#xGG; "
                       "&#12345678; &#x1234567;";
  markdown_apply_tags(buffer, text);
  assert_text(buffer, text);
  markdown_apply_tags(buffer, "&amp;copy; &#38;copy; \\&copy; &#42;literal&#42;");
  assert_text(buffer, "&copy; &copy; &copy; *literal*");
  GtkTextIter at;
  gtk_text_buffer_get_iter_at_offset(buffer, &at, 23);
  g_assert_false(gtk_text_iter_has_tag(&at, gtk_text_tag_table_lookup(
      gtk_text_buffer_get_tag_table(buffer), "italic")));
  g_object_unref(buffer);
}

static void test_entities_in_code(void) {
  GtkTextBuffer *buffer = new_buffer();
  markdown_apply_tags(buffer, "`&copy; &#169;`\n\n```\n&amp; &#x2014;\n```\n");
  assert_text(buffer, "&copy; &#169;\n\n&amp; &#x2014;\n");
  markdown_apply_tags(buffer, "    &copy; &#169;\n");
  assert_text(buffer, "&copy; &#169;\n");
  g_object_unref(buffer);
}

static void test_entity_attributes(void) {
  GtkTextBuffer *buffer = new_buffer();
  markdown_apply_tags(buffer, "[A &amp; B](https://example.com/?a=1&amp;b=2&#35;part)");
  assert_text(buffer, "A & B");
  GtkTextIter at;
  gtk_text_buffer_get_start_iter(buffer, &at);
  GSList *tags = gtk_text_iter_get_tags(&at);
  gboolean found_url = FALSE;
  for (GSList *item = tags; item; item = item->next) {
    const gchar *url = g_object_get_data(G_OBJECT(item->data), VIEWMD_LINK_URL_DATA);
    if (url) {
      g_assert_cmpstr(url, ==, "https://example.com/?a=1&b=2#part");
      found_url = TRUE;
    }
  }
  g_assert_true(found_url);
  g_slist_free(tags);

  markdown_apply_tags(buffer, "![A &amp; B](images/a&amp;b.png)");
  gtk_text_buffer_get_start_iter(buffer, &at);
  GtkTextChildAnchor *anchor = gtk_text_iter_get_child_anchor(&at);
  g_assert_nonnull(anchor);
  g_assert_cmpstr(g_object_get_data(G_OBJECT(anchor), VIEWMD_IMAGE_SRC_DATA),
                  ==, "images/a&b.png");
  g_assert_cmpstr(g_object_get_data(G_OBJECT(anchor), VIEWMD_IMAGE_ALT_DATA),
                  ==, "A & B");

  markdown_apply_tags(buffer, "![A](images/a\\&amp;b.png)");
  gtk_text_buffer_get_start_iter(buffer, &at);
  anchor = gtk_text_iter_get_child_anchor(&at);
  g_assert_nonnull(anchor);
  g_assert_cmpstr(g_object_get_data(G_OBJECT(anchor), VIEWMD_IMAGE_SRC_DATA),
                  ==, "images/a&amp;b.png");
  g_object_unref(buffer);
}

static void test_entities_in_headings_and_tables(void) {
  GtkTextBuffer *buffer = new_buffer();
  markdown_apply_tags(buffer, "# Tom &amp; Jerry\n\n| Header |\n| --- |\n"
                               "| &copy; &amp; &lt;b&gt; &NotEqualTilde; |\n");
  g_assert_nonnull(gtk_text_buffer_get_mark(buffer, "viewmd-anchor-tom-jerry"));
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, TRUE);
  g_assert_nonnull(strstr(text, "© & <b> ≂̸"));
  g_free(text);
  g_object_unref(buffer);
}

static void test_front_matter_closed(void) {
  const gchar *prefixes[] = {
    "---\ntitle: Hidden\ntags: [a, b]\n---\n",
    "---\ntitle: Hidden\n...\n",
    "---\r\ntitle: Hidden\r\n---\r\n",
    "\xEF\xBB\xBF---\ntitle: Hidden\n---\n",
    "--- \t\ntitle: Hidden\n... \t\n",
    "---\n---\n",
    "---\ntitle: |\n  ---\n  Indented delimiter\n---\n"
  };
  const gchar *body = "# Visible\n\nA **bold** paragraph.\n\n---\n\nEnd.";
  GtkTextBuffer *expected = new_buffer();
  markdown_apply_tags(expected, body);
  gchar *text = buffer_text(expected);
  GtkTextBuffer *buffer = new_buffer();
  for (guint i = 0; i < G_N_ELEMENTS(prefixes); i++) {
    gchar *source = g_strconcat(prefixes[i], body, NULL);
    markdown_apply_tags(buffer, source);
    assert_text(buffer, text);
    g_assert_nonnull(gtk_text_buffer_get_mark(buffer, "viewmd-anchor-visible"));
    g_free(source);
  }
  g_free(text);
  g_object_unref(expected);
  g_object_unref(buffer);
}

static void test_front_matter_only(void) {
  const gchar *sources[] = {
    "---\ntitle: Hidden\n---",
    "---\ntitle: Hidden\n...",
    "---\n---\n",
    "---\ntitle: Hidden\n---\n\n"
  };
  GtkTextBuffer *buffer = new_buffer();
  for (guint i = 0; i < G_N_ELEMENTS(sources); i++) {
    markdown_apply_tags(buffer, sources[i]);
    assert_text(buffer, "");
  }
  g_object_unref(buffer);
}

static void test_front_matter_boundaries(void) {
  const gchar *sources[] = {
    "---\ntitle: Keep", /* No closing delimiter. */
    "---\ntitle: Keep\n----\n",
    "---\ntitle: Keep\n... trailing text\n",
    "---\ntitle: Keep\n  ---\n", /* Indented YAML content is not a delimiter. */
    "\n---\ntitle: Keep\n---\n", /* Opening must be on the first line. */
    "Intro\n\n---\ntitle: Keep\n---\n",
    " ---\ntitle: Keep\n---\n",
    "----\ntitle: Keep\n---\n",
    "--- trailing text\ntitle: Keep\n---\n"
  };
  GtkTextBuffer *buffer = new_buffer();
  for (guint i = 0; i < G_N_ELEMENTS(sources); i++) {
    markdown_apply_tags(buffer, sources[i]);
    gchar *text = buffer_text(buffer);
    g_assert_nonnull(strstr(text, "title: Keep"));
    g_free(text);
  }
  markdown_apply_tags(buffer, "---\n\nBody");
  assert_text(buffer, "──────\n\nBody");
  markdown_apply_tags(buffer, "---");
  assert_text(buffer, "──────\n\n");
  g_object_unref(buffer);
}

static void test_front_matter_in_code(void) {
  GtkTextBuffer *buffer = new_buffer();
  markdown_apply_tags(buffer, "```yaml\n---\ntitle: Keep\n---\n```\n");
  assert_text(buffer, "---\ntitle: Keep\n---\n");
  g_object_unref(buffer);
}

static void test_front_matter_reload(void) {
  GtkTextBuffer *buffer = new_buffer();
  markdown_apply_tags(buffer, "# Hidden\n\n[Old](#hidden)");
  markdown_apply_tags(buffer, "---\n# Hidden\n[Old](#hidden)\n---\n# Visible\n");
  assert_text(buffer, "Visible\n");
  g_assert_null(gtk_text_buffer_get_mark(buffer, "viewmd-anchor-hidden"));
  g_assert_nonnull(gtk_text_buffer_get_mark(buffer, "viewmd-anchor-visible"));
  markdown_apply_tags(buffer, "# Restored\n");
  assert_text(buffer, "Restored\n");
  g_object_unref(buffer);
}

static void test_front_matter_setting(void) {
  GtkTextBuffer *buffer = new_buffer();
  const gchar *source = "---\ntitle: Metadata\n---\n# Body\n";
  g_assert_true(config->hide_front_matter);
  markdown_apply_tags(buffer, source);
  assert_text(buffer, "Body\n");
  config->hide_front_matter = FALSE;
  markdown_apply_tags(buffer, source);
  assert_text(buffer, "──────\n\ntitle: Metadata\n\n──────\n\nBody\n");
  markdown_apply_tags(buffer, "---\n---\n# Body\n");
  assert_text(buffer, "──────\n\n──────\n\nBody\n");
  config->hide_front_matter = TRUE;
  markdown_apply_tags(buffer, source);
  assert_text(buffer, "Body\n");
  g_object_unref(buffer);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  gtk_init_check(&argc, &argv);
  /* GtkTextBuffer works without a display. Register the boxed color types
   * normally registered by a successful GTK display initialization. */
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  g_type_ensure(GDK_TYPE_COLOR);
  G_GNUC_END_IGNORE_DEPRECATIONS
  g_type_ensure(GDK_TYPE_RGBA);
  config = config_new();

  g_test_add_func("/markdown/reload/link-tags", test_reload_link_tags);
  g_test_add_func("/markdown/reload/heading-anchors", test_reload_heading_anchors);
  g_test_add_func("/markdown/reload/preserve-other-state", test_reload_preserves_other_state);
  g_test_add_func("/markdown/reload/independent-buffers", test_reload_buffers_independent);
  g_test_add_func("/markdown/tasks/statuses", test_task_statuses);
  g_test_add_func("/markdown/tasks/nested", test_nested_tasks);
  g_test_add_func("/markdown/tasks/ordered", test_ordered_tasks);
  g_test_add_func("/markdown/tasks/hanging-indent", test_task_hanging_indent);
  g_test_add_func("/markdown/entities/names", test_entity_names);
  g_test_add_func("/markdown/entities/numbers", test_entity_numbers);
  g_test_add_func("/markdown/entities/literals", test_entity_literals);
  g_test_add_func("/markdown/entities/code", test_entities_in_code);
  g_test_add_func("/markdown/entities/attributes", test_entity_attributes);
  g_test_add_func("/markdown/entities/headings-and-tables", test_entities_in_headings_and_tables);
  g_test_add_func("/markdown/front-matter/closed", test_front_matter_closed);
  g_test_add_func("/markdown/front-matter/only", test_front_matter_only);
  g_test_add_func("/markdown/front-matter/boundaries", test_front_matter_boundaries);
  g_test_add_func("/markdown/front-matter/code", test_front_matter_in_code);
  g_test_add_func("/markdown/front-matter/reload", test_front_matter_reload);
  g_test_add_func("/markdown/front-matter/setting", test_front_matter_setting);
  gint result = g_test_run();
  config_free(config);
  return result;
}

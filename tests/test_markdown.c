#include "config.h"
#include "markdown.h"
#include "footnotes.h"
#include "md4c/md4c.h"
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

static const gchar *link_at(GtkTextBuffer *buffer, gint offset) {
  GtkTextIter at;
  gtk_text_buffer_get_iter_at_offset(buffer, &at, offset);
  GSList *tags = gtk_text_iter_get_tags(&at);
  const gchar *href = NULL;
  for (GSList *item = tags; item; item = item->next) {
    const gchar *candidate = g_object_get_data(G_OBJECT(item->data), VIEWMD_LINK_URL_DATA);
    if (candidate)
      href = candidate;
  }
  g_slist_free(tags);
  return href;
}

static void test_footnotes_basic(void) {
  GtkTextBuffer *buffer = new_buffer();
  markdown_apply_tags(buffer, "First[^named], again[^named], second[^1].\n\n"
      "[^1]: Numbered source.\n[^named]: Named **source**.\n");
  gchar *text = buffer_text(buffer);
  g_assert_true(g_str_has_prefix(text, "First[1], again[1], second[2]."));
  g_assert_null(strstr(text, "Footnotes"));
  g_assert_nonnull(strstr(text, "──────\n\n1. Named source."));
  g_assert_nonnull(strstr(text, "Named source."));
  g_assert_nonnull(strstr(text, "Numbered source."));
  g_assert_true(strstr(text, "Named source.") < strstr(text, "Numbered source."));
  g_assert_null(strstr(text, "[^"));
  g_assert_cmpstr(link_at(buffer, 5), ==, "viewmd-footnote:note-1");
  for (guint i = 1; i <= 2; i++) {
    gchar *name = g_strdup_printf("viewmd-footnote:ref-1-%u", i);
    g_assert_nonnull(gtk_text_buffer_get_mark(buffer, name));
    g_free(name);
  }
  g_assert_nonnull(gtk_text_buffer_get_mark(buffer, "viewmd-footnote:note-2"));
  GtkTextIter at;
  gtk_text_buffer_get_iter_at_offset(buffer, &at, 5);
  g_assert_true(gtk_text_iter_has_tag(&at, gtk_text_tag_table_lookup(
      gtk_text_buffer_get_tag_table(buffer), "footnote_reference")));
  g_assert_false(gtk_text_iter_has_tag(&at, gtk_text_tag_table_lookup(
      gtk_text_buffer_get_tag_table(buffer), "link")));
  g_assert_nonnull(strstr(text, "1. Named source. ↩ ↩"));
  g_assert_nonnull(strstr(text, "2. Numbered source. ↩"));
  const gchar *note = strstr(text, "1. Named");
  gtk_text_buffer_get_iter_at_offset(buffer, &at, g_utf8_pointer_to_offset(text, note));
  GSList *tags = gtk_text_iter_get_tags(&at);
  for (GSList *item = tags; item; item = item->next) {
    gint margin = 0;
    g_object_get(item->data, "left-margin", &margin, NULL);
    g_assert_cmpint(margin, ==, 0);
  }
  g_slist_free(tags);
  g_assert_false(gtk_text_iter_has_tag(&at, gtk_text_tag_table_lookup(
      gtk_text_buffer_get_tag_table(buffer), "list_bullet")));
  gint underline = -1;
  gdouble scale = 1;
  g_object_get(gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer),
      "footnote_link"), "underline", &underline, NULL);
  g_object_get(gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer),
      "footnote_reference"), "scale", &scale, NULL);
  g_assert_cmpint(underline, ==, PANGO_UNDERLINE_NONE);
  g_assert_cmpfloat(scale, <, 0.75);
  g_free(text);
  g_object_unref(buffer);
}

static void assert_no_footnotes(const gchar *source) {
  FootnoteDocument *document = footnotes_prepare(source, MD_DIALECT_GITHUB);
  g_assert_cmpstr(footnotes_source(document), ==, source);
  footnotes_free(document);
}

static void test_footnotes_code_definitions(void) {
  const gchar *examples[] = {
    "```md\n[^fake]: fenced\n```\n\nUse[^fake].\n",
    "```\n[^fake]: unclosed fence\n\nUse[^fake].\n",
    "~~~\n[^fake]: unclosed fence",
    "~~~~\n[^fake]: fenced\n~~~\n[^other]: still fenced\n~~~~\nUse[^fake].\n",
    "````\n```\n[^fake]: longer fence\n```\n````\nUse[^fake].\n",
    "    [^fake]: indented\n\nUse[^fake].\n",
    "\t[^fake]: indented\n\nUse[^fake].\n",
    "`start\n[^fake]: inline code\nend`\n\nUse[^fake].\n",
    "``start `\n[^fake]: inline code\nend``\n\nUse[^fake].\n",
    "- item\n\n  ```\n  [^fake]: list code\n  ```\n\nUse[^fake].\n",
    "> ```\n> [^fake]: quoted code\n> ```\n\nUse[^fake].\n",
    "<!--\n[^fake]: comment\n-->\n\nUse[^fake].\n",
    "<pre>\n[^fake]: HTML code\n</pre>\n\nUse[^fake].\n",
    "\\[^fake]: escaped definition\n\nUse[^fake].\n",
    "[^]: empty label\n\nUse[^].\n",
    "[^two words]: invalid label\n\nUse[^two words].\n"
  };
  for (guint i = 0; i < G_N_ELEMENTS(examples); i++) {
    g_test_message("Definition example %u", i);
    assert_no_footnotes(examples[i]);
  }
}

static void test_footnotes_code_references(void) {
  const gchar *examples[] = {
    "`[^n]`", "``code ` [^n]``", "`start\n[^n]\nend`",
    "```md\n[^n]\n```\n", "~~~\n[^n]\n~~~\n",
    "````\n```\n[^n]\n```\n````\n", "    [^n]\n", "\t[^n]\n",
    "- item\n\n  ```\n  [^n]\n  ```\n",
    "> ```\n> [^n]\n> ```\n", "\\[^n]", "&#91;^n]",
    "[label[^n]](https://example.com)", "[^n](https://example.com)",
    "![alt[^n]](image.png)", "[label](https://example.com/[^n])",
    "<!-- [^n] -->", "<pre>\n[^n]\n</pre>\n"
  };
  for (guint i = 0; i < G_N_ELEMENTS(examples); i++) {
    gchar *source = g_strconcat(examples[i], "\n\nReal[^n].\n\n[^n]: Note.\n", NULL);
    FootnoteDocument *document = footnotes_prepare(source, MD_DIALECT_GITHUB);
    g_test_message("Reference example %u", i);
    g_assert_true(g_str_has_prefix(footnotes_source(document), examples[i]));
    g_assert_null(strstr(footnotes_source(document), "ref-1-2"));
    /* Only the real reference creates a superscript link. */
    guint references = 0;
    const gchar *at = footnotes_source(document);
    while ((at = strstr(at, "(viewmd-footnote:"))) {
      const gchar *end = strchr(++at, ')');
      g_assert_nonnull(end);
      gchar *token = g_strndup(at, end - at);
      const FootnoteLink *link = footnotes_lookup_link(document, token);
      if (link && link->superscript)
        references++;
      g_free(token);
      at = end + 1;
    }
    g_assert_cmpuint(references, ==, 1);
    footnotes_free(document);
    g_free(source);
  }
}

static void test_footnotes_multiline(void) {
  GtkTextBuffer *buffer = new_buffer();
  markdown_apply_tags(buffer, "Read[^long].\n\n"
      "[^long]: First paragraph.\n    Continued.\n\n"
      "    Second **paragraph**.\n\n    ```\n    [^literal]: code\n    ```\n\n"
      "    - A list\n    - Another item\n\nAfter.\n");
  gchar *text = buffer_text(buffer);
  g_assert_nonnull(strstr(text, "Read[1]."));
  g_assert_nonnull(strstr(text, "First paragraph.\nContinued."));
  g_assert_nonnull(strstr(text, "Second paragraph."));
  g_assert_nonnull(strstr(text, "[^literal]: code"));
  g_assert_nonnull(strstr(text, "• A list"));
  g_assert_nonnull(strstr(text, "──────"));
  g_assert_true(strstr(text, "After.") < strstr(text, "──────"));
  g_free(text);
  g_object_unref(buffer);
}

static void test_footnotes_missing_and_duplicates(void) {
  GtkTextBuffer *buffer = new_buffer();
  markdown_apply_tags(buffer, "Unknown[^missing], known[^a].\n\n"
      "[^a]: First definition.\n[^a]: Duplicate definition.\n[^unused]: Unused.\n");
  gchar *text = buffer_text(buffer);
  g_assert_nonnull(strstr(text, "Unknown[^missing], known[1]."));
  g_assert_nonnull(strstr(text, "First definition."));
  g_assert_null(strstr(text, "Duplicate definition."));
  g_assert_null(strstr(text, "Unused."));
  g_free(text);
  g_object_unref(buffer);
}

static void test_footnotes_reload(void) {
  GtkTextBuffer *buffer = new_buffer();
  /* Prime the existing list-layout tag cache before measuring transient tags. */
  markdown_apply_tags(buffer, "1. List\n");
  markdown_apply_tags(buffer, "Plain text");
  gint baseline = gtk_text_tag_table_get_size(gtk_text_buffer_get_tag_table(buffer));
  for (guint i = 0; i < 20; i++) {
    markdown_apply_tags(buffer, "A[^a].\n\n[^a]: Note.\n");
    g_assert_cmpint(gtk_text_tag_table_get_size(gtk_text_buffer_get_tag_table(buffer)),
                    ==, baseline + 2);
  }
  markdown_apply_tags(buffer, "Plain text");
  g_assert_null(gtk_text_buffer_get_mark(buffer, "viewmd-footnote:note-1"));
  g_assert_null(gtk_text_buffer_get_mark(buffer, "viewmd-footnote:ref-1-1"));
  g_assert_null(gtk_text_buffer_get_mark(buffer, "viewmd-anchor-footnotes"));
  g_assert_cmpint(gtk_text_tag_table_get_size(gtk_text_buffer_get_tag_table(buffer)),
                  ==, baseline);
  g_object_unref(buffer);
}

static void test_footnotes_boundaries(void) {
  GtkTextBuffer *buffer = new_buffer();
  markdown_apply_tags(buffer, "A[^a] and B[^a-b_c].\r\n\r\n"
      "[^a]: First.\r\n[^a-b_c]: Second.\r\n\tTab continuation.\r\n");
  gchar *text = buffer_text(buffer);
  g_assert_true(g_str_has_prefix(text, "A[1] and B[2]."));
  g_assert_nonnull(strstr(text, "Second.\nTab continuation."));
  g_free(text);
  markdown_apply_tags(buffer, "A[^é].\n\n[^é]: Unicode label.");
  text = buffer_text(buffer);
  g_assert_true(g_str_has_prefix(text, "A[1]."));
  g_assert_nonnull(strstr(text, "Unicode label."));
  g_free(text);
  markdown_apply_tags(buffer, "\\\\[^a] and `unclosed [^a].\n\n[^a]: Note.");
  text = buffer_text(buffer);
  g_assert_true(g_str_has_prefix(text, "\\[1] and `unclosed [1]."));
  g_free(text);
  markdown_apply_tags(buffer, "A[^a].\n\n[^a]: ```\n    [^literal]\n    ```\n");
  text = buffer_text(buffer);
  g_assert_nonnull(strstr(text, "[^literal]\n"));
  g_assert_null(strstr(text, "```"));
  g_free(text);
  markdown_apply_tags(buffer, "[Original](viewmd-footnote:generated-1) A[^a].\n\n"
                               "[^a]: Note.");
  g_assert_cmpstr(link_at(buffer, 0), ==, "viewmd-footnote:generated-1");
  g_object_unref(buffer);
}

static void test_footnotes_fence_lengths(void) {
  for (guint length = 3; length <= 10; length++) {
    gchar *fence = g_strnfill(length, '`');
    gchar *short_fence = g_strnfill(length - 1, '`');
    gchar *source = g_strdup_printf("%s\n[^n]: inside fence\n%s\n"
        "[^other]: still code\n%s\n\nUse[^n] and [^other].\n",
        fence, short_fence, fence);
    assert_no_footnotes(source);
    g_free(source);
    g_free(short_fence);
    g_free(fence);
  }
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
  g_test_add_func("/markdown/footnotes/basic", test_footnotes_basic);
  g_test_add_func("/markdown/footnotes/code-definitions", test_footnotes_code_definitions);
  g_test_add_func("/markdown/footnotes/code-references", test_footnotes_code_references);
  g_test_add_func("/markdown/footnotes/multiline", test_footnotes_multiline);
  g_test_add_func("/markdown/footnotes/missing-and-duplicates", test_footnotes_missing_and_duplicates);
  g_test_add_func("/markdown/footnotes/reload", test_footnotes_reload);
  g_test_add_func("/markdown/footnotes/boundaries", test_footnotes_boundaries);
  g_test_add_func("/markdown/footnotes/fence-lengths", test_footnotes_fence_lengths);
  gint result = g_test_run();
  config_free(config);
  return result;
}

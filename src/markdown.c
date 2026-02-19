#include "markdown.h"
#include "code_highlight.h"
#include "config.h"
#include "md4c/md4c.h"
#include <string.h>

/* Tag names */
#define TAG_H1 "h1"
#define TAG_H2 "h2"
#define TAG_H3 "h3"
#define TAG_BOLD "bold"
#define TAG_ITALIC "italic"
#define TAG_STRIKE "strike"
#define TAG_CODE "code"
#define TAG_CODE_BLOCK "code_block"
#define TAG_QUOTE "quote"
#define TAG_LIST "list"
#define TAG_LIST_BULLET "list_bullet"
#define TAG_LINK "link"
#define TAG_HRULE "hrule"
#define TAG_TABLE "table"
#define TAG_TABLE_HEADER "table_header"
#define TAG_TABLE_SEPARATOR "table_separator"
#define TAG_INVISIBLE "invisible"
#define TABLE_MODEL_DATA_KEY "viewmd-table-model"

typedef struct {
  gboolean ordered;
  guint next_index;
} ListState;

typedef struct {
  MD_BLOCKTYPE type;
  guint pushed_tags;
} BlockState;

typedef struct {
  MD_SPANTYPE type;
  guint pushed_tags;
} SpanState;

typedef struct {
  gboolean is_header;
  GPtrArray *cells; /* gchar* */
} ViewmdTableRow;

typedef struct {
  guint col_count;
  GArray *aligns;   /* MD_ALIGN */
  GPtrArray *rows;  /* ViewmdTableRow* */
} ViewmdTable;

typedef struct {
  GtkTextBuffer *buffer;
  GtkTextIter iter;
  GPtrArray *active_tags; /* GtkTextTag* */
  GArray *block_stack;    /* BlockState */
  GArray *span_stack;     /* SpanState */
  GArray *list_stack;     /* ListState */
  GHashTable *anchor_counts;
  GString *heading_text;
  gint heading_start_offset;
  gboolean in_heading;
  gboolean list_item_prefix_pending;
  guint quote_depth;
  gboolean in_table_head;
  ViewmdTable *table_model;
  ViewmdTableRow *table_current_row;
  GString *table_cell_text;
  guint table_current_col;
  gboolean has_output;
  guint trailing_newlines;
} RenderCtx;

static void viewmd_table_row_free(gpointer data) {
  ViewmdTableRow *row = (ViewmdTableRow *)data;
  if (!row) {
    return;
  }
  if (row->cells) {
    g_ptr_array_free(row->cells, TRUE);
  }
  g_free(row);
}

static void viewmd_table_free(gpointer data) {
  ViewmdTable *table = (ViewmdTable *)data;
  if (!table) {
    return;
  }
  if (table->aligns) {
    g_array_free(table->aligns, TRUE);
  }
  if (table->rows) {
    g_ptr_array_free(table->rows, TRUE);
  }
  g_free(table);
}

static ViewmdTable *viewmd_table_new(guint col_count) {
  ViewmdTable *table = g_new0(ViewmdTable, 1);
  table->col_count = col_count;
  table->aligns = g_array_sized_new(FALSE, FALSE, sizeof(MD_ALIGN), col_count);
  table->rows = g_ptr_array_new_with_free_func(viewmd_table_row_free);
  for (guint i = 0; i < col_count; i++) {
    MD_ALIGN align = MD_ALIGN_DEFAULT;
    g_array_append_val(table->aligns, align);
  }
  return table;
}

static GtkTextTag *lookup_tag(GtkTextBuffer *buffer, const gchar *name) {
  GtkTextTagTable *table;
  if (!buffer || !name) {
    return NULL;
  }
  table = gtk_text_buffer_get_tag_table(buffer);
  return table ? gtk_text_tag_table_lookup(table, name) : NULL;
}

static void update_newline_state(RenderCtx *ctx, const gchar *text, gsize len) {
  if (!ctx || !text || len == 0) {
    return;
  }
  ctx->has_output = TRUE;
  for (gsize i = 0; i < len; i++) {
    if (text[i] == '\n') {
      ctx->trailing_newlines++;
    } else {
      ctx->trailing_newlines = 0;
    }
  }
}

static void apply_tag_by_offsets(GtkTextBuffer *buffer, GtkTextTag *tag,
                                 gint start_offset, gint end_offset) {
  GtkTextIter start;
  GtkTextIter end;

  if (!buffer || !tag || end_offset <= start_offset) {
    return;
  }

  gtk_text_buffer_get_iter_at_offset(buffer, &start, start_offset);
  gtk_text_buffer_get_iter_at_offset(buffer, &end, end_offset);
  gtk_text_buffer_apply_tag(buffer, tag, &start, &end);
}

static void apply_tag_by_name_offsets(GtkTextBuffer *buffer, const gchar *name,
                                      gint start_offset, gint end_offset) {
  GtkTextTag *tag = lookup_tag(buffer, name);
  apply_tag_by_offsets(buffer, tag, start_offset, end_offset);
}

static void apply_active_tags(RenderCtx *ctx, gint start_offset,
                              gint end_offset) {
  if (!ctx || end_offset <= start_offset) {
    return;
  }
  for (guint i = 0; i < ctx->active_tags->len; i++) {
    GtkTextTag *tag = g_ptr_array_index(ctx->active_tags, i);
    apply_tag_by_offsets(ctx->buffer, tag, start_offset, end_offset);
  }
}

static void insert_text(RenderCtx *ctx, const gchar *text, gsize len) {
  gint start_offset;
  gint end_offset;

  if (!ctx || !text || len == 0) {
    return;
  }

  start_offset = gtk_text_iter_get_offset(&ctx->iter);
  gtk_text_buffer_insert(ctx->buffer, &ctx->iter, text, (gint)len);
  end_offset = gtk_text_iter_get_offset(&ctx->iter);

  apply_active_tags(ctx, start_offset, end_offset);
  update_newline_state(ctx, text, len);
}

static void insert_cstr(RenderCtx *ctx, const gchar *text) {
  if (!text) {
    return;
  }
  insert_text(ctx, text, strlen(text));
}

static void ensure_newlines(RenderCtx *ctx, guint min_newlines) {
  if (!ctx || min_newlines == 0 || !ctx->has_output) {
    return;
  }
  while (ctx->trailing_newlines < min_newlines) {
    insert_cstr(ctx, "\n");
  }
}

static void push_active_tag(RenderCtx *ctx, GtkTextTag *tag, guint *counter) {
  if (!ctx || !tag) {
    return;
  }
  g_ptr_array_add(ctx->active_tags, tag);
  if (counter) {
    (*counter)++;
  }
}

static void push_active_tag_by_name(RenderCtx *ctx, const gchar *name,
                                    guint *counter) {
  push_active_tag(ctx, lookup_tag(ctx->buffer, name), counter);
}

static void pop_active_tags(RenderCtx *ctx, guint count) {
  if (!ctx) {
    return;
  }
  while (count > 0 && ctx->active_tags->len > 0) {
    g_ptr_array_remove_index(ctx->active_tags, ctx->active_tags->len - 1);
    count--;
  }
}

static gchar *attr_to_string(const MD_ATTRIBUTE *attr) {
  if (!attr || !attr->text || attr->size == 0) {
    return g_strdup("");
  }
  return g_strndup(attr->text, attr->size);
}

static gchar *md_text_to_utf8(MD_TEXTTYPE type, const MD_CHAR *text,
                              MD_SIZE size) {
  if (type == MD_TEXT_NULLCHAR) {
    GString *s = g_string_new(NULL);
    for (MD_SIZE i = 0; i < size; i++) {
      g_string_append(s, "\xEF\xBF\xBD");
    }
    return g_string_free(s, FALSE);
  }

  if (!text || size == 0) {
    return g_strdup("");
  }

  return g_strndup(text, size);
}

gchar *markdown_normalize_anchor_slug(const gchar *text) {
  gchar *decoded;
  const gchar *p;
  GString *out;
  gboolean prev_dash = TRUE;

  if (!text) {
    return g_strdup("");
  }

  decoded = g_uri_unescape_string(text, NULL);
  p = decoded ? decoded : text;
  out = g_string_new(NULL);

  while (*p) {
    gunichar c = g_utf8_get_char(p);
    if (g_unichar_isalnum(c)) {
      gchar utf8[8];
      gint len = g_unichar_to_utf8(g_unichar_tolower(c), utf8);
      utf8[len] = '\0';
      g_string_append_len(out, utf8, len);
      prev_dash = FALSE;
    } else if (c == ' ' || c == '-' || c == '_') {
      if (!prev_dash) {
        g_string_append_c(out, '-');
        prev_dash = TRUE;
      }
    }
    p = g_utf8_next_char(p);
  }

  while (out->len > 0 && out->str[out->len - 1] == '-') {
    g_string_truncate(out, out->len - 1);
  }

  g_free(decoded);
  return g_string_free(out, FALSE);
}

gchar *markdown_anchor_mark_name(const gchar *fragment) {
  gchar *slug = markdown_normalize_anchor_slug(fragment);
  gchar *name = g_strdup_printf("%s%s", VIEWMD_ANCHOR_MARK_PREFIX, slug);
  g_free(slug);
  return name;
}

static void capture_heading_text(RenderCtx *ctx, const gchar *text) {
  if (!ctx || !ctx->in_heading || !ctx->heading_text || !text) {
    return;
  }
  for (const gchar *p = text; *p != '\0'; p++) {
    g_string_append_c(ctx->heading_text, (*p == '\n' || *p == '\r') ? ' ' : *p);
  }
}

static void create_heading_anchor(RenderCtx *ctx) {
  gchar *base;
  guint count;
  gchar *slug;
  gchar *mark_name;
  GtkTextIter at;

  if (!ctx || !ctx->heading_text) {
    return;
  }

  base = markdown_normalize_anchor_slug(ctx->heading_text->str);
  if (!base || base[0] == '\0') {
    g_free(base);
    return;
  }

  count = GPOINTER_TO_UINT(g_hash_table_lookup(ctx->anchor_counts, base));
  slug = (count == 0) ? g_strdup(base) : g_strdup_printf("%s-%u", base, count);
  g_hash_table_replace(ctx->anchor_counts, g_strdup(base), GUINT_TO_POINTER(count + 1));

  mark_name = g_strdup_printf("%s%s", VIEWMD_ANCHOR_MARK_PREFIX, slug);
  gtk_text_buffer_get_iter_at_offset(ctx->buffer, &at, ctx->heading_start_offset);
  gtk_text_buffer_create_mark(ctx->buffer, mark_name, &at, TRUE);

  g_free(mark_name);
  g_free(slug);
  g_free(base);
}

static void table_capture_append(RenderCtx *ctx, const gchar *text) {
  if (!ctx || !ctx->table_cell_text || !text) {
    return;
  }
  g_string_append(ctx->table_cell_text, text);
}

static void table_start_row(RenderCtx *ctx) {
  ViewmdTableRow *row;
  if (!ctx || !ctx->table_model) {
    return;
  }
  row = g_new0(ViewmdTableRow, 1);
  row->is_header = ctx->in_table_head;
  row->cells = g_ptr_array_new_with_free_func(g_free);
  g_ptr_array_add(ctx->table_model->rows, row);
  ctx->table_current_row = row;
  ctx->table_current_col = 0;
}

static void table_start_cell(RenderCtx *ctx, void *detail) {
  MD_BLOCK_TD_DETAIL *td = (MD_BLOCK_TD_DETAIL *)detail;
  if (!ctx || !ctx->table_current_row) {
    return;
  }

  if (ctx->table_cell_text) {
    g_string_free(ctx->table_cell_text, TRUE);
  }
  ctx->table_cell_text = g_string_new(NULL);

  if (ctx->table_model && ctx->table_current_col < ctx->table_model->aligns->len) {
    g_array_index(ctx->table_model->aligns, MD_ALIGN, ctx->table_current_col) =
        td ? td->align : MD_ALIGN_DEFAULT;
  }
}

static void table_finish_cell(RenderCtx *ctx) {
  gchar *cell;
  if (!ctx || !ctx->table_current_row || !ctx->table_cell_text) {
    return;
  }
  cell = g_strdup(ctx->table_cell_text->str);
  g_strstrip(cell);
  g_ptr_array_add(ctx->table_current_row->cells, cell);
  g_string_free(ctx->table_cell_text, TRUE);
  ctx->table_cell_text = NULL;
  ctx->table_current_col++;
}

static void table_finish_row(RenderCtx *ctx) {
  if (!ctx || !ctx->table_current_row || !ctx->table_model) {
    return;
  }
  while (ctx->table_current_row->cells->len < ctx->table_model->col_count) {
    g_ptr_array_add(ctx->table_current_row->cells, g_strdup(""));
  }
  ctx->table_current_row = NULL;
}

static void table_emit_anchor(RenderCtx *ctx) {
  GtkTextChildAnchor *anchor;

  if (!ctx || !ctx->table_model) {
    return;
  }
  if (ctx->table_model->rows->len == 0 || ctx->table_model->col_count == 0) {
    viewmd_table_free(ctx->table_model);
    ctx->table_model = NULL;
    return;
  }

  anchor = gtk_text_buffer_create_child_anchor(ctx->buffer, &ctx->iter);
  g_object_set_data(G_OBJECT(anchor), VIEWMD_TABLE_ANCHOR_DATA, GINT_TO_POINTER(1));
  g_object_set_data_full(G_OBJECT(anchor), TABLE_MODEL_DATA_KEY, ctx->table_model,
                         viewmd_table_free);

  /* Force at least one hard line break after the embedded table widget so
   * following markdown never shares the same visual line. */
  insert_cstr(ctx, "\n");

  ctx->table_model = NULL;
}

static void insert_list_marker(RenderCtx *ctx) {
  ListState *list;
  gchar *ordered = NULL;
  gint marker_start_offset;
  gint marker_end_offset;
  guint depth_indent;

  if (!ctx || ctx->list_stack->len == 0) {
    return;
  }

  list = &g_array_index(ctx->list_stack, ListState, ctx->list_stack->len - 1);
  depth_indent = ((ctx->list_stack->len > 0) ? (ctx->list_stack->len - 1) * 2 : 0) +
                 (ctx->quote_depth * 2);

  for (guint i = 0; i < depth_indent; i++) {
    insert_cstr(ctx, " ");
  }

  marker_start_offset = gtk_text_iter_get_offset(&ctx->iter);
  if (list->ordered) {
    ordered = g_strdup_printf("%u.", list->next_index++);
    insert_cstr(ctx, ordered);
  } else {
    insert_cstr(ctx, "\xE2\x80\xA2");
  }
  marker_end_offset = gtk_text_iter_get_offset(&ctx->iter);
  apply_tag_by_name_offsets(ctx->buffer, TAG_LIST_BULLET, marker_start_offset,
                            marker_end_offset);

  insert_cstr(ctx, " ");
  g_free(ordered);
}

static int on_enter_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
  RenderCtx *ctx = (RenderCtx *)userdata;
  BlockState state = {type, 0};

  g_array_append_val(ctx->block_stack, state);

  switch (type) {
  case MD_BLOCK_DOC:
    break;

  case MD_BLOCK_P:
    if (ctx->list_item_prefix_pending) {
      ctx->list_item_prefix_pending = FALSE;
    } else {
      ensure_newlines(ctx, (ctx->list_stack->len > 0) ? 1 : 2);
    }
    break;

  case MD_BLOCK_H: {
    MD_BLOCK_H_DETAIL *h = (MD_BLOCK_H_DETAIL *)detail;
    ensure_newlines(ctx, 2);
    ctx->heading_start_offset = gtk_text_iter_get_offset(&ctx->iter);
    ctx->in_heading = TRUE;
    if (!ctx->heading_text) {
      ctx->heading_text = g_string_new(NULL);
    } else {
      g_string_set_size(ctx->heading_text, 0);
    }

    if (h && h->level == 1) {
      push_active_tag_by_name(ctx, TAG_H1,
                              &g_array_index(ctx->block_stack, BlockState,
                                             ctx->block_stack->len - 1)
                                   .pushed_tags);
    } else if (h && h->level == 2) {
      push_active_tag_by_name(ctx, TAG_H2,
                              &g_array_index(ctx->block_stack, BlockState,
                                             ctx->block_stack->len - 1)
                                   .pushed_tags);
    } else {
      push_active_tag_by_name(ctx, TAG_H3,
                              &g_array_index(ctx->block_stack, BlockState,
                                             ctx->block_stack->len - 1)
                                   .pushed_tags);
    }
    break;
  }

  case MD_BLOCK_QUOTE:
    ensure_newlines(ctx, 2);
    ctx->quote_depth++;
    push_active_tag_by_name(ctx, TAG_QUOTE,
                            &g_array_index(ctx->block_stack, BlockState,
                                           ctx->block_stack->len - 1)
                                 .pushed_tags);
    break;

  case MD_BLOCK_UL: {
    ListState list = {FALSE, 1};
    ensure_newlines(ctx, (ctx->list_stack->len > 0) ? 1 : 2);
    g_array_append_val(ctx->list_stack, list);
    break;
  }

  case MD_BLOCK_OL: {
    MD_BLOCK_OL_DETAIL *ol = (MD_BLOCK_OL_DETAIL *)detail;
    ListState list = {TRUE, (ol && ol->start > 0) ? ol->start : 1};
    ensure_newlines(ctx, (ctx->list_stack->len > 0) ? 1 : 2);
    g_array_append_val(ctx->list_stack, list);
    break;
  }

  case MD_BLOCK_LI:
    ensure_newlines(ctx, 1);
    push_active_tag_by_name(ctx, TAG_LIST,
                            &g_array_index(ctx->block_stack, BlockState,
                                           ctx->block_stack->len - 1)
                                 .pushed_tags);
    insert_list_marker(ctx);
    ctx->list_item_prefix_pending = TRUE;
    break;

  case MD_BLOCK_HR: {
    gint start_offset;
    gint end_offset;
    ensure_newlines(ctx, 2);
    start_offset = gtk_text_iter_get_offset(&ctx->iter);
    insert_cstr(ctx, "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80");
    end_offset = gtk_text_iter_get_offset(&ctx->iter);
    apply_tag_by_name_offsets(ctx->buffer, TAG_HRULE, start_offset, end_offset);
    ensure_newlines(ctx, 2);
    break;
  }

  case MD_BLOCK_CODE:
    ensure_newlines(ctx, 2);
    push_active_tag_by_name(ctx, TAG_CODE_BLOCK,
                            &g_array_index(ctx->block_stack, BlockState,
                                           ctx->block_stack->len - 1)
                                 .pushed_tags);
    break;

  case MD_BLOCK_HTML:
    ensure_newlines(ctx, 2);
    break;

  case MD_BLOCK_TABLE: {
    MD_BLOCK_TABLE_DETAIL *tbl = (MD_BLOCK_TABLE_DETAIL *)detail;
    ensure_newlines(ctx, 2);
    if (ctx->table_model) {
      viewmd_table_free(ctx->table_model);
    }
    ctx->table_model = viewmd_table_new(tbl ? tbl->col_count : 0);
    ctx->table_current_row = NULL;
    ctx->in_table_head = FALSE;
    break;
  }

  case MD_BLOCK_THEAD:
    ctx->in_table_head = TRUE;
    break;

  case MD_BLOCK_TBODY:
    ctx->in_table_head = FALSE;
    break;

  case MD_BLOCK_TR:
    table_start_row(ctx);
    break;

  case MD_BLOCK_TH:
  case MD_BLOCK_TD:
    table_start_cell(ctx, detail);
    break;
  }

  return 0;
}

static int on_leave_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
  RenderCtx *ctx = (RenderCtx *)userdata;
  (void)detail;

  if (type == MD_BLOCK_H) {
    create_heading_anchor(ctx);
    ctx->in_heading = FALSE;
    ensure_newlines(ctx, 1);
  } else if (type == MD_BLOCK_QUOTE) {
    if (ctx->quote_depth > 0) {
      ctx->quote_depth--;
    }
  } else if (type == MD_BLOCK_UL || type == MD_BLOCK_OL) {
    if (ctx->list_stack->len > 0) {
      g_array_set_size(ctx->list_stack, ctx->list_stack->len - 1);
    }
  } else if (type == MD_BLOCK_LI) {
    ctx->list_item_prefix_pending = FALSE;
    ensure_newlines(ctx, 1);
  } else if (type == MD_BLOCK_TH || type == MD_BLOCK_TD) {
    table_finish_cell(ctx);
  } else if (type == MD_BLOCK_TR) {
    table_finish_row(ctx);
  } else if (type == MD_BLOCK_TABLE) {
    table_finish_row(ctx);
    table_emit_anchor(ctx);
    ensure_newlines(ctx, 2);
  }

  if (ctx->block_stack->len > 0) {
    BlockState state =
        g_array_index(ctx->block_stack, BlockState, ctx->block_stack->len - 1);
    pop_active_tags(ctx, state.pushed_tags);
    g_array_set_size(ctx->block_stack, ctx->block_stack->len - 1);
  }

  return 0;
}

static int on_enter_span(MD_SPANTYPE type, void *detail, void *userdata) {
  RenderCtx *ctx = (RenderCtx *)userdata;
  SpanState state = {type, 0};

  g_array_append_val(ctx->span_stack, state);

  switch (type) {
  case MD_SPAN_EM:
    push_active_tag_by_name(ctx, TAG_ITALIC,
                            &g_array_index(ctx->span_stack, SpanState,
                                           ctx->span_stack->len - 1)
                                 .pushed_tags);
    break;

  case MD_SPAN_STRONG:
    push_active_tag_by_name(ctx, TAG_BOLD,
                            &g_array_index(ctx->span_stack, SpanState,
                                           ctx->span_stack->len - 1)
                                 .pushed_tags);
    break;

  case MD_SPAN_CODE:
    push_active_tag_by_name(ctx, TAG_CODE,
                            &g_array_index(ctx->span_stack, SpanState,
                                           ctx->span_stack->len - 1)
                                 .pushed_tags);
    break;

  case MD_SPAN_DEL:
    push_active_tag_by_name(ctx, TAG_STRIKE,
                            &g_array_index(ctx->span_stack, SpanState,
                                           ctx->span_stack->len - 1)
                                 .pushed_tags);
    break;

  case MD_SPAN_A: {
    MD_SPAN_A_DETAIL *a = (MD_SPAN_A_DETAIL *)detail;
    gchar *href = attr_to_string(a ? &a->href : NULL);
    GtkTextTag *url_tag = gtk_text_buffer_create_tag(ctx->buffer, NULL, NULL);
    g_object_set_data_full(G_OBJECT(url_tag), VIEWMD_LINK_URL_DATA, href, g_free);
    push_active_tag_by_name(ctx, TAG_LINK,
                            &g_array_index(ctx->span_stack, SpanState,
                                           ctx->span_stack->len - 1)
                                 .pushed_tags);
    push_active_tag(ctx, url_tag,
                    &g_array_index(ctx->span_stack, SpanState,
                                   ctx->span_stack->len - 1)
                         .pushed_tags);
    break;
  }

  default:
    break;
  }

  return 0;
}

static int on_leave_span(MD_SPANTYPE type, void *detail, void *userdata) {
  RenderCtx *ctx = (RenderCtx *)userdata;
  (void)type;
  (void)detail;

  if (ctx->span_stack->len > 0) {
    SpanState state =
        g_array_index(ctx->span_stack, SpanState, ctx->span_stack->len - 1);
    pop_active_tags(ctx, state.pushed_tags);
    g_array_set_size(ctx->span_stack, ctx->span_stack->len - 1);
  }

  return 0;
}

static int on_text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size,
                   void *userdata) {
  RenderCtx *ctx = (RenderCtx *)userdata;
  gchar *rendered = NULL;

  if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) {
    rendered = g_strdup("\n");
  } else {
    rendered = md_text_to_utf8(type, text, size);
  }

  if (ctx->list_item_prefix_pending && rendered[0] != '\0') {
    ctx->list_item_prefix_pending = FALSE;
  }
  if (ctx->table_cell_text) {
    table_capture_append(ctx, rendered);
  } else {
    insert_cstr(ctx, rendered);
    capture_heading_text(ctx, rendered);
  }
  g_free(rendered);
  return 0;
}

void markdown_init_tags(GtkTextBuffer *buffer) {
  gtk_text_buffer_create_tag(buffer, TAG_INVISIBLE, "invisible", TRUE, NULL);

  gtk_text_buffer_create_tag(buffer, TAG_H1, "weight", PANGO_WEIGHT_BOLD, "scale",
                             2.0, "foreground", config->h1_color,
                             "pixels-below-lines", 12, NULL);
  gtk_text_buffer_create_tag(buffer, TAG_H2, "weight", PANGO_WEIGHT_BOLD, "scale",
                             1.6, "foreground", config->h2_color,
                             "pixels-below-lines", 10, NULL);
  gtk_text_buffer_create_tag(buffer, TAG_H3, "weight", PANGO_WEIGHT_BOLD, "scale",
                             1.3, "foreground", config->h3_color,
                             "pixels-below-lines", 8, NULL);

  gtk_text_buffer_create_tag(buffer, TAG_BOLD, "weight", PANGO_WEIGHT_BOLD, NULL);
  gtk_text_buffer_create_tag(buffer, TAG_ITALIC, "style", PANGO_STYLE_ITALIC,
                             NULL);
  gtk_text_buffer_create_tag(buffer, TAG_STRIKE, "strikethrough", TRUE, NULL);

  gtk_text_buffer_create_tag(buffer, TAG_CODE, "family", "Monospace",
                             "background", "#3E4451", "foreground", "#E06C75",
                             NULL);
  gtk_text_buffer_create_tag(
      buffer, TAG_CODE_BLOCK, "family", "Monospace", "foreground", "#ABB2BF",
      "paragraph-background", "#2C313A", "left-margin", 24, "right-margin", 16,
      NULL);

  gtk_text_buffer_create_tag(buffer, MARKYD_TAG_CODE_KW_A, "family", "Monospace",
                             "foreground", config->h1_color, "weight",
                             PANGO_WEIGHT_BOLD, NULL);
  gtk_text_buffer_create_tag(buffer, MARKYD_TAG_CODE_KW_B, "family", "Monospace",
                             "foreground", config->h2_color, "weight",
                             PANGO_WEIGHT_BOLD, NULL);
  gtk_text_buffer_create_tag(buffer, MARKYD_TAG_CODE_KW_C, "family", "Monospace",
                             "foreground", config->h3_color, "weight",
                             PANGO_WEIGHT_BOLD, NULL);
  gtk_text_buffer_create_tag(buffer, MARKYD_TAG_CODE_LITERAL, "family",
                             "Monospace", "foreground", config->h3_color, NULL);

  gtk_text_buffer_create_tag(buffer, TAG_QUOTE, "left-margin", 24, "style",
                             PANGO_STYLE_ITALIC, "foreground", "#5C6370",
                             "paragraph-background", "#2C313A", NULL);

  gtk_text_buffer_create_tag(buffer, TAG_LIST, "left-margin", 28, NULL);
  gtk_text_buffer_create_tag(buffer, TAG_LIST_BULLET, "foreground",
                             config->list_bullet_color, NULL);

  gtk_text_buffer_create_tag(buffer, TAG_LINK, "foreground", "#61AFEF",
                             "underline", PANGO_UNDERLINE_SINGLE, NULL);

  gtk_text_buffer_create_tag(buffer, TAG_HRULE, "foreground", "#5C6370",
                             "justification", GTK_JUSTIFY_CENTER,
                             "pixels-above-lines", 6, "pixels-below-lines", 6,
                             NULL);

  gtk_text_buffer_create_tag(buffer, TAG_TABLE, "family", "Monospace",
                             "left-margin", 20, "right-margin", 12,
                             "paragraph-background", "#2C313A", NULL);
  gtk_text_buffer_create_tag(buffer, TAG_TABLE_HEADER, "family", "Monospace",
                             "weight", PANGO_WEIGHT_BOLD, "foreground",
                             config->h1_color, NULL);
  gtk_text_buffer_create_tag(buffer, TAG_TABLE_SEPARATOR, "family", "Monospace",
                             "foreground", "#5C6370", NULL);
}

void markdown_update_accent_tags(GtkTextBuffer *buffer) {
  GtkTextTagTable *table;
  GtkTextTag *tag;

  if (!buffer || !config) {
    return;
  }

  table = gtk_text_buffer_get_tag_table(buffer);
  if (!table) {
    return;
  }

  tag = gtk_text_tag_table_lookup(table, TAG_H1);
  if (tag) {
    g_object_set(tag, "foreground", config->h1_color, NULL);
  }
  tag = gtk_text_tag_table_lookup(table, TAG_H2);
  if (tag) {
    g_object_set(tag, "foreground", config->h2_color, NULL);
  }
  tag = gtk_text_tag_table_lookup(table, TAG_H3);
  if (tag) {
    g_object_set(tag, "foreground", config->h3_color, NULL);
  }
  tag = gtk_text_tag_table_lookup(table, TAG_LIST_BULLET);
  if (tag) {
    g_object_set(tag, "foreground", config->list_bullet_color, NULL);
  }
  tag = gtk_text_tag_table_lookup(table, MARKYD_TAG_CODE_KW_A);
  if (tag) {
    g_object_set(tag, "foreground", config->h1_color, NULL);
  }
  tag = gtk_text_tag_table_lookup(table, MARKYD_TAG_CODE_KW_B);
  if (tag) {
    g_object_set(tag, "foreground", config->h2_color, NULL);
  }
  tag = gtk_text_tag_table_lookup(table, MARKYD_TAG_CODE_KW_C);
  if (tag) {
    g_object_set(tag, "foreground", config->h3_color, NULL);
  }
  tag = gtk_text_tag_table_lookup(table, MARKYD_TAG_CODE_LITERAL);
  if (tag) {
    g_object_set(tag, "foreground", config->h3_color, NULL);
  }
  tag = gtk_text_tag_table_lookup(table, TAG_TABLE_HEADER);
  if (tag) {
    g_object_set(tag, "foreground", config->h1_color, NULL);
  }
}

static gfloat align_to_xalign(MD_ALIGN align) {
  switch (align) {
  case MD_ALIGN_RIGHT:
    return 1.0f;
  case MD_ALIGN_CENTER:
    return 0.5f;
  case MD_ALIGN_LEFT:
  case MD_ALIGN_DEFAULT:
  default:
    return 0.0f;
  }
}

GtkWidget *markdown_create_table_widget(GtkTextChildAnchor *anchor) {
  ViewmdTable *table;
  GtkWidget *wrapper;
  GtkWidget *grid;

  if (!anchor) {
    return NULL;
  }

  table = (ViewmdTable *)g_object_get_data(G_OBJECT(anchor), TABLE_MODEL_DATA_KEY);
  if (!table || table->col_count == 0 || !table->rows || table->rows->len == 0) {
    return NULL;
  }

  wrapper = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_style_context_add_class(gtk_widget_get_style_context(wrapper),
                              "viewmd-table");
  gtk_widget_set_halign(wrapper, GTK_ALIGN_START);
  gtk_widget_set_margin_top(wrapper, 6);
  gtk_widget_set_margin_bottom(wrapper, 6);
  gtk_widget_set_margin_start(wrapper, 8);
  gtk_widget_set_margin_end(wrapper, 8);

  grid = gtk_grid_new();
  gtk_style_context_add_class(gtk_widget_get_style_context(grid),
                              "viewmd-table-grid");
  gtk_grid_set_row_spacing(GTK_GRID(grid), 0);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 0);
  gtk_box_pack_start(GTK_BOX(wrapper), grid, TRUE, TRUE, 0);

  for (guint r = 0; r < table->rows->len; r++) {
    ViewmdTableRow *row = g_ptr_array_index(table->rows, r);
    if (!row) {
      continue;
    }

    for (guint c = 0; c < table->col_count; c++) {
      GtkWidget *cell = gtk_event_box_new();
      GtkWidget *label = gtk_label_new(NULL);
      const gchar *text = "";
      gchar *markup = NULL;
      MD_ALIGN align = (c < table->aligns->len)
                           ? g_array_index(table->aligns, MD_ALIGN, c)
                           : MD_ALIGN_DEFAULT;

      if (c < row->cells->len) {
        text = g_ptr_array_index(row->cells, c);
        if (!text) {
          text = "";
        }
      }

      gtk_style_context_add_class(gtk_widget_get_style_context(cell),
                                  "viewmd-table-cell");
      if (row->is_header) {
        gtk_style_context_add_class(gtk_widget_get_style_context(cell),
                                    "viewmd-table-header-cell");
      }
      gtk_style_context_add_class(gtk_widget_get_style_context(label),
                                  "viewmd-table-label");
      gtk_widget_set_hexpand(cell, FALSE);
      gtk_widget_set_vexpand(cell, FALSE);
      gtk_container_add(GTK_CONTAINER(cell), label);

      gtk_label_set_xalign(GTK_LABEL(label), align_to_xalign(align));
      gtk_label_set_yalign(GTK_LABEL(label), 0.5f);
      gtk_label_set_line_wrap(GTK_LABEL(label), FALSE);
      gtk_label_set_selectable(GTK_LABEL(label), FALSE);
      gtk_widget_set_margin_start(label, 8);
      gtk_widget_set_margin_end(label, 8);
      gtk_widget_set_margin_top(label, row->is_header ? 6 : 5);
      gtk_widget_set_margin_bottom(label, row->is_header ? 6 : 5);

      if (row->is_header) {
        markup = g_markup_printf_escaped("<b>%s</b>", text);
        gtk_label_set_markup(GTK_LABEL(label), markup);
        g_free(markup);
      } else {
        gtk_label_set_text(GTK_LABEL(label), text);
      }

      gtk_grid_attach(GTK_GRID(grid), cell, (gint)c, (gint)r, 1, 1);
    }
  }

  return wrapper;
}

void markdown_apply_tags(GtkTextBuffer *buffer, const gchar *source) {
  RenderCtx ctx;
  MD_PARSER parser = {0};
  gint rc;

  if (!buffer) {
    return;
  }

  gtk_text_buffer_set_text(buffer, "", -1);

  memset(&ctx, 0, sizeof(ctx));
  ctx.buffer = buffer;
  ctx.active_tags = g_ptr_array_new();
  ctx.block_stack = g_array_new(FALSE, FALSE, sizeof(BlockState));
  ctx.span_stack = g_array_new(FALSE, FALSE, sizeof(SpanState));
  ctx.list_stack = g_array_new(FALSE, FALSE, sizeof(ListState));
  ctx.anchor_counts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  ctx.heading_start_offset = 0;
  ctx.has_output = FALSE;
  ctx.trailing_newlines = 0;
  gtk_text_buffer_get_start_iter(buffer, &ctx.iter);

  parser.abi_version = 0;
  parser.flags = MD_DIALECT_GITHUB | MD_FLAG_PERMISSIVEATXHEADERS;
  parser.enter_block = on_enter_block;
  parser.leave_block = on_leave_block;
  parser.enter_span = on_enter_span;
  parser.leave_span = on_leave_span;
  parser.text = on_text;
  parser.debug_log = NULL;
  parser.syntax = NULL;

  rc = md_parse((source ? source : ""), (MD_SIZE)strlen(source ? source : ""),
                &parser, &ctx);
  if (rc != 0) {
    gtk_text_buffer_set_text(buffer, source ? source : "", -1);
  }

  if (ctx.table_cell_text) {
    g_string_free(ctx.table_cell_text, TRUE);
  }
  if (ctx.table_model) {
    viewmd_table_free(ctx.table_model);
  }
  if (ctx.heading_text) {
    g_string_free(ctx.heading_text, TRUE);
  }
  g_hash_table_destroy(ctx.anchor_counts);
  g_array_free(ctx.list_stack, TRUE);
  g_array_free(ctx.span_stack, TRUE);
  g_array_free(ctx.block_stack, TRUE);
  g_ptr_array_free(ctx.active_tags, TRUE);
}

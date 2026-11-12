#include "footnotes.h"
#include "md4c/md4c.h"
#include <stdint.h>
#include <string.h>

typedef struct {
  gsize start, marker, colon, end;
  gchar *label;
  gchar *body;
  guint number;
  guint references;
} Definition;

typedef struct {
  gsize start, end;
  Definition *definition;
  guint occurrence;
} Reference;

struct _FootnoteDocument {
  gchar *source;
  gchar *token_prefix;
  GHashTable *links;
};

enum { TEXT_NORMAL = 1, TEXT_TOP_LEVEL = 2 };

typedef struct {
  const gchar *source;
  gsize length;
  guint8 *allowed;
  guint containers;
  guint links;
} Scan;

static int scan_block(MD_BLOCKTYPE type, void *detail, void *data) {
  Scan *scan = data;
  (void)detail;
  if (type == MD_BLOCK_QUOTE || type == MD_BLOCK_UL || type == MD_BLOCK_OL)
    scan->containers++;
  return 0;
}

static int scan_leave_block(MD_BLOCKTYPE type, void *detail, void *data) {
  Scan *scan = data;
  (void)detail;
  if (type == MD_BLOCK_QUOTE || type == MD_BLOCK_UL || type == MD_BLOCK_OL)
    scan->containers--;
  return 0;
}

static int scan_span(MD_SPANTYPE type, void *detail, void *data) {
  Scan *scan = data;
  (void)detail;
  if (type == MD_SPAN_A || type == MD_SPAN_IMG)
    scan->links++;
  return 0;
}

static int scan_leave_span(MD_SPANTYPE type, void *detail, void *data) {
  Scan *scan = data;
  (void)detail;
  if (type == MD_SPAN_A || type == MD_SPAN_IMG)
    scan->links--;
  return 0;
}

static int scan_text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size,
                      void *data) {
  Scan *scan = data;
  uintptr_t base = (uintptr_t)scan->source;
  uintptr_t at = (uintptr_t)text;
  if (type != MD_TEXT_NORMAL || scan->links || at < base ||
      at - base > scan->length || size > scan->length - (at - base))
    return 0;
  guint8 flags = TEXT_NORMAL | (scan->containers ? 0 : TEXT_TOP_LEVEL);
  memset(scan->allowed + (at - base), flags, size);
  return 0;
}

/* Let the Markdown parser decide which bytes are literal prose. In particular,
 * it handles fence lengths, container indentation and multiline backtick spans. */
static guint8 *scan_prose(const gchar *source, guint flags) {
  Scan scan = {source, strlen(source), NULL, 0, 0};
  scan.allowed = g_malloc0(scan.length + 1);
  MD_PARSER parser = {0};
  parser.flags = flags;
  parser.enter_block = scan_block;
  parser.leave_block = scan_leave_block;
  parser.enter_span = scan_span;
  parser.leave_span = scan_leave_span;
  parser.text = scan_text;
  if (md_parse(source, (MD_SIZE)scan.length, &parser, &scan) != 0)
    memset(scan.allowed, 0, scan.length);
  return scan.allowed;
}

static gboolean allowed_range(const guint8 *allowed, gsize start, gsize end,
                               guint8 flag) {
  for (gsize i = start; i < end; i++)
    if (!(allowed[i] & flag))
      return FALSE;
  return TRUE;
}

static gboolean escaped_at(const gchar *source, gsize at) {
  guint slashes = 0;
  while (at > 0 && source[--at] == '\\')
    slashes++;
  return slashes % 2 != 0;
}

/* Labels are nonempty and contain no whitespace, brackets or escapes. */
static gsize marker_end(const gchar *source, gsize start) {
  if (source[start] != '[' || source[start + 1] != '^')
    return 0;
  gsize i = start + 2;
  for (; source[i] && source[i] != ']'; i++) {
    if (g_ascii_isspace(source[i]) || source[i] == '[' || source[i] == '\\')
      return 0;
  }
  return source[i] == ']' && i > start + 2 ? i + 1 : 0;
}

static const gchar *next_line(const gchar *line) {
  const gchar *end = strchr(line, '\n');
  return end ? end + 1 : line + strlen(line);
}

static gboolean blank_line(const gchar *line, const gchar *end) {
  for (; line < end; line++)
    if (!g_ascii_isspace(*line))
      return FALSE;
  return TRUE;
}

static guint continuation_indent(const gchar *line, const gchar *end) {
  guint columns = 0;
  const gchar *p = line;
  while (p < end && columns < 4) {
    if (*p == ' ')
      columns++;
    else if (*p == '\t')
      columns += 4 - columns % 4;
    else
      return 0;
    p++;
  }
  return columns >= 4 ? (guint)(p - line) : 0;
}

static void definition_free(gpointer data) {
  Definition *definition = data;
  g_free(definition->label);
  g_free(definition->body);
  g_free(definition);
}

static GPtrArray *find_definitions(const gchar *source) {
  GPtrArray *definitions = g_ptr_array_new_with_free_func(definition_free);
  for (const gchar *line = source; *line; line = next_line(line)) {
    const gchar *marker = line;
    while (*marker == ' ' && marker - line < 3)
      marker++;
    gsize end = marker_end(source, marker - source);
    if (!end || source[end] != ':')
      continue;
    Definition *definition = g_new0(Definition, 1);
    definition->start = line - source;
    definition->marker = marker - source;
    definition->colon = end;
    definition->label = g_strndup(marker + 2, end - definition->marker - 3);
    g_ptr_array_add(definitions, definition);
  }
  return definitions;
}

static void collect_body(Definition *definition, const gchar *source) {
  const gchar *content = source + definition->colon + 1;
  while (*content == ' ' || *content == '\t')
    content++;
  const gchar *end = next_line(content);
  GString *body = g_string_new_len(content, end - content);
  definition->end = end - source;
  const gchar *pending = end;
  for (const gchar *line = end; *line; line = end) {
    end = next_line(line);
    if (blank_line(line, end))
      continue;
    guint indent = continuation_indent(line, end);
    if (!indent)
      break;
    g_string_append_len(body, pending, line - pending);
    g_string_append_len(body, line + indent, end - line - indent);
    pending = end;
    definition->end = end - source;
  }
  definition->body = g_string_free(body, FALSE);
}

static void mask_range(gchar *source, gsize start, gsize end) {
  for (gsize i = start; i < end; i++)
    if (source[i] != '\n' && source[i] != '\r')
      source[i] = ' ';
}

static void link_free(gpointer data) {
  FootnoteLink *link = data;
  g_free(link->anchor);
  g_free(link->target);
  g_free(link);
}

static FootnoteLink *append_link(FootnoteDocument *document, GString *out,
                         const gchar *text, gchar *anchor, gchar *target,
                         gboolean superscript) {
  FootnoteLink *link = g_new0(FootnoteLink, 1);
  link->anchor = anchor;
  link->target = target;
  link->superscript = superscript;
  gchar *token = g_strdup_printf("%s%u", document->token_prefix,
                                 g_hash_table_size(document->links) + 1);
  g_string_append_printf(out, "[%s](%s)", text, token);
  g_hash_table_insert(document->links, token, link);
  return link;
}

FootnoteDocument *footnotes_prepare(const gchar *source, guint parser_flags) {
  FootnoteDocument *document = g_new0(FootnoteDocument, 1);
  document->links = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, link_free);
  GPtrArray *definitions = find_definitions(source);
  if (!definitions->len) {
    document->source = g_strdup(source);
    g_ptr_array_free(definitions, TRUE);
    return document;
  }
  /* Keep generated links distinct from all URIs in the input, without scanning
   * successively longer prefixes in adversarial documents. */
  do {
    g_free(document->token_prefix);
    gchar *uuid = g_uuid_string_random();
    document->token_prefix = g_strdup_printf("viewmd-footnote:%s-", uuid);
    g_free(uuid);
  } while (strstr(source, document->token_prefix));

  /* Mask only the definition colon in the probe. Otherwise md4c can consume a
   * footnote definition as an ordinary link definition without reporting text. */
  gchar *probe = g_strdup(source);
  for (guint i = 0; i < definitions->len; i++) {
    Definition *definition = g_ptr_array_index(definitions, i);
    probe[definition->colon] = ' ';
  }
  guint8 *allowed = scan_prose(probe, parser_flags);
  GHashTable *by_label = g_hash_table_new(g_str_hash, g_str_equal);
  for (guint i = 0; i < definitions->len;) {
    Definition *definition = g_ptr_array_index(definitions, i);
    if (!allowed_range(allowed, definition->marker, definition->colon,
                        TEXT_TOP_LEVEL)) {
      g_ptr_array_remove_index(definitions, i);
      continue;
    }
    collect_body(definition, source);
    if (!g_hash_table_contains(by_label, definition->label))
      g_hash_table_insert(by_label, definition->label, definition);
    i++;
  }
  g_free(allowed);
  g_free(probe);

  probe = g_strdup(source);
  for (guint i = 0; i < definitions->len; i++) {
    Definition *definition = g_ptr_array_index(definitions, i);
    mask_range(probe, definition->start, definition->end);
  }
  allowed = scan_prose(probe, parser_flags);
  GArray *references = g_array_new(FALSE, FALSE, sizeof(Reference));
  GPtrArray *ordered = g_ptr_array_new();
  for (gsize i = 0; source[i]; i++) {
    gsize end = marker_end(source, i);
    if (!end || escaped_at(source, i) ||
        !allowed_range(allowed, i, end, TEXT_NORMAL))
      continue;
    gchar *label = g_strndup(source + i + 2, end - i - 3);
    Definition *definition = g_hash_table_lookup(by_label, label);
    g_free(label);
    if (definition) {
      if (!definition->number) {
        g_ptr_array_add(ordered, definition);
        definition->number = ordered->len;
      }
      Reference reference = {i, end, definition, ++definition->references};
      g_array_append_val(references, reference);
    }
    i = end - 1;
  }
  g_free(allowed);

  GString *out = g_string_new(NULL);
  guint def_index = 0, ref_index = 0;
  for (gsize i = 0; source[i];) {
    Definition *definition = def_index < definitions->len
        ? g_ptr_array_index(definitions, def_index) : NULL;
    Reference *reference = ref_index < references->len
        ? &g_array_index(references, Reference, ref_index) : NULL;
    if (definition && i == definition->start) {
      g_string_append_len(out, probe + i, definition->end - i);
      i = definition->end;
      def_index++;
    } else if (reference && i == reference->start) {
      guint n = reference->definition->number;
      gchar *number = g_strdup_printf("\\[%u\\]", n);
      append_link(document, out, number,
          g_strdup_printf("viewmd-footnote:ref-%u-%u", n, reference->occurrence),
          g_strdup_printf("viewmd-footnote:note-%u", n), TRUE);
      g_free(number);
      i = reference->end;
      ref_index++;
    } else {
      g_string_append_c(out, source[i++]);
    }
  }
  if (ordered->len)
    g_string_append(out, "\n\n***\n\n");
  for (guint i = 0; i < ordered->len; i++) {
    Definition *definition = g_ptr_array_index(ordered, i);
    FootnoteLink *label = append_link(document, out, "",
        g_strdup_printf("viewmd-footnote:note-%u", definition->number), NULL, FALSE);
    label->number = definition->number;
    /* Separate source paragraphs preserve the note's block syntax. The renderer
     * joins an initial prose paragraph and the return links for presentation. */
    g_string_append_printf(out, "\n\n%s\n\n", definition->body);
    for (guint j = 1; j <= definition->references; j++) {
      append_link(document, out, "↩", NULL,
          g_strdup_printf("viewmd-footnote:ref-%u-%u", definition->number, j), FALSE);
      g_string_append_c(out, ' ');
    }
    g_string_append(out, "\n\n");
  }
  document->source = g_string_free(out, FALSE);
  g_free(probe);
  g_hash_table_destroy(by_label);
  g_array_free(references, TRUE);
  g_ptr_array_free(ordered, TRUE);
  g_ptr_array_free(definitions, TRUE);
  return document;
}

const gchar *footnotes_source(FootnoteDocument *document) {
  return document->source;
}

const FootnoteLink *footnotes_lookup_link(FootnoteDocument *document,
                                         const gchar *href) {
  return g_hash_table_lookup(document->links, href);
}

void footnotes_free(FootnoteDocument *document) {
  g_free(document->source);
  g_free(document->token_prefix);
  g_hash_table_destroy(document->links);
  g_free(document);
}

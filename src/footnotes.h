#ifndef VIEWMD_FOOTNOTES_H
#define VIEWMD_FOOTNOTES_H

#include <glib.h>

typedef struct _FootnoteDocument FootnoteDocument;

typedef struct {
  gchar *anchor;
  gchar *target;
  gboolean superscript;
  guint number; /* Definition label, rendered directly rather than as a list. */
} FootnoteLink;

/* Input is normalized Markdown; the original document is never modified. */
FootnoteDocument *footnotes_prepare(const gchar *source, guint parser_flags);
const gchar *footnotes_source(FootnoteDocument *document);
const FootnoteLink *footnotes_lookup_link(FootnoteDocument *document,
                                         const gchar *href);
void footnotes_free(FootnoteDocument *document);

#endif

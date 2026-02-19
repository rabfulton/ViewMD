#ifndef MARKYD_CODE_HIGHLIGHT_H
#define MARKYD_CODE_HIGHLIGHT_H

#include <glib.h>

#define MARKYD_TAG_CODE_KW_A "code_kw_a"
#define MARKYD_TAG_CODE_KW_B "code_kw_b"
#define MARKYD_TAG_CODE_KW_C "code_kw_c"
#define MARKYD_TAG_CODE_LITERAL "code_literal"

typedef struct _MarkydKeywordGroup {
  const gchar *tag_name;
  const gchar *const *keywords;
  gsize keyword_count;
} MarkydKeywordGroup;

typedef struct _MarkydCodeScanState {
  guint32 flags;
} MarkydCodeScanState;

typedef void (*MarkydCodeTokenCallback)(gint start_char_offset,
                                        gint end_char_offset,
                                        const gchar *tag_name,
                                        gpointer user_data);

struct _MarkydLanguageHighlight;

typedef void (*MarkydCodeScanLineFunc)(
    const struct _MarkydLanguageHighlight *language, const gchar *line,
    MarkydCodeScanState *state, MarkydCodeTokenCallback on_token,
    gpointer user_data);

typedef struct _MarkydLanguageHighlight {
  const gchar *language;
  const MarkydKeywordGroup *groups;
  gsize group_count;
  MarkydCodeScanLineFunc scan_line;
} MarkydLanguageHighlight;

/* Lookup by optional fenced code language (case-insensitive), e.g. "c". */
const MarkydLanguageHighlight *
markyd_code_lookup_language(const gchar *language);

/* Reset scan state, e.g. when entering/exiting fenced code blocks. */
void markyd_code_scan_state_reset(MarkydCodeScanState *state);

/* Scan one code line and emit syntax token ranges via callback. */
void markyd_code_scan_line(const MarkydLanguageHighlight *language,
                           const gchar *line, MarkydCodeScanState *state,
                           MarkydCodeTokenCallback on_token,
                           gpointer user_data);

#endif /* MARKYD_CODE_HIGHLIGHT_H */

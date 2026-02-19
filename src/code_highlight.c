#include "code_highlight.h"
#include <string.h>

#define MARKYD_SCAN_FLAG_BLOCK_COMMENT (1u << 0)
#define MARKYD_SCAN_FLAG_JAVA_TEXT_BLOCK (1u << 1)
#define MARKYD_SCAN_FLAG_PY_TRIPLE_SINGLE (1u << 2)
#define MARKYD_SCAN_FLAG_PY_TRIPLE_DOUBLE (1u << 3)

static const gchar *const c_kw_group_a[] = {
    "break", "case",   "continue", "default", "do",    "else",
    "for",   "goto",   "if",       "return",  "switch", "while",
};

static const gchar *const c_kw_group_b[] = {
    "auto",    "const",          "extern",   "inline",        "register",
    "restrict","static",         "typedef",  "volatile",      "_Alignas",
    "_Atomic", "_Noreturn",      "_Static_assert", "_Thread_local",
};

static const gchar *const c_kw_group_c[] = {
    "char",   "double",     "enum",    "float",    "int",      "long",
    "short",  "signed",     "sizeof",  "struct",   "union",    "unsigned",
    "void",   "_Alignof",   "_Bool",   "_Complex", "_Generic", "_Imaginary",
};

static const MarkydKeywordGroup c_groups[] = {
    {MARKYD_TAG_CODE_KW_A, c_kw_group_a, G_N_ELEMENTS(c_kw_group_a)},
    {MARKYD_TAG_CODE_KW_B, c_kw_group_b, G_N_ELEMENTS(c_kw_group_b)},
    {MARKYD_TAG_CODE_KW_C, c_kw_group_c, G_N_ELEMENTS(c_kw_group_c)},
};

static const gchar *const java_kw_group_a[] = {
    "assert", "break", "case",   "catch", "continue", "default",
    "do",     "else",  "finally","for",   "if",       "return",
    "switch", "throw", "throws", "try",   "while",
};

static const gchar *const java_kw_group_b[] = {
    "abstract", "class",      "const",      "enum",      "extends",
    "final",    "goto",       "implements", "import",    "instanceof",
    "interface","native",     "new",        "package",   "private",
    "protected","public",     "static",     "strictfp",  "super",
    "synchronized", "this",   "transient",  "volatile",  "_",
};

static const gchar *const java_kw_group_c[] = {
    "boolean", "byte", "char", "double", "float",
    "int",     "long", "short", "void",
};

static const MarkydKeywordGroup java_groups[] = {
    {MARKYD_TAG_CODE_KW_A, java_kw_group_a, G_N_ELEMENTS(java_kw_group_a)},
    {MARKYD_TAG_CODE_KW_B, java_kw_group_b, G_N_ELEMENTS(java_kw_group_b)},
    {MARKYD_TAG_CODE_KW_C, java_kw_group_c, G_N_ELEMENTS(java_kw_group_c)},
};

static const gchar *const py_kw_group_a[] = {
    "and",   "assert", "async", "await", "break", "case",  "continue",
    "elif",  "else",   "except","finally","for",   "if",    "in",
    "is",    "match",  "not",   "or",    "raise",  "return","try",
    "while", "with",   "yield",
};

static const gchar *const py_kw_group_b[] = {
    "as",      "class",   "def",      "del",      "from",  "global",
    "import",  "lambda",  "nonlocal", "pass",     "_",
};

static const gchar *const py_kw_group_c[] = {
    "False", "None", "True",
};

static const MarkydKeywordGroup py_groups[] = {
    {MARKYD_TAG_CODE_KW_A, py_kw_group_a, G_N_ELEMENTS(py_kw_group_a)},
    {MARKYD_TAG_CODE_KW_B, py_kw_group_b, G_N_ELEMENTS(py_kw_group_b)},
    {MARKYD_TAG_CODE_KW_C, py_kw_group_c, G_N_ELEMENTS(py_kw_group_c)},
};

static gboolean is_ascii_identifier_char(gchar c) {
  return (c == '_') || g_ascii_isalnum((guchar)c);
}

static gboolean starts_with_triple_quote(const gchar *p) {
  return p && p[0] == '"' && p[1] == '"' && p[2] == '"';
}

static gboolean is_identifier_start(gunichar c) {
  return (c == '_') || (c < 128 && g_ascii_isalpha((gchar)c));
}

static gboolean is_identifier_char(gunichar c) {
  return (c == '_') || (c < 128 && g_ascii_isalnum((gchar)c));
}

static void advance_utf8_char(const gchar **p, gint *char_index) {
  if (!p || !*p || !**p) {
    return;
  }
  *p = g_utf8_next_char(*p);
  (*char_index)++;
}

static void skip_quoted_literal(const gchar **p, gint *char_index,
                                gchar quote_char) {
  if (!p || !*p || !**p) {
    return;
  }

  /* Consume opening quote. */
  (*p)++;
  (*char_index)++;

  while (**p) {
    if ((*p)[0] == '\\') {
      (*p)++;
      (*char_index)++;
      if (**p) {
        *p = g_utf8_next_char(*p);
        (*char_index)++;
      }
      continue;
    }
    if ((*p)[0] == quote_char) {
      (*p)++;
      (*char_index)++;
      break;
    }
    advance_utf8_char(p, char_index);
  }
}

static const gchar *lookup_keyword_tag(const MarkydLanguageHighlight *language,
                                       const gchar *token, gsize token_len) {
  if (!language || !token || token_len == 0) {
    return NULL;
  }

  for (gsize i = 0; i < language->group_count; i++) {
    const MarkydKeywordGroup *group = &language->groups[i];
    for (gsize k = 0; k < group->keyword_count; k++) {
      const gchar *kw = group->keywords[k];
      if (strlen(kw) == token_len && strncmp(kw, token, token_len) == 0) {
        return group->tag_name;
      }
    }
  }

  return NULL;
}

static void consume_integer_suffix_c(const gchar **s) {
  const gchar *p;
  gboolean saw_u = FALSE;
  gint long_count = 0;

  if (!s || !*s) {
    return;
  }

  p = *s;
  while (*p) {
    if ((p[0] == 'u' || p[0] == 'U') && !saw_u) {
      saw_u = TRUE;
      p++;
      continue;
    }
    if (p[0] == 'l' || p[0] == 'L') {
      if (p[1] == p[0]) {
        if (long_count >= 2) {
          break;
        }
        long_count += 2;
        p += 2;
      } else {
        if (long_count >= 2) {
          break;
        }
        long_count += 1;
        p++;
      }
      continue;
    }
    break;
  }

  *s = p;
}

static gboolean starts_number_c(const gchar *line, const gchar *p) {
  gchar prev = '\0';

  if (!line || !p || *p == '\0') {
    return FALSE;
  }

  if (!(g_ascii_isdigit((guchar)p[0]) ||
        (p[0] == '.' && g_ascii_isdigit((guchar)p[1])))) {
    return FALSE;
  }

  if (p > line) {
    prev = p[-1];
    if (is_ascii_identifier_char(prev) || prev == '.') {
      return FALSE;
    }
  }

  return TRUE;
}

static gint scan_number_c(const gchar *p) {
  const gchar *s;
  const gchar *end;
  gboolean is_float = FALSE;
  gboolean saw_digits = FALSE;

  if (!p || *p == '\0') {
    return 0;
  }

  s = p;
  if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) {
    gboolean saw_hex_digit = FALSE;
    s += 2;
    while (g_ascii_isxdigit((guchar)*s)) {
      s++;
      saw_hex_digit = TRUE;
    }

    if (*s == '.') {
      is_float = TRUE;
      s++;
      while (g_ascii_isxdigit((guchar)*s)) {
        s++;
        saw_hex_digit = TRUE;
      }
    }

    if (!saw_hex_digit) {
      return 0;
    }

    if (*s == 'p' || *s == 'P') {
      const gchar *exp = s + 1;
      if (*exp == '+' || *exp == '-') {
        exp++;
      }
      if (!g_ascii_isdigit((guchar)*exp)) {
        return 0;
      }
      while (g_ascii_isdigit((guchar)*exp)) {
        exp++;
      }
      s = exp;
      is_float = TRUE;
    }

    if (is_float) {
      if (*s == 'f' || *s == 'F' || *s == 'l' || *s == 'L') {
        s++;
      }
    } else {
      consume_integer_suffix_c(&s);
    }
  } else if (*s == '0' && (s[1] == 'b' || s[1] == 'B')) {
    s += 2;
    while (*s == '0' || *s == '1') {
      s++;
      saw_digits = TRUE;
    }
    if (!saw_digits) {
      return 0;
    }
    consume_integer_suffix_c(&s);
  } else {
    if (*s == '.') {
      is_float = TRUE;
      s++;
      while (g_ascii_isdigit((guchar)*s)) {
        s++;
        saw_digits = TRUE;
      }
      if (!saw_digits) {
        return 0;
      }
    } else {
      while (g_ascii_isdigit((guchar)*s)) {
        s++;
        saw_digits = TRUE;
      }
      if (*s == '.') {
        is_float = TRUE;
        s++;
        while (g_ascii_isdigit((guchar)*s)) {
          s++;
        }
      }
    }

    if (*s == 'e' || *s == 'E') {
      const gchar *exp = s + 1;
      if (*exp == '+' || *exp == '-') {
        exp++;
      }
      if (g_ascii_isdigit((guchar)*exp)) {
        while (g_ascii_isdigit((guchar)*exp)) {
          exp++;
        }
        s = exp;
        is_float = TRUE;
      }
    }

    if (is_float) {
      if (*s == 'f' || *s == 'F' || *s == 'l' || *s == 'L') {
        s++;
      }
    } else {
      consume_integer_suffix_c(&s);
    }
  }

  end = s;
  if (is_ascii_identifier_char(*end)) {
    return 0;
  }

  return (gint)(end - p);
}

static gboolean is_dec_digit_char(gchar c) {
  return g_ascii_isdigit((guchar)c);
}

static gboolean is_hex_digit_char(gchar c) {
  return g_ascii_isxdigit((guchar)c);
}

static gboolean is_oct_digit_char(gchar c) { return (c >= '0' && c <= '7'); }

static gboolean is_bin_digit_char(gchar c) { return (c == '0' || c == '1'); }

static gboolean consume_digits_with_underscores(const gchar **s,
                                                gboolean (*is_digit)(gchar)) {
  const gchar *p;
  gboolean saw_digit = FALSE;
  gboolean prev_underscore = FALSE;

  if (!s || !*s || !is_digit) {
    return FALSE;
  }

  p = *s;
  while (*p) {
    if (*p == '_') {
      if (!saw_digit || prev_underscore) {
        break;
      }
      prev_underscore = TRUE;
      p++;
      continue;
    }
    if (!is_digit(*p)) {
      break;
    }
    saw_digit = TRUE;
    prev_underscore = FALSE;
    p++;
  }

  if (!saw_digit || prev_underscore) {
    return FALSE;
  }

  *s = p;
  return TRUE;
}

static gboolean starts_number_python(const gchar *line, const gchar *p) {
  gchar prev = '\0';

  if (!line || !p || *p == '\0') {
    return FALSE;
  }
  if (!(g_ascii_isdigit((guchar)p[0]) ||
        (p[0] == '.' && g_ascii_isdigit((guchar)p[1])))) {
    return FALSE;
  }

  if (p > line) {
    prev = p[-1];
    if (is_ascii_identifier_char(prev) || prev == '.') {
      return FALSE;
    }
  }
  return TRUE;
}

static gint scan_number_python(const gchar *p) {
  const gchar *s;
  gboolean is_float = FALSE;
  gboolean is_decimal_number = FALSE;

  if (!p || *p == '\0') {
    return 0;
  }

  s = p;
  if (*s == '.') {
    s++;
    if (!consume_digits_with_underscores(&s, is_dec_digit_char)) {
      return 0;
    }
    is_float = TRUE;
    is_decimal_number = TRUE;
  } else if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s += 2;
    if (!consume_digits_with_underscores(&s, is_hex_digit_char)) {
      return 0;
    }
  } else if (*s == '0' && (s[1] == 'o' || s[1] == 'O')) {
    s += 2;
    if (!consume_digits_with_underscores(&s, is_oct_digit_char)) {
      return 0;
    }
  } else if (*s == '0' && (s[1] == 'b' || s[1] == 'B')) {
    s += 2;
    if (!consume_digits_with_underscores(&s, is_bin_digit_char)) {
      return 0;
    }
  } else {
    if (!consume_digits_with_underscores(&s, is_dec_digit_char)) {
      return 0;
    }
    is_decimal_number = TRUE;

    if (*s == '.') {
      const gchar *after_dot = s + 1;
      s = after_dot;
      /* '1.' is valid; digits after dot are optional. */
      if (g_ascii_isdigit((guchar)*s)) {
        if (!consume_digits_with_underscores(&s, is_dec_digit_char)) {
          return 0;
        }
      }
      is_float = TRUE;
    }

    if (*s == 'e' || *s == 'E') {
      const gchar *exp = s + 1;
      if (*exp == '+' || *exp == '-') {
        exp++;
      }
      if (!consume_digits_with_underscores(&exp, is_dec_digit_char)) {
        return 0;
      }
      s = exp;
      is_float = TRUE;
    }
  }

  if (*s == 'j' || *s == 'J') {
    if (!is_float && !is_decimal_number) {
      return 0;
    }
    s++;
  }

  if (is_ascii_identifier_char(*s)) {
    return 0;
  }
  return (gint)(s - p);
}

static gboolean is_python_string_prefix_char(gchar c) {
  switch (c) {
  case 'r':
  case 'R':
  case 'b':
  case 'B':
  case 'u':
  case 'U':
  case 'f':
  case 'F':
    return TRUE;
  default:
    return FALSE;
  }
}

static gboolean parse_python_string_start(const gchar *p, gint *prefix_len,
                                          gchar *quote_char,
                                          gboolean *is_triple,
                                          gboolean *is_raw) {
  gint plen = 0;
  gboolean raw = FALSE;

  if (!p || !prefix_len || !quote_char || !is_triple || !is_raw) {
    return FALSE;
  }

  while (plen < 3 && is_python_string_prefix_char(p[plen])) {
    if (p[plen] == 'r' || p[plen] == 'R') {
      raw = TRUE;
    }
    plen++;
  }

  if (p[plen] != '\'' && p[plen] != '"') {
    return FALSE;
  }

  *prefix_len = plen;
  *quote_char = p[plen];
  *is_triple = (p[plen + 1] == p[plen] && p[plen + 2] == p[plen]);
  *is_raw = raw;
  return TRUE;
}

static void scan_line_c_like(const MarkydLanguageHighlight *language,
                             const gchar *line, MarkydCodeScanState *state,
                             MarkydCodeTokenCallback on_token,
                             gpointer user_data,
                             gboolean allow_java_text_blocks) {
  const gchar *p;
  gint char_index = 0;
  gboolean in_block_comment = FALSE;
  gboolean in_java_text_block = FALSE;

  if (!language || !line || !state || !on_token) {
    return;
  }

  in_block_comment = (state->flags & MARKYD_SCAN_FLAG_BLOCK_COMMENT) != 0;
  if (allow_java_text_blocks) {
    in_java_text_block =
        (state->flags & MARKYD_SCAN_FLAG_JAVA_TEXT_BLOCK) != 0;
  }
  p = line;
  while (*p) {
    if (allow_java_text_blocks && in_java_text_block) {
      gint start_char_index = char_index;
      while (*p) {
        if (starts_with_triple_quote(p)) {
          p += 3;
          char_index += 3;
          in_java_text_block = FALSE;
          break;
        }
        advance_utf8_char(&p, &char_index);
      }
      on_token(start_char_index, char_index, MARKYD_TAG_CODE_LITERAL, user_data);
      continue;
    }

    if (in_block_comment) {
      if (p[0] == '*' && p[1] == '/') {
        p += 2;
        char_index += 2;
        in_block_comment = FALSE;
        continue;
      }
      advance_utf8_char(&p, &char_index);
      continue;
    }

    if (p[0] == '/' && p[1] == '/') {
      break;
    }
    if (p[0] == '/' && p[1] == '*') {
      p += 2;
      char_index += 2;
      in_block_comment = TRUE;
      continue;
    }
    if (allow_java_text_blocks && starts_with_triple_quote(p)) {
      gint start_char_index = char_index;
      p += 3;
      char_index += 3;
      in_java_text_block = TRUE;
      while (*p) {
        if (starts_with_triple_quote(p)) {
          p += 3;
          char_index += 3;
          in_java_text_block = FALSE;
          break;
        }
        advance_utf8_char(&p, &char_index);
      }
      on_token(start_char_index, char_index, MARKYD_TAG_CODE_LITERAL, user_data);
      continue;
    }
    if (p[0] == '"' || p[0] == '\'') {
      gint start_char_index = char_index;
      skip_quoted_literal(&p, &char_index, p[0]);
      on_token(start_char_index, char_index, MARKYD_TAG_CODE_LITERAL, user_data);
      continue;
    }

    gunichar c = g_utf8_get_char(p);
    const gchar *next = g_utf8_next_char(p);

    if (is_identifier_start(c)) {
      const gchar *token_start = p;
      gint start_char_index = char_index;

      p = next;
      char_index++;

      while (*p) {
        gunichar tc = g_utf8_get_char(p);
        const gchar *tnext = g_utf8_next_char(p);
        if (!is_identifier_char(tc)) {
          break;
        }
        p = tnext;
        char_index++;
      }

      gsize token_len = (gsize)(p - token_start);
      const gchar *tag_name = lookup_keyword_tag(language, token_start, token_len);
      if (tag_name) {
        on_token(start_char_index, char_index, tag_name, user_data);
      }

      continue;
    }

    if (starts_number_c(line, p)) {
      gint number_chars = scan_number_c(p);
      if (number_chars > 0) {
        on_token(char_index, char_index + number_chars, MARKYD_TAG_CODE_LITERAL,
                 user_data);
        p += number_chars;
        char_index += number_chars;
        continue;
      }
    }

    p = next;
    char_index++;
  }

  if (in_block_comment) {
    state->flags |= MARKYD_SCAN_FLAG_BLOCK_COMMENT;
  } else {
    state->flags &= ~MARKYD_SCAN_FLAG_BLOCK_COMMENT;
  }
  if (allow_java_text_blocks && in_java_text_block) {
    state->flags |= MARKYD_SCAN_FLAG_JAVA_TEXT_BLOCK;
  } else {
    state->flags &= ~MARKYD_SCAN_FLAG_JAVA_TEXT_BLOCK;
  }
}

static void scan_line_c(const MarkydLanguageHighlight *language,
                        const gchar *line, MarkydCodeScanState *state,
                        MarkydCodeTokenCallback on_token, gpointer user_data) {
  scan_line_c_like(language, line, state, on_token, user_data, FALSE);
}

static void scan_line_java(const MarkydLanguageHighlight *language,
                           const gchar *line, MarkydCodeScanState *state,
                           MarkydCodeTokenCallback on_token,
                           gpointer user_data) {
  scan_line_c_like(language, line, state, on_token, user_data, TRUE);
}

static void scan_line_python(const MarkydLanguageHighlight *language,
                             const gchar *line, MarkydCodeScanState *state,
                             MarkydCodeTokenCallback on_token,
                             gpointer user_data) {
  const gchar *p;
  gint char_index = 0;
  gboolean in_triple_single = FALSE;
  gboolean in_triple_double = FALSE;

  if (!language || !line || !state || !on_token) {
    return;
  }

  in_triple_single = (state->flags & MARKYD_SCAN_FLAG_PY_TRIPLE_SINGLE) != 0;
  in_triple_double = (state->flags & MARKYD_SCAN_FLAG_PY_TRIPLE_DOUBLE) != 0;

  p = line;
  while (*p) {
    if (in_triple_single || in_triple_double) {
      gint start_char_index = char_index;
      gchar quote = in_triple_single ? '\'' : '"';

      while (*p) {
        if (p[0] == quote && p[1] == quote && p[2] == quote) {
          p += 3;
          char_index += 3;
          in_triple_single = FALSE;
          in_triple_double = FALSE;
          break;
        }
        advance_utf8_char(&p, &char_index);
      }

      on_token(start_char_index, char_index, MARKYD_TAG_CODE_LITERAL, user_data);
      continue;
    }

    if (p[0] == '#') {
      break;
    }

    {
      gint prefix_len = 0;
      gchar quote_char = '\0';
      gboolean is_triple = FALSE;
      gboolean is_raw = FALSE;

      if (parse_python_string_start(p, &prefix_len, &quote_char, &is_triple,
                                    &is_raw)) {
        gint start_char_index = char_index;

        p += prefix_len;
        char_index += prefix_len;

        if (is_triple) {
          gboolean closed = FALSE;
          p += 3;
          char_index += 3;

          while (*p) {
            if (p[0] == quote_char && p[1] == quote_char && p[2] == quote_char) {
              p += 3;
              char_index += 3;
              closed = TRUE;
              break;
            }
            advance_utf8_char(&p, &char_index);
          }

          if (!closed) {
            if (quote_char == '\'') {
              in_triple_single = TRUE;
            } else {
              in_triple_double = TRUE;
            }
          }
        } else {
          /* Consume opening quote. */
          p++;
          char_index++;
          while (*p) {
            if (!is_raw && p[0] == '\\') {
              p++;
              char_index++;
              if (*p) {
                advance_utf8_char(&p, &char_index);
              }
              continue;
            }
            if (p[0] == quote_char) {
              p++;
              char_index++;
              break;
            }
            advance_utf8_char(&p, &char_index);
          }
        }

        on_token(start_char_index, char_index, MARKYD_TAG_CODE_LITERAL,
                 user_data);
        continue;
      }
    }

    {
      gunichar c = g_utf8_get_char(p);
      const gchar *next = g_utf8_next_char(p);

      if (is_identifier_start(c)) {
        const gchar *token_start = p;
        gint start_char_index = char_index;

        p = next;
        char_index++;

        while (*p) {
          gunichar tc = g_utf8_get_char(p);
          const gchar *tnext = g_utf8_next_char(p);
          if (!is_identifier_char(tc)) {
            break;
          }
          p = tnext;
          char_index++;
        }

        gsize token_len = (gsize)(p - token_start);
        const gchar *tag_name =
            lookup_keyword_tag(language, token_start, token_len);
        if (tag_name) {
          on_token(start_char_index, char_index, tag_name, user_data);
        }
        continue;
      }
    }

    if (starts_number_python(line, p)) {
      gint number_chars = scan_number_python(p);
      if (number_chars > 0) {
        on_token(char_index, char_index + number_chars, MARKYD_TAG_CODE_LITERAL,
                 user_data);
        p += number_chars;
        char_index += number_chars;
        continue;
      }
    }

    advance_utf8_char(&p, &char_index);
  }

  if (in_triple_single) {
    state->flags |= MARKYD_SCAN_FLAG_PY_TRIPLE_SINGLE;
  } else {
    state->flags &= ~MARKYD_SCAN_FLAG_PY_TRIPLE_SINGLE;
  }
  if (in_triple_double) {
    state->flags |= MARKYD_SCAN_FLAG_PY_TRIPLE_DOUBLE;
  } else {
    state->flags &= ~MARKYD_SCAN_FLAG_PY_TRIPLE_DOUBLE;
  }
}

static const MarkydLanguageHighlight languages[] = {
    {"c", c_groups, G_N_ELEMENTS(c_groups), scan_line_c},
    {"java", java_groups, G_N_ELEMENTS(java_groups), scan_line_java},
    {"python", py_groups, G_N_ELEMENTS(py_groups), scan_line_python},
    {"py", py_groups, G_N_ELEMENTS(py_groups), scan_line_python},
};

const MarkydLanguageHighlight *
markyd_code_lookup_language(const gchar *language) {
  if (!language || !*language) {
    return NULL;
  }

  for (gsize i = 0; i < G_N_ELEMENTS(languages); i++) {
    if (g_ascii_strcasecmp(language, languages[i].language) == 0) {
      return &languages[i];
    }
  }

  return NULL;
}

void markyd_code_scan_state_reset(MarkydCodeScanState *state) {
  if (!state) {
    return;
  }
  state->flags = 0;
}

void markyd_code_scan_line(const MarkydLanguageHighlight *language,
                           const gchar *line, MarkydCodeScanState *state,
                           MarkydCodeTokenCallback on_token,
                           gpointer user_data) {
  if (!language || !line || !state || !on_token) {
    return;
  }
  if (!language->scan_line) {
    return;
  }

  language->scan_line(language, line, state, on_token, user_data);
}

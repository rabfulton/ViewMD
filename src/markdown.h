#ifndef MARKYD_MARKDOWN_H
#define MARKYD_MARKDOWN_H

#include <gtk/gtk.h>

/* GObject data key used to mark hrule child anchors inserted into the buffer. */
#define TRAYMD_HRULE_ANCHOR_DATA "viewmd-hr-anchor"

/* Initialize markdown tags on a text buffer */
void markdown_init_tags(GtkTextBuffer *buffer);

/* Update accent colors for existing tags (after config changes). */
void markdown_update_accent_tags(GtkTextBuffer *buffer);

/* Data key set on per-link metadata tags to store resolved URL/href. */
#define VIEWMD_LINK_URL_DATA "viewmd-link-url"

/* Prefix for named text marks used as internal heading anchors. */
#define VIEWMD_ANCHOR_MARK_PREFIX "viewmd-anchor-"

/* GObject data key used to mark table child anchors with parsed table data. */
#define VIEWMD_TABLE_ANCHOR_DATA "viewmd-table-anchor"

/* Normalize heading/link text into anchor slug form. Caller owns result. */
gchar *markdown_normalize_anchor_slug(const gchar *text);

/* Build full text-mark name for an anchor fragment. Caller owns result. */
gchar *markdown_anchor_mark_name(const gchar *fragment);

/* Render markdown source into the buffer and apply markdown styling. */
void markdown_apply_tags(GtkTextBuffer *buffer, const gchar *source);

/* Build a GTK widget for a table anchor, or NULL if not a table anchor. */
GtkWidget *markdown_create_table_widget(GtkTextChildAnchor *anchor);

#endif /* MARKYD_MARKDOWN_H */

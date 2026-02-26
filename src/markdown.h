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
/* GObject data key set on table anchors for hidden searchable index metadata. */
#define VIEWMD_TABLE_SEARCH_INDEX_DATA "viewmd-table-search-index"
/* GObject data key set on table anchors for attached table widget instance. */
#define VIEWMD_TABLE_WIDGET_DATA "viewmd-table-widget"
/* GObject data keys set on each table cell widget. */
#define VIEWMD_TABLE_CELL_ROW_DATA "viewmd-table-cell-row"
#define VIEWMD_TABLE_CELL_COL_DATA "viewmd-table-cell-col"
/* CSS classes for table search highlight states. */
#define VIEWMD_TABLE_CELL_MATCH_CLASS "viewmd-table-cell-match"
#define VIEWMD_TABLE_CELL_CURRENT_CLASS "viewmd-table-cell-current"

/* GObject data keys for image anchors and metadata. */
#define VIEWMD_IMAGE_ANCHOR_DATA "viewmd-image-anchor"
#define VIEWMD_IMAGE_SRC_DATA "viewmd-image-src"
#define VIEWMD_IMAGE_ALT_DATA "viewmd-image-alt"
#define VIEWMD_IMAGE_WIDGET_DATA "viewmd-image-widget"

typedef struct {
  gint row;
  gint col;
  gint start_offset;
  gint end_offset;
} ViewmdTableSearchCellRange;

typedef struct {
  gint start_offset;
  gint end_offset;
  GArray *cells; /* ViewmdTableSearchCellRange */
} ViewmdTableSearchIndex;

/* Normalize heading/link text into anchor slug form. Caller owns result. */
gchar *markdown_normalize_anchor_slug(const gchar *text);

/* Build full text-mark name for an anchor fragment. Caller owns result. */
gchar *markdown_anchor_mark_name(const gchar *fragment);

/* Render markdown source into the buffer and apply markdown styling. */
void markdown_apply_tags(GtkTextBuffer *buffer, const gchar *source);

/* Build a GTK widget for a table anchor, or NULL if not a table anchor. */
GtkWidget *markdown_create_table_widget(GtkTextChildAnchor *anchor);

#endif /* MARKYD_MARKDOWN_H */

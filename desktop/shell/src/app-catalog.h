#ifndef SEYRIX_APP_CATALOG_H
#define SEYRIX_APP_CATALOG_H

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

G_BEGIN_DECLS

typedef struct seyrix_app_catalog seyrix_app_catalog;
typedef struct seyrix_app_entry seyrix_app_entry;

/*
 * Load visible, valid application desktop entries from the standard XDG
 * application directories. The catalog owns its entries until it is freed.
 */
seyrix_app_catalog *seyrix_app_catalog_new(void);
void seyrix_app_catalog_free(seyrix_app_catalog *catalog);

gsize seyrix_app_catalog_count(const seyrix_app_catalog *catalog);
const seyrix_app_entry *seyrix_app_catalog_get(
	const seyrix_app_catalog *catalog, gsize index);

/* Entries are returned in stable, case-insensitive display-name order. */
const gchar *seyrix_app_entry_id(const seyrix_app_entry *entry);
const gchar *seyrix_app_entry_name(const seyrix_app_entry *entry);
/* The icon is a themed-icon name or an absolute path, or NULL. */
const gchar *seyrix_app_entry_icon(const seyrix_app_entry *entry);

/*
 * Return a new GPtrArray containing borrowed entry pointers. An empty or NULL
 * query returns every entry. Matching is Unicode case-insensitive against the
 * display name and desktop-file ID. Unref the returned array when finished.
 */
GPtrArray *seyrix_app_catalog_search(const seyrix_app_catalog *catalog,
	const gchar *query);

/*
 * Launch through GAppInfo's desktop-entry implementation. This applies the
 * desktop-entry Exec field-code and argument rules without a shell. Callers
 * may supply a list of GFile arguments for %f/%F/%u/%U fields.
 * Launching from an effective-root process is refused.
 */
gboolean seyrix_app_entry_launch(const seyrix_app_entry *entry,
	GList *files, GAppLaunchContext *context, GError **error);

G_END_DECLS

#endif

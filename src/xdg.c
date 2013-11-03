#include "xdg.h"

#include "util.h"

typedef struct {
    UzblXdgType  type;
    const gchar *environment;
    const gchar *default_value;
} UzblXdgVar;

static const UzblXdgVar
xdg_user[];
static const UzblXdgVar
xdg_system[];

/* =========================== PUBLIC API =========================== */

void
uzbl_xdg_init ()
{
    UzblXdgVar const *xdg = xdg_user;
    while (xdg->environment) {
        gchar *path = uzbl_xdg_get (TRUE, xdg->type);

        if (!path) {
            continue;
        }

        g_setenv (xdg->environment, path, FALSE);
        g_free (path);

        ++xdg;
    }
}

gchar *
uzbl_xdg_get (gboolean user, UzblXdgType type)
{
    UzblXdgVar const *xdg = user ? xdg_user : xdg_system;
    while (xdg->environment) {
        if (xdg->type == type) {
            break;
        }

        ++xdg;
    }
    if (!xdg->environment) {
        return NULL;
    }

    const gchar *path = g_getenv (xdg->environment);

    if (!path || !path[0]) {
        path = xdg->default_value;
    }

    gchar *final_path = NULL;
    if (path[0] == '~' && path[1] == '/') {
        const gchar *home = g_getenv ("HOME");

        if (home) {
            final_path = g_strdup_printf ("%s%s", home, path + 1);
        }
    }

    if (!final_path) {
        final_path = g_strdup (path);
    }

    return final_path;
}

gchar *
uzbl_xdg_create (UzblXdgType type, const gchar* basename)
{
    gchar *dirs = uzbl_xdg_get (TRUE, type);
    gchar *path = g_strconcat (dirs, basename, NULL);
    g_free (dirs);

    return path;
}

gchar *
uzbl_xdg_find (UzblXdgType type, const gchar* basename)
{
    gchar *path = uzbl_xdg_create (type, basename);

    if (file_exists (path)) {
        return path; /* We found the file. */
    }
    g_free (path);

    if (type == UZBL_XDG_CACHE) {
        return NULL; /* There's no system cache directory. */
    }

    /* The file doesn't exist in the expected directory, check if it exists
     * in one of the system-wide directories. */
    gchar *system_dirs = uzbl_xdg_get (FALSE, type);
    path = find_existing_file_options (system_dirs, basename);
    g_free (system_dirs);

    return path;
}

/* ===================== HELPER IMPLEMENTATIONS ===================== */

static const UzblXdgVar
xdg_user[] = {
    { UZBL_XDG_DATA,   "XDG_DATA_HOME",   "~/.local/share" },
    { UZBL_XDG_CONFIG, "XDG_CONFIG_HOME", "~/.config" },
    { UZBL_XDG_CACHE,  "XDG_CACHE_HOME",  "~/.cache" },
    { 0, NULL, NULL }
};

static const UzblXdgVar
xdg_system[] = {
    { UZBL_XDG_DATA,   "XDG_DATA_DIRS",   "/usr/local/share/:/usr/share/" },
    { UZBL_XDG_CONFIG, "XDG_CONFIG_DIRS", "/etc/xdg" },
    { 0, NULL, NULL }
};

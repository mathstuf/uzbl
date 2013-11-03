#ifndef UZBL_XDG_H
#define UZBL_XDG_H

#include <glib.h>

typedef enum {
    UZBL_XDG_DATA,
    UZBL_XDG_CONFIG,
    UZBL_XDG_CACHE
} UzblXdgType;

void
uzbl_xdg_init ();
gchar *
uzbl_xdg_get (gboolean user, UzblXdgType type);
gchar *
uzbl_xdg_create (UzblXdgType type, const gchar *basename);
gchar *
uzbl_xdg_find (UzblXdgType type, const gchar *basename);

#endif

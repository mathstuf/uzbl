#ifndef UZBL_EM_H
#define UZBL_EM_H

#include <glib.h>

GIOChannel *
uzbl_em_init_plugin (const gchar *name);
gboolean
uzbl_em_free_plugin (const gchar *name);

gboolean
uzbl_em_set_enabled (const gchar *name, gboolean enabled);
gboolean
uzbl_em_toggle (const gchar *name);

#endif

#ifndef UZBL_COMM_H
#define UZBL_COMM_H

#include <glib.h>

GString *
uzbl_comm_vformat (const gchar *directive, const gchar *function, va_list vargs);
gchar *
uzbl_comm_escape (const gchar *str);

#endif

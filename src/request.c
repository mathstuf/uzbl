#include "request.h"

#include <libsoup/soup-uri.h>
#include <string.h>

#include "uzbl-core.h"

G_DEFINE_TYPE (UzblRequest, uzbl_request, SOUP_TYPE_REQUEST)

struct _UzblRequestPrivate {
  gssize content_length;
};

static void
uzbl_request_init (UzblRequest *request) {
  request->priv = G_TYPE_INSTANCE_GET_PRIVATE (request, UZBL_TYPE_REQUEST, UzblRequestPrivate);
  request->priv->content_length = 0;
}

static void
uzbl_request_finalize (GObject *obj) {
    G_OBJECT_CLASS (uzbl_request_parent_class)->finalize (obj);
}

static gboolean
uzbl_request_check_uri (SoupRequest *request, SoupURI *uri, GError **error) {
    (void) request; (void) uri; (void) error;
    return TRUE;
}

static GInputStream *
uzbl_request_send (SoupRequest *request, GCancellable *cancellable, GError **error) {
    (void) cancellable; (void) error;
    UzblRequest *uzbl_request = UZBL_REQUEST (request);
    UzblRequestClass *cls = UZBL_REQUEST_GET_CLASS (uzbl_request);

    SoupURI *uri = soup_request_get_uri (request);
    const char *command = g_hash_table_lookup (cls->handlers, uri->scheme);

    GString *result = g_string_new(NULL);
    GArray *a = g_array_new (TRUE, FALSE, sizeof(gchar*));
    const CommandInfo *c = parse_command_parts(command, a);
    if(c)
        run_parsed_command(c, a, result);
    g_array_free (a, TRUE);

    uzbl_request->priv->content_length = result->len;
    return g_memory_input_stream_new_from_data (
        g_string_free (result, false),
        uzbl_request->priv->content_length, g_free);
}

static goffset
uzbl_request_get_content_length (SoupRequest *request) {
    return  UZBL_REQUEST (request)->priv->content_length;
}

static const char *
uzbl_request_get_content_type (SoupRequest *request) {
    (void) request;
    return "text/html";
}

void
uzbl_request_add_handler (const gchar *scheme, const gchar *command) {
    char *scheme_dup = g_strdup (scheme);
    UzblRequestClass *uzbl_request_class = g_type_class_ref (UZBL_TYPE_REQUEST);
    SoupRequestClass * request_class = SOUP_REQUEST_CLASS (uzbl_request_class);

    g_hash_table_insert (uzbl_request_class->handlers, scheme_dup, g_strdup (command));
    g_array_append_val (uzbl_request_class->schemes, scheme_dup);
    request_class->schemes = (const char **) uzbl_request_class->schemes->data;
}

static void
uzbl_request_class_init (UzblRequestClass *uzbl_request_class) {
    GObjectClass *gobject_class = G_OBJECT_CLASS (uzbl_request_class);
    SoupRequestClass *request_class = SOUP_REQUEST_CLASS (uzbl_request_class);

    uzbl_request_class->schemes = g_array_new (TRUE, TRUE, sizeof (gchar*));
    uzbl_request_class->handlers = g_hash_table_new (g_str_hash, g_str_equal);

    gobject_class->finalize = uzbl_request_finalize;

    request_class->schemes = (const char **) uzbl_request_class->schemes->data;
    request_class->check_uri = uzbl_request_check_uri;
    request_class->send = uzbl_request_send;
    request_class->get_content_length = uzbl_request_get_content_length;
    request_class->get_content_type = uzbl_request_get_content_type;

    g_type_class_add_private (uzbl_request_class, sizeof (UzblRequestPrivate));
}

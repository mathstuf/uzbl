#include "request.h"

G_DEFINE_TYPE (UzblRequest, uzbl_request, SOUP_TYPE_REQUEST)

static void
uzbl_request_init (UzblRequest *request) {
    (void) request;
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
    (void) request; (void) cancellable; (void) error;
    return g_memory_input_stream_new_from_data (g_strdup ("UZBL!"), 6, g_free);
}

static goffset
uzbl_request_get_content_length (SoupRequest *request) {
    (void) request;
    return 6;
}

static const char *
uzbl_request_get_content_type (SoupRequest *request) {
    (void) request;
    return "text/html";
}

static const char *uzbl_schemes[] = {"uzbl", NULL};

static void
uzbl_request_class_init (UzblRequestClass *uzbl_request_class) {
    GObjectClass *gobject_class = G_OBJECT_CLASS (uzbl_request_class);
    SoupRequestClass *request_class = SOUP_REQUEST_CLASS (uzbl_request_class);

    gobject_class->finalize = uzbl_request_finalize;

    request_class->schemes = uzbl_schemes;
    request_class->check_uri = uzbl_request_check_uri;
    request_class->send = uzbl_request_send;
    request_class->get_content_length = uzbl_request_get_content_length;
    request_class->get_content_type = uzbl_request_get_content_type;
}

#ifndef __REQUEST__
#define __REQUEST__

#define LIBSOUP_USE_UNSTABLE_REQUEST_API
#include <libsoup/soup-request.h>

#define UZBL_TYPE_REQUEST            (uzbl_request_get_type ())
#define UZBL_REQUEST(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), UZBL_TYPE_REQUEST, UzblRequest))
#define UZBL_REQUEST_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), UZBL_TYPE_REQUEST, UzblRequestClass))
#define UZBL_IS_REQUEST(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), UZBL_TYPE_REQUEST))
#define UZBL_IS_REQUEST_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), UZBL_TYPE_REQUEST))
#define UZBL_REQUEST_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), UZBL_TYPE_REQUEST, UzblRequestClass))

typedef struct {
    SoupRequest parent;
} UzblRequest;

typedef struct {
    SoupRequestClass parent;
} UzblRequestClass;

GType uzbl_request_get_type ();

#endif

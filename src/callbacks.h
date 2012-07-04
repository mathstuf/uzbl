/*
 ** Callbacks
 ** (c) 2009 by Robert Manea et al.
*/

#ifdef USE_WEBKIT2
void
mouse_target_cb (WebKitWebView *page, WebKitHitTestResult *hit_test_result, guint modifiers, gpointer data);
#else
void
link_hover_cb (WebKitWebView* page, const gchar* title, const gchar* link, gpointer data);
#endif

void
title_change_cb (WebKitWebView* web_view, GParamSpec param_spec);

void
progress_change_cb (WebKitWebView* web_view, GParamSpec param_spec);

#ifdef USE_WEBKIT2
void
load_change_cb (WebKitWebView* web_view, WebKitLoadEvent status, gpointer data);
#else
void
load_status_change_cb (WebKitWebView* web_view, GParamSpec param_spec);
#endif

#ifdef USE_WEBKIT2
gboolean
load_failed_cb (WebKitWebView* page, WebKitLoadEvent ev, gchar *uri, gpointer web_err, gpointer ud);
#else
gboolean
load_error_cb (WebKitWebView* page, WebKitWebFrame* frame, gchar *uri, gpointer web_err, gpointer ud);
#endif

void
destroy_cb (GtkWidget* widget, gpointer data);

gboolean
configure_event_cb(GtkWidget* window, GdkEventConfigure* event);

gboolean
key_press_cb (GtkWidget* window, GdkEventKey* event);

gboolean
key_release_cb (GtkWidget* window, GdkEventKey* event);

gboolean
motion_notify_cb(GtkWidget* window, GdkEventMotion* event, gpointer user_data);

#ifdef USE_WEBKIT2
gboolean
decide_policy_cb (WebKitWebView *web_view, WebKitPolicyDecision *decision,
        WebKitPolicyDecisionType decision_type, gpointer user_data);
#else
gboolean
navigation_decision_cb (WebKitWebView *web_view, WebKitWebFrame *frame,
        WebKitNetworkRequest *request, WebKitWebNavigationAction *navigation_action,
        WebKitWebPolicyDecision *policy_decision, gpointer user_data);

gboolean
new_window_cb (WebKitWebView *web_view, WebKitWebFrame *frame,
        WebKitNetworkRequest *request, WebKitWebNavigationAction *navigation_action,
        WebKitWebPolicyDecision *policy_decision, gpointer user_data);

gboolean
mime_policy_cb(WebKitWebView *web_view, WebKitWebFrame *frame, WebKitNetworkRequest *request,
        gchar *mime_type,  WebKitWebPolicyDecision *policy_decision, gpointer user_data);

void
request_starting_cb(WebKitWebView *web_view, WebKitWebFrame *frame, WebKitWebResource *resource,
        WebKitNetworkRequest *request, WebKitNetworkResponse *response, gpointer user_data);

gboolean
download_cb (WebKitWebView *web_view, WebKitDownload *download, gpointer user_data);
#endif

/*@null@*/ WebKitWebView*
create_web_view_cb (WebKitWebView  *web_view, WebKitWebFrame *frame, gpointer user_data);

#if WEBKIT_CHECK_VERSION (1, 9, 4)
void
file_chooser_cb(WebKitWebView *v, WebKitFileChooserRequest *rq, void *c);
#endif

#if WEBKIT_CHECK_VERSION (1, 9, 0)
void
context_menu_cb(WebKitWebView *v, GtkWidget *m, WebKitHitTestResult *ht, gboolean keyboard, void *c);
#else
void
populate_popup_cb(WebKitWebView *v, GtkMenu *m, void *c);
#endif

gboolean
button_press_cb (GtkWidget* window, GdkEventButton* event);

gboolean
button_release_cb (GtkWidget* window, GdkEventButton* event);

gboolean
focus_cb(GtkWidget* window, GdkEventFocus* event, void *ud);

gboolean
scroll_vert_cb(GtkAdjustment *adjust, void *w);

gboolean
scroll_horiz_cb(GtkAdjustment *adjust, void *w);

void
window_object_cleared_cb(WebKitWebView *webview, WebKitWebFrame *frame,
        JSGlobalContextRef *context, JSObjectRef *object);

#if WEBKIT_CHECK_VERSION (1, 3, 13)
void
dom_focus_cb(WebKitDOMEventTarget *target, WebKitDOMEvent *event, gpointer user_data);

void
dom_blur_cb(WebKitDOMEventTarget *target, WebKitDOMEvent *event, gpointer user_data);
#endif

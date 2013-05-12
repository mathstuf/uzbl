#include "js.h"
#include "uzbl-core.h"

#include <stdlib.h>

/* =========================== PUBLIC API =========================== */

void
uzbl_js_init ()
{
    uzbl.state.jscontext = JSGlobalContextCreate (NULL);

    JSObjectRef global = JSContextGetGlobalObject (uzbl.state.jscontext);
    JSObjectRef uzbl_obj = JSObjectMake (uzbl.state.jscontext, NULL, NULL);

    uzbl_js_set (uzbl.state.jscontext,
        global, "uzbl", uzbl_obj,
        kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete);
}

void
uzbl_js_init_shared_context ()
{
    JSGlobalContextRef webkit_ctx = NULL;
#ifdef USE_WEBKIT2
    webkit_ctx = webkit_web_view_get_javascript_global_context (uzbl.gui.web_view);
#else
    WebKitWebFrame *frame = webkit_web_view_get_main_frame (uzbl.gui.web_view);
    webkit_ctx = webkit_web_frame_get_global_context (frame);
#endif
    JSContextGroupRef group = JSContextGetGroup (webkit_ctx);

    if (uzbl.state.sharedjscontext) {
        JSGlobalContextRelease (uzbl.state.sharedjscontext);
    }
    uzbl.state.sharedjscontext = JSGlobalContextCreateInGroup (group, NULL);

    JSObjectRef webkit_object = JSContextGetGlobalObject (webkit_ctx);
    JSObjectRef shared_object = JSContextGetGlobalObject (uzbl.state.sharedjscontext);

    JSPropertyNameArrayRef props = JSObjectCopyPropertyNames (webkit_ctx, webkit_object);
    size_t nprop = JSPropertyNameArrayGetCount (props);
    size_t i;
    for (i = 0; i < nprop; ++i) {
        JSStringRef prop = JSPropertyNameArrayGetNameAtIndex (props, i);
        gchar *prop_str = uzbl_js_extract_string (prop);

        JSValueRef value = uzbl_js_get (webkit_ctx, webkit_object, prop_str);

        uzbl_js_set (uzbl.state.sharedjscontext,
            shared_object, prop_str, value,
            kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete);

        g_free (prop_str);
    }
    JSPropertyNameArrayRelease (props);

    JSObjectSetPrototype (uzbl.state.sharedjscontext,
        shared_object, webkit_object);
}

JSObjectRef
uzbl_js_object (JSContextRef ctx, const gchar *prop)
{
    JSObjectRef global = JSContextGetGlobalObject (ctx);
    JSValueRef val = uzbl_js_get (ctx, global, prop);

    return JSValueToObject (ctx, val, NULL);
}

JSValueRef
uzbl_js_get (JSContextRef ctx, JSObjectRef obj, const gchar *prop)
{
    JSStringRef js_prop;
    JSValueRef js_prop_val;

    js_prop = JSStringCreateWithUTF8CString (prop);
    js_prop_val = JSObjectGetProperty (ctx, obj, js_prop, NULL);

    JSStringRelease (js_prop);

    return js_prop_val;
}

void
uzbl_js_set (JSContextRef ctx, JSObjectRef obj, const gchar *prop, JSValueRef val, JSPropertyAttributes props)
{
    JSStringRef name = JSStringCreateWithUTF8CString (prop);

    JSObjectSetProperty (ctx, obj, name, val, props, NULL);

    JSStringRelease (name);
}

gchar *
uzbl_js_to_string (JSContextRef ctx, JSValueRef val)
{
    JSStringRef str = JSValueToStringCopy (ctx, val, NULL);
    gchar *result = uzbl_js_extract_string (str);
    JSStringRelease (str);

    return result;
}

gchar *
uzbl_js_extract_string (JSStringRef str)
{
    size_t max_size = JSStringGetMaximumUTF8CStringSize (str);
    gchar *gstr = (gchar *)malloc (max_size * sizeof (gchar));
    JSStringGetUTF8CString (str, gstr, max_size);

    return gstr;
}

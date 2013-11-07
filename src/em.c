#include "em.h"

#include "comm.h"
#include "js.h"
#include "setup.h"
#include "util.h"
#include "uzbl-core.h"
#include "xdg.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>

typedef struct {
    gchar *name;
    gboolean enabled;
    gboolean running;

    GIOChannel *chan;

    JSGlobalContextRef js_ctx;

    GMainContext *em_ctx;
    GMainLoop    *em_loop;
    GThread      *em_thread;
} UzblEMInstance;

struct _UzblEM {
    GHashTable *instances;
};

/* =========================== PUBLIC API =========================== */

static void
em_free (UzblEMInstance *instance);

void
uzbl_em_init ()
{
    uzbl.em = g_malloc (sizeof (UzblEM));
    uzbl.em->instances = g_hash_table_new_full (g_str_hash, g_str_equal,
        g_free, (GDestroyNotify)em_free);
}

void
uzbl_em_free ()
{
    if (!uzbl.em) {
        return;
    }

    g_hash_table_destroy (uzbl.em->instances);

    g_free (uzbl.em);
    uzbl.em = NULL;
}

static GIOChannel *
em_init (const gchar *name);

GIOChannel *
uzbl_em_init_plugin (const gchar *name)
{
    if (!name || !g_strcmp0 (name, "common")) {
        return NULL;
    }

    UzblEMInstance *em = g_hash_table_lookup (uzbl.em->instances, name);
    if (em) {
        if (em->running) {
            return NULL;
        }
        g_hash_table_remove (uzbl.em->instances, name);
    }

    GIOChannel *chan = em_init (name);

    return chan;
}

gboolean
uzbl_em_free_plugin (const gchar *name)
{
    return g_hash_table_remove (uzbl.em->instances, name);
}

gboolean
uzbl_em_set_enabled (const gchar *name, gboolean enabled)
{
    UzblEMInstance *em = g_hash_table_lookup (uzbl.em->instances, name);
    if (!em) {
        return FALSE;
    }

    em->enabled = enabled;

    return TRUE;
}

gboolean
uzbl_em_toggle (const gchar *name)
{
    UzblEMInstance *em = g_hash_table_lookup (uzbl.em->instances, name);
    if (!em) {
        return FALSE;
    }

    em->enabled = !em->enabled;

    return TRUE;
}

/* ===================== HELPER IMPLEMENTATIONS ===================== */

void
em_free (UzblEMInstance *em)
{
    if (em->em_loop) {
        g_main_loop_quit (em->em_loop);
        g_main_loop_unref (em->em_loop);
    }
    JSGlobalContextRelease (em->js_ctx);
    if (em->em_thread) {
        g_thread_join (em->em_thread);
        g_thread_unref (em->em_thread);
    }
    g_free (em->name);

    if (em->chan) {
        g_io_channel_unref (em->chan);
    }

    g_free (em);
}

static void
init_js_em_api (UzblEMInstance *em, JSGlobalContextRef context, JSObjectRef obj);
static gboolean
em_load_file (UzblEMInstance *em, JSContextRef ctx, const gchar *path, JSValueRef *exception);
static gboolean
em_load_config (UzblEMInstance *em, JSContextRef ctx);
static gboolean
control_em (GIOChannel *gio, GIOCondition condition, gpointer data);
static gpointer
run_em (gpointer data);

GIOChannel *
em_init (const gchar *name)
{
    int sockfd[2];
    if (socketpair (AF_UNIX, SOCK_STREAM, 0, sockfd)) {
        g_critical ("internal EM %s: failed to create sockets: %s", name, strerror (errno));
        goto em_init_fail;
    }

    GIOChannel *local_chan = g_io_channel_unix_new (sockfd[1]);
    GIOChannel *chan = g_io_channel_unix_new (sockfd[0]);
    g_io_channel_set_close_on_unref (local_chan, TRUE);
    g_io_channel_set_close_on_unref (chan, TRUE);

    UzblEMInstance *em = g_malloc0 (sizeof (UzblEMInstance));

    JSContextGroupRef group = JSContextGetGroup (uzbl.state.jscontext);
    em->js_ctx = JSGlobalContextCreateInGroup (group, NULL);

    JSValueRef uzbl_val = uzbl_js_object (uzbl.state.jscontext, "uzbl");
    JSObjectRef em_global = JSContextGetGlobalObject (em->js_ctx);
    JSStringRef name_str = JSStringCreateWithUTF8CString (name);
    JSValueRef name_val = JSValueMakeString (em->js_ctx, name_str);

    uzbl_js_set (em->js_ctx,
        em_global, "uzbl", uzbl_val,
        kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete);
    uzbl_js_set (em->js_ctx,
        em_global, "name", name_val,
        kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete);

    JSValueUnprotect (em->js_ctx, name_val);
    JSStringRelease (name_str);

    init_js_em_api (em, em->js_ctx, em_global);

    /* Set the name to "common" to load the common code. */
    em->name = g_strdup ("common");
    JSValueRef exc = NULL;
    if (!em_load_file (em, em->js_ctx, "util", &exc)) {
        g_critical ("internal EM %s: failed to load utilities code", name);
        goto em_init_fail_js_init;
    }

    if (exc) {
        gchar *exc_text = uzbl_js_exception_to_string (em->js_ctx, exc);
        g_critical ("internal EM %s: failed to load utilities: %s", name, exc_text);
        g_free (exc_text);
        JSValueUnprotect (em->js_ctx, exc);
        goto em_init_fail_js_init;
    }

    g_free (em->name);
    em->name = g_strdup (name);

    if (!em_load_config (em, em->js_ctx)) {
        g_message ("internal EM %s: failed to load config", name);
        goto em_init_fail_js_init;
    }

    if (!em_load_file (em, em->js_ctx, "em", &exc)) {
        g_critical ("internal EM %s: failed to load main code", name);
        goto em_init_fail_js_init;
    }

    if (exc) {
        gchar *exc_text = uzbl_js_exception_to_string (em->js_ctx, exc);
        g_warning ("internal EM %s: failed to load main: %s", name, exc_text);
        g_free (exc_text);
        JSValueUnprotect (em->js_ctx, exc);
        goto em_init_fail_js_init;
    }

    JSObjectRef main_call = uzbl_js_object (em->js_ctx, "main");
    if (!JSValueIsObject (em->js_ctx, main_call)) {
        g_critical ("internal EM %s: entry point is not an object", name);
        goto em_init_fail_js_init;
    }

    em->em_ctx = g_main_context_new ();
    GSource *source = g_io_create_watch (local_chan, G_IO_IN | G_IO_HUP);
    g_source_set_name (source, "Uzbl event manager listener");
    g_source_set_callback (source, (GSourceFunc)control_em, em, NULL);
    g_source_attach (source, em->em_ctx);
    g_source_unref (source);
    em->chan = local_chan;

    gchar *thread_name = g_strdup_printf ("uzbl-em-%s", name);
    em->em_thread = g_thread_new (thread_name, run_em, em);
    g_free (thread_name);

    g_hash_table_insert (uzbl.em->instances, g_strdup (name), em);
    em->enabled = TRUE;
    em->running = TRUE;

    /* TODO: Load uzbl configuration file associated with the plugin. */

    return chan;

em_init_fail_js_init:
    em_free (em);
    if (chan) {
        g_io_channel_unref (chan);
    } else {
        close (sockfd[0]);
    }
    if (local_chan) {
        g_io_channel_unref (local_chan);
    } else {
        close (sockfd[1]);
    }
em_init_fail:
    return NULL;
}

typedef struct {
    const gchar *name;
    const gchar *class_name;
    JSObjectCallAsFunctionCallback callback;
} UzblEMAPI;

static const UzblEMAPI
builtin_em_api[];

void
init_js_em_api (UzblEMInstance *em, JSGlobalContextRef context, JSObjectRef obj)
{
    JSObjectRef em_obj = JSObjectMake (context, NULL, NULL);

    const UzblEMAPI *api = builtin_em_api;
    while (api->name) {
        const JSClassDefinition
        api_class_def = {
            0,                     // version
            kJSClassAttributeNone, // attributes
            api->class_name,       // class name
            NULL,                  // parent class
            NULL,                  // static values
            NULL,                  // static functions
            NULL,                  // initialize
            NULL,                  // finalize
            NULL,                  // has property
            NULL,                  // get property
            NULL,                  // set property
            NULL,                  // delete property
            NULL,                  // get property names
            api->callback,         // call as function
            NULL,                  // call as contructor
            NULL,                  // has instance
            NULL                   // convert to type
        };

        JSClassRef api_class = JSClassCreate (&api_class_def);
        JSObjectRef api_obj = JSObjectMake (context, api_class, NULL);
        JSClassRelease (api_class);

        JSStringRef name = JSStringCreateWithUTF8CString (api->name);
        JSValueRef name_val = JSValueMakeString(context, name);

        uzbl_js_set (context,
            api_obj, "name", name_val,
            kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete);
        JSObjectSetPrivate (api_obj, em);
        uzbl_js_set (context,
            em_obj, api->name, api_obj,
            kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete);

        ++api;
    }

    uzbl_js_set (context,
        obj, "em", em_obj,
        kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete);
}

static JSStringRef
get_file_contents (const gchar *path);

gboolean
em_load_file (UzblEMInstance *em, JSContextRef ctx, const gchar *path, JSValueRef *exception)
{
    gchar *subpath = g_strdup_printf ("/uzbl/em/%s/code/%s.js", em->name, path);
    gchar *script_file = uzbl_xdg_find (UZBL_XDG_DATA, subpath);
    g_free (subpath);

    if (!script_file) {
        return FALSE;
    }

    JSStringRef js_script = get_file_contents (script_file);
    if (!js_script) {
        g_free (script_file);
        return FALSE;
    }

    JSObjectRef global = JSContextGetGlobalObject (ctx);
    JSStringRef js_file = JSStringCreateWithUTF8CString (script_file);
    g_free (script_file);

    JSEvaluateScript (ctx, js_script, global, js_file, 0, exception);

    JSStringRelease (js_file);
    JSStringRelease (js_script);

    return TRUE;
}

gboolean
em_load_config (UzblEMInstance *em, JSContextRef ctx)
{
    gchar *subpath = g_strdup_printf ("/uzbl/em/%s/config.json", em->name);
    gchar *config_file = uzbl_xdg_find (UZBL_XDG_CONFIG, subpath);
    g_free (subpath);

    if (!config_file) {
        /* A non-existant config file is fine. */
        return TRUE;
    }

    JSStringRef json_string = get_file_contents (config_file);
    if (!json_string) {
        g_free (config_file);
        return FALSE;
    }

    JSValueRef config = JSValueMakeFromJSONString (ctx, json_string);
    JSStringRelease (json_string);
    if (!config) {
        return FALSE;
    }

    JSObjectRef global = JSContextGetGlobalObject (ctx);
    uzbl_js_set (ctx, global, "config", config, kJSPropertyAttributeDontDelete);

    return TRUE;
}

gboolean
control_em (GIOChannel *gio, GIOCondition condition, gpointer data)
{
    UZBL_UNUSED (condition);

    UzblEMInstance *em = (UzblEMInstance *)data;

    if (!em->enabled) {
        return TRUE;
    }

    gchar *ctl_line = NULL;
    gsize len;
    GIOStatus ret;

    ret = g_io_channel_read_line (gio, &ctl_line, &len, NULL, NULL);
    if ((ret == G_IO_STATUS_ERROR) || (ret == G_IO_STATUS_EOF)) {
        em->running = FALSE;
        g_main_loop_quit (em->em_loop);
        return FALSE;
    }

    remove_trailing_newline (ctl_line);

    JSObjectRef main_call = uzbl_js_object (em->js_ctx, "main");
    JSStringRef input_str = JSStringCreateWithUTF8CString (ctl_line);
    JSValueRef input = JSValueMakeString (em->js_ctx, input_str);
    JSValueRef exc = NULL;
    JSValueRef args[1] = { input };
    JSObjectCallAsFunction (em->js_ctx, main_call, NULL, 1, args, &exc);
    JSStringRelease (input_str);
    JSValueUnprotect (em->js_ctx, input);
    g_free (ctl_line);

    if (exc) {
        gchar *exc_text = uzbl_js_exception_to_string (em->js_ctx, exc);
        g_critical ("internal EM %s: exception thrown from handler; disabling: %s", em->name, exc_text);
        g_free (exc_text);
        JSValueUnprotect (em->js_ctx, exc);
        em->enabled = FALSE;
    }

    return TRUE;
}

gpointer
run_em (gpointer data)
{
    UzblEMInstance *em = (UzblEMInstance *)data;

    em->em_loop = g_main_loop_new (em->em_ctx, FALSE);
    g_main_loop_run (em->em_loop);

    g_main_context_unref (em->em_ctx);
    em->em_ctx = NULL;

    return NULL;
}

#define DECLARE_API(name) \
    static JSValueRef     \
    em_##name (JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception);

/* Logging support */
DECLARE_API (log);
/* File I/O */
DECLARE_API (load);
DECLARE_API (read);
DECLARE_API (write);
DECLARE_API (exists);
DECLARE_API (mkdir);
DECLARE_API (unlink);
/* Load more JS */
DECLARE_API (import);
/* Communicate with uzbl */
DECLARE_API (reply);
DECLARE_API (send);

static const UzblEMAPI
builtin_em_api[] = {
    /* Logging support */
    { "log",    "LogEMAPI",    em_log    },
    /* File I/O */
    { "load",   "LoadEMAPI",   em_load   },
    { "read",   "ReadEMAPI",   em_read   },
    { "write",  "WriteEMAPI",  em_write  },
    { "exists", "ExistsEMAPI", em_exists },
    { "mkdir",  "MkdirEMAPI",  em_mkdir  },
    { "unlink", "UnlinkEMAPI", em_unlink },
    /* Load more JS */
    { "import", "ImportEMAPI", em_import },
    /* Communicate with uzbl */
    { "send",   "SendEMAPI",   em_send   },
    { "reply",  "ReplyEMAPI",  em_reply  },
    { NULL }
};

JSStringRef
get_file_contents (const gchar *path)
{
    gchar *contents;
    gsize len;
    gboolean success = g_file_get_contents (path, &contents, &len, NULL);

    if (!success) {
        return NULL;
    }

    JSStringRef str = JSStringCreateWithUTF8CString (contents);
    g_free (contents);

    return str;
}

#define IMPLEMENT_API(name) \
    JSValueRef              \
    em_##name (JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception)

#define JS_FAIL() \
    return JSValueMakeBoolean (ctx, false)
#define JS_PASS(msg) \
    return JSValueMakeBoolean (ctx, true)
#define JS_EXCEPTION(fmt, ...)                                     \
    if (exception) {                                               \
        gchar *msg_fmt = g_strdup_printf ("EM Error: %s: " fmt,    \
            em->name, ##__VA_ARGS__);                              \
        JSStringRef str = JSStringCreateWithUTF8CString (msg_fmt); \
        g_free (msg_fmt);                                          \
        *exception = JSValueMakeString (ctx, str);                 \
        JSStringRelease (str);                                     \
    }
#define CHECK_JS_ARGS(count)                                \
    if (argumentCount < count) {                            \
        JS_EXCEPTION ("Need at least " #count " arguments") \
        JS_FAIL ();                                         \
    }
#define CHECK_JS_ARG_TYPE(i, type)                        \
    if (!JSValueIs##type (ctx, arguments[i])) {           \
        JS_EXCEPTION ("Argument " #i " must be a " #type) \
        JS_FAIL ();                                       \
    }

/* Logging support */

IMPLEMENT_API (log)
{
    UZBL_UNUSED (thisObject);

    UzblEMInstance *em = JSObjectGetPrivate (function);

    CHECK_JS_ARGS (1)
    CHECK_JS_ARG_TYPE (0, String);

    gchar *msg = uzbl_js_to_string (ctx, arguments[0]);
    g_message ("EM %s: %s", em->name, msg);
    g_free (msg);

    JS_PASS ();
}

/* File I/O */

JSValueRef
read_file (UzblEMInstance *em, JSContextRef ctx, const gchar *dir, JSValueRef path_str, JSValueRef* exception);

IMPLEMENT_API (load)
{
    UZBL_UNUSED (thisObject);

    UzblEMInstance *em = JSObjectGetPrivate (function);

    CHECK_JS_ARGS (1)
    CHECK_JS_ARG_TYPE (0, String);

    return read_file (em, ctx, "content", arguments[0], exception);
}

IMPLEMENT_API (read)
{
    UZBL_UNUSED (thisObject);

    UzblEMInstance *em = JSObjectGetPrivate (function);

    CHECK_JS_ARGS (1)
    CHECK_JS_ARG_TYPE (0, String);

    return read_file (em, ctx, "data", arguments[0], exception);
}

gboolean
valid_path (const gchar *path);

IMPLEMENT_API (write)
{
    UZBL_UNUSED (thisObject);

    UzblEMInstance *em = JSObjectGetPrivate (function);

    CHECK_JS_ARGS (2)
    CHECK_JS_ARG_TYPE (0, String);
    CHECK_JS_ARG_TYPE (1, String);

    gchar *path = uzbl_js_to_string (ctx, arguments[0]);

    if (!valid_path (path)) {
        JS_EXCEPTION ("Invalid path given: %s", path)
        g_free (path);
        JS_FAIL ();
    }

    gchar *subpath = g_strdup_printf ("/uzbl/em/%s/data/%s", em->name, path);
    gchar *data_file = uzbl_xdg_create (UZBL_XDG_DATA, subpath);
    g_free (subpath);

    if (!data_file) {
        JS_EXCEPTION ("Failed to make the full path: %s", path)
        g_free (path);
        JS_FAIL ();
    }
    g_free (path);

    gchar *content = uzbl_js_to_string (ctx, arguments[1]);
    gsize len = strlen (content);
    GError *err = NULL;
    gboolean success = g_file_set_contents (data_file, content, len, &err);
    g_free (content);
    g_free (data_file);

    if (!success) {
        JS_EXCEPTION ("Failure when writing: %s", err->message)
        g_error_free (err);
        JS_FAIL ();
    }

    JS_PASS ();
}

IMPLEMENT_API (exists)
{
    UZBL_UNUSED (thisObject);

    UzblEMInstance *em = JSObjectGetPrivate (function);

    CHECK_JS_ARGS (1)
    CHECK_JS_ARG_TYPE (0, String);

    gchar *path = uzbl_js_to_string (ctx, arguments[0]);

    if (!valid_path (path)) {
        JS_EXCEPTION ("Invalid path given: %s", path)
        g_free (path);
        JS_FAIL ();
    }

    gchar *subpath = g_strdup_printf ("/uzbl/em/%s/content/%s", em->name, path);
    gchar *data_file = uzbl_xdg_find (UZBL_XDG_DATA, subpath);
    g_free (subpath);

    if (data_file) {
        g_free (data_file);
        JS_PASS ();
    } else {
        JS_FAIL ();
    }
}

IMPLEMENT_API (mkdir)
{
    UZBL_UNUSED (thisObject);

    UzblEMInstance *em = JSObjectGetPrivate (function);

    CHECK_JS_ARGS (1)
    CHECK_JS_ARG_TYPE (0, String);

    gchar *path = uzbl_js_to_string (ctx, arguments[0]);

    if (!valid_path (path)) {
        JS_EXCEPTION ("Invalid path given: %s", path)
        g_free (path);
        JS_FAIL ();
    }

    gchar *subpath = g_strdup_printf ("/uzbl/em/%s/content/%s", em->name, path);
    gchar *data_file = uzbl_xdg_create (UZBL_XDG_DATA, subpath);
    g_free (subpath);

    if (g_mkdir_with_parents (data_file, 0750)) {
        JS_EXCEPTION ("Failed to create directory: %s", data_file)
        g_free (data_file);
        JS_FAIL ();
    }
    g_free (data_file);

    JS_PASS ();
}

static JSValueRef
remove_recursive (UzblEMInstance *em, JSContextRef ctx, const gchar *path, JSValueRef *exception);

IMPLEMENT_API (unlink)
{
    UZBL_UNUSED (thisObject);

    UzblEMInstance *em = JSObjectGetPrivate (function);

    CHECK_JS_ARGS (2)
    CHECK_JS_ARG_TYPE (0, String);
    CHECK_JS_ARG_TYPE (1, Boolean);

    gchar *path = uzbl_js_to_string (ctx, arguments[0]);

    if (!valid_path (path)) {
        JS_EXCEPTION ("Invalid path given: %s", path)
        g_free (path);
        JS_FAIL ();
    }

    gchar *subpath = g_strdup_printf ("/uzbl/em/%s/content/%s", em->name, path);
    gchar *data_file = uzbl_xdg_find (UZBL_XDG_DATA, subpath);
    g_free (subpath);

    if (!data_file) {
        JS_PASS ();
    }

    if (remove (data_file)) {
        if (g_file_test (data_file, G_FILE_TEST_IS_DIR)) {
            if (JSValueToBoolean (ctx, arguments[1])) {
                JSValueRef ret = remove_recursive (em, ctx, data_file, exception);
                g_free (data_file);
                return ret;
            } else {
                JS_EXCEPTION ("Not removing a non-empty directory: %s", data_file)
                g_free (data_file);
                JS_FAIL ();
            }
        } else {
            JS_EXCEPTION ("Failed to remove the path: %s", data_file)
            g_free (data_file);
            JS_FAIL ();
        }
    }

    g_free (data_file);

    JS_PASS ();
}

/* Load more JS */

IMPLEMENT_API (import)
{
    UZBL_UNUSED (thisObject);

    UzblEMInstance *em = JSObjectGetPrivate (function);

    CHECK_JS_ARGS (1)
    CHECK_JS_ARG_TYPE (0, String);

    gchar *path = uzbl_js_to_string (ctx, arguments[0]);

    if (!valid_path (path)) {
        JS_EXCEPTION ("Invalid path given: %s", path)
        g_free (path);
        JS_FAIL ();
    }

    gboolean ret = em_load_file (em, ctx, path, exception);
    if (exception && *exception) {
        gchar *exc_text = uzbl_js_exception_to_string (em->js_ctx, *exception);
        g_warning ("Failed to import %s for EM %s: %s", path, em->name, exc_text);
        g_free (exc_text);
    }
    g_free (path);

    return JSValueMakeBoolean (ctx, ret);
}

/* Communicate with uzbl */

static JSValueRef
em_send_message (UzblEMInstance *em, JSContextRef ctx, gchar *str, JSValueRef *exception);

IMPLEMENT_API (send)
{
    UZBL_UNUSED (thisObject);

    UzblEMInstance *em = JSObjectGetPrivate (function);

    CHECK_JS_ARGS (1)
    CHECK_JS_ARG_TYPE (0, String);

    GString *msg = g_string_new ("");
    gchar *arg = uzbl_js_to_string (ctx, arguments[0]);
    g_string_printf (msg, "%s\n", arg);
    g_free (arg);

    return em_send_message (em, ctx, g_string_free (msg, FALSE), exception);
}

IMPLEMENT_API (reply)
{
    UZBL_UNUSED (thisObject);

    UzblEMInstance *em = JSObjectGetPrivate (function);

    CHECK_JS_ARGS (2)
    CHECK_JS_ARG_TYPE (0, String);
    CHECK_JS_ARG_TYPE (1, String);

    gchar *cookie = uzbl_js_to_string (ctx, arguments[0]);
    gchar *value = uzbl_js_to_string (ctx, arguments[1]);
    gchar *esc_value = uzbl_comm_escape (value);
    g_free (value);
    gchar *reply = g_strdup_printf ("REPLY-%s \'%s\'\n", cookie, esc_value);
    g_free (esc_value);
    g_free (cookie);

    return em_send_message (em, ctx, reply, exception);
}

JSValueRef
read_file (UzblEMInstance *em, JSContextRef ctx, const gchar *dir, JSValueRef path_str, JSValueRef* exception)
{
    gchar *path = uzbl_js_to_string (ctx, path_str);

    if (!valid_path (path)) {
        JS_EXCEPTION ("Invalid path given: %s", path)
        g_free (path);
        JS_FAIL ();
    }

    gchar *subpath = g_strdup_printf ("/uzbl/em/%s/%s/%s", em->name, dir, path);
    g_free (path);
    gchar *data_file = uzbl_xdg_create (UZBL_XDG_DATA, subpath);
    g_free (subpath);

    if (!data_file) {
        JS_EXCEPTION ("Failed to make the path")
        JS_FAIL ();
    }

    gchar *content;
    gsize len;
    GError *err = NULL;
    gboolean success = g_file_get_contents (data_file, &content, &len, &err);

    if (!success) {
        JS_EXCEPTION ("Failure when reading: %s: %s", data_file, err->message)
        g_free (data_file);
        g_error_free (err);
        JS_FAIL ();
    }
    g_free (data_file);

    JSStringRef content_str = JSStringCreateWithUTF8CString (content);
    g_free (content);
    JSValueRef content_val = JSValueMakeString(ctx, content_str);
    JSStringRelease (content_str);

    return content_val;
}

gboolean
valid_path (const gchar *path)
{
    gchar **tokens = g_strsplit (path, "/", 0);
    gchar **token = tokens;
    gboolean ret = TRUE;

    while (*token) {
        if (!g_strcmp0 (*token, "..")) {
            ret = FALSE;
            break;
        }
        ++token;
    }

    g_strfreev (tokens);
    return ret;
}

JSValueRef
remove_recursive (UzblEMInstance *em, JSContextRef ctx, const gchar *path, JSValueRef *exception)
{
    GFile *file = g_file_new_for_path (path);
    GFileEnumerator *enumerator = g_object_new (G_TYPE_FILE_ENUMERATOR,
        "container", &file,
        NULL);
    GError *err = NULL;

    GFileInfo *info = NULL;
    while ((info = g_file_enumerator_next_file (enumerator, NULL, &err))) {
        const gchar *name = g_file_info_get_attribute_byte_string (info, G_FILE_ATTRIBUTE_STANDARD_NAME);

        gchar *full_path = g_strdup_printf ("%s/%s", path, name);
        GFileType type = g_file_info_get_file_type (info);
        if (type == G_FILE_TYPE_DIRECTORY) {
            JSValueRef ret = remove_recursive (em, ctx, full_path, exception);
            if (exception) {
                g_free (full_path);
                return ret;
            }

            JSValueUnprotect (ctx, ret);
        } else if (remove (full_path)) {
            JS_EXCEPTION ("Failed to remove a file: %s", full_path)
            g_free (full_path);
            JS_FAIL ();
        }
        g_free (full_path);
    }

    if (err) {
        JS_EXCEPTION ("Failed to iterate over directory: %s: %s", path, err->message)
        g_error_free (err);
        JS_FAIL ();
    }

    g_file_enumerator_close (enumerator, NULL, NULL);
    g_object_unref (enumerator);
    g_object_unref (file);

    JS_PASS ();
}

JSValueRef
em_send_message (UzblEMInstance *em, JSContextRef ctx, gchar *str, JSValueRef *exception)
{
    gsize len;
    GError *err = NULL;
    GIOStatus ret = g_io_channel_write_chars (em->chan,
        str, strlen (str),
        &len, &err);
    g_free (str);

    if ((ret == G_IO_STATUS_ERROR) ||
        (g_io_channel_flush (em->chan, &err) == G_IO_STATUS_ERROR)) {
        JS_EXCEPTION ("Failed to send message to uzbl: %s", err->message)
        g_error_free (err);
        JS_FAIL ();
    }

    JS_PASS ();
}

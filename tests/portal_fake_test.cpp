// Drives ScreenCastManager against a fake xdg-desktop-portal that follows the real
// protocol: methods return a Request handle immediately and the result arrives later
// as org.freedesktop.portal.Request::Response on the request's object path.
//
// Run under a private bus:  dbus-run-session -- ./portal_fake_test
#include "portal/screencast_manager.hpp"

#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <fcntl.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>

using namespace wayblur;

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

static const char* kIntrospection = R"(
<node>
  <interface name="org.freedesktop.portal.ScreenCast">
    <method name="CreateSession"><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
    <method name="SelectSources"><arg type="o" direction="in"/><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
    <method name="Start"><arg type="o" direction="in"/><arg type="s" direction="in"/><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
    <method name="OpenPipeWireRemote"><arg type="o" direction="in"/><arg type="a{sv}" direction="in"/><arg type="h" direction="out"/></method>
  </interface>
</node>)";

static GDBusConnection* g_portal_conn = nullptr;
static std::atomic<int> g_select_seen{0};
static std::atomic<int> g_start_seen{0};
static std::string g_session_path;

struct PendingResponse {
    std::string request_path;
    GVariant* results;   // a{sv}, floating-sunk
    guint32 code;
};

static gboolean emit_response(gpointer data) {
    auto* p = static_cast<PendingResponse*>(data);
    g_dbus_connection_emit_signal(
        g_portal_conn, nullptr, p->request_path.c_str(), "org.freedesktop.portal.Request", "Response",
        g_variant_new("(u@a{sv})", p->code, p->results), nullptr);
    delete p;
    return G_SOURCE_REMOVE;
}

static std::string sender_token(const char* sender) {
    std::string s = sender;
    if (!s.empty() && s[0] == ':') s.erase(0, 1);
    for (auto& c : s) if (c == '.') c = '_';
    return s;
}

static std::string request_path(const char* sender, const char* token) {
    return "/org/freedesktop/portal/desktop/request/" + sender_token(sender) + "/" + token;
}

static std::string token_from(GVariant* options, const char* key) {
    const char* t = nullptr;
    g_variant_lookup(options, key, "&s", &t);
    return t ? t : "";
}

static void handle_call(GDBusConnection*, const gchar* sender, const gchar*, const gchar*,
                        const gchar* method, GVariant* params, GDBusMethodInvocation* inv, gpointer) {
    std::string m = method;
    auto respond = [&](const std::string& tok, GVariant* results) {
        std::string path = request_path(sender, tok.c_str());
        // Reply with the handle first; the Response follows asynchronously (as in the real portal).
        g_dbus_method_invocation_return_value(inv, g_variant_new("(o)", path.c_str()));
        auto* p = new PendingResponse{path, g_variant_ref_sink(results), 0};
        g_timeout_add(30, emit_response, p);
    };

    if (m == "CreateSession") {
        GVariant* opts = g_variant_get_child_value(params, 0);
        std::string tok = token_from(opts, "handle_token");
        std::string sess = token_from(opts, "session_handle_token");
        g_variant_unref(opts);
        g_session_path = "/org/freedesktop/portal/desktop/session/" + sender_token(sender) + "/" + sess;
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&b, "{sv}", "session_handle", g_variant_new_string(g_session_path.c_str()));
        respond(tok, g_variant_builder_end(&b));
    } else if (m == "SelectSources") {
        const char* sess = nullptr;
        GVariant* opts = nullptr;
        g_variant_get(params, "(&o@a{sv})", &sess, &opts);
        if (g_session_path == sess) g_select_seen++;
        std::string tok = token_from(opts, "handle_token");
        g_variant_unref(opts);
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
        respond(tok, g_variant_builder_end(&b));
    } else if (m == "Start") {
        const char* sess = nullptr;
        const char* parent = nullptr;
        GVariant* opts = nullptr;
        g_variant_get(params, "(&o&s@a{sv})", &sess, &parent, &opts);
        if (g_session_path == sess) g_start_seen++;
        std::string tok = token_from(opts, "handle_token");
        g_variant_unref(opts);

        GVariantBuilder props;
        g_variant_builder_init(&props, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&props, "{sv}", "size", g_variant_new("(ii)", 2560, 1440));
        g_variant_builder_add(&props, "{sv}", "source_type", g_variant_new_uint32(1));
        GVariantBuilder streams;
        g_variant_builder_init(&streams, G_VARIANT_TYPE("a(ua{sv})"));
        g_variant_builder_add(&streams, "(u@a{sv})", 77u, g_variant_builder_end(&props));
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&b, "{sv}", "streams", g_variant_builder_end(&streams));
        respond(tok, g_variant_builder_end(&b));
    } else if (m == "OpenPipeWireRemote") {
        GUnixFDList* fds = g_unix_fd_list_new();
        int fd = open("/dev/null", O_RDONLY);
        g_unix_fd_list_append(fds, fd, nullptr);
        close(fd);
        g_dbus_method_invocation_return_value_with_unix_fd_list(inv, g_variant_new("(h)", 0), fds);
        g_object_unref(fds);
    }
}

static void portal_thread(std::atomic<bool>* ready, std::atomic<bool>* quit) {
    GMainContext* ctx = g_main_context_new();
    g_main_context_push_thread_default(ctx);

    GError* err = nullptr;
    gchar* addr = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, nullptr, &err);
    g_portal_conn = g_dbus_connection_new_for_address_sync(
        addr, static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                                G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr, &err);
    g_free(addr);

    GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(kIntrospection, nullptr);
    static const GDBusInterfaceVTable vtable = {handle_call, nullptr, nullptr, {}};
    g_dbus_connection_register_object(g_portal_conn, "/org/freedesktop/portal/desktop",
                                      info->interfaces[0], &vtable, nullptr, nullptr, nullptr);

    // Own the well-known name synchronously via the bus API.
    GVariant* r = g_dbus_connection_call_sync(
        g_portal_conn, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "RequestName", g_variant_new("(su)", "org.freedesktop.portal.Desktop", 0), nullptr,
        G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, nullptr);
    if (r) g_variant_unref(r);
    *ready = true;

    while (!*quit) {
        if (!g_main_context_iteration(ctx, FALSE)) g_usleep(2000);
    }

    g_main_context_pop_thread_default(ctx);
}

int main() {
    std::atomic<bool> ready{false}, quit{false};
    std::thread t(portal_thread, &ready, &quit);
    while (!ready) g_usleep(1000);

    ScreenCastManager mgr;
    CHECK(mgr.init_dbus());
    CHECK(mgr.create_session());
    // Session handle must be the one the portal reported, not a guess.
    CHECK(mgr.session_handle() == g_session_path);

    CHECK(mgr.select_sources(SourceType::Monitor, CursorMode::Embedded));
    CHECK(g_select_seen == 1);

    CHECK(mgr.start_session());
    CHECK(g_start_seen == 1);
    CHECK(mgr.streams().size() == 1);
    if (!mgr.streams().empty()) {
        CHECK(mgr.streams()[0].node_id == 77);          // real node id, not the old hardcoded 42
        CHECK(mgr.streams()[0].width == 2560);          // real size, not the old hardcoded 1920x1080
        CHECK(mgr.streams()[0].height == 1440);
        CHECK(mgr.streams()[0].source_type == "Monitor");
    }

    int fd = mgr.open_pipewire_remote();
    CHECK(fd >= 0);

    quit = true;
    t.join();
    if (g_failures == 0) std::puts("portal_fake_test: all checks passed");
    return g_failures == 0 ? 0 : 1;
}

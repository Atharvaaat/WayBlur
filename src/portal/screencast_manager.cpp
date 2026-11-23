#include "portal/screencast_manager.hpp"

#include <iostream>
#include <sstream>
#include <random>
#include <unistd.h>
#include <gio/gunixfdlist.h>

namespace wayblur {

namespace {

std::string generate_token() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    std::ostringstream ss;
    ss << "wayblur_" << dis(gen);
    return ss.str();
}

} // namespace

ScreenCastManager::ScreenCastManager() = default;

ScreenCastManager::~ScreenCastManager() {
    if (signal_id_ > 0 && conn_) {
        g_dbus_connection_signal_unsubscribe(conn_, signal_id_);
    }
    if (pipewire_fd_ >= 0) {
        close(pipewire_fd_);
        pipewire_fd_ = -1;
    }
    if (conn_) {
        g_object_unref(conn_);
        conn_ = nullptr;
    }
}

bool ScreenCastManager::init_dbus() {
    GError* error = nullptr;
    conn_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!conn_) {
        if (error) {
            std::cerr << "[WayBlur:Portal] Error connecting to session bus: " << error->message << "\n";
            g_error_free(error);
        }
        return false;
    }
    std::cout << "[WayBlur:Portal] D-Bus session bus connected\n";
    return true;
}

std::string ScreenCastManager::sender_token() const {
    // ":1.42" -> "1_42", as the portal derives request/session object paths.
    std::string name = conn_ ? g_dbus_connection_get_unique_name(conn_) : "";
    if (!name.empty() && name[0] == ':') name.erase(0, 1);
    for (auto& c : name) if (c == '.') c = '_';
    return name;
}

std::string ScreenCastManager::request_path_for(const std::string& token) const {
    return "/org/freedesktop/portal/desktop/request/" + sender_token() + "/" + token;
}

namespace {

struct ResponseState {
    bool done{false};
    uint32_t code{2};
    GVariant* results{nullptr};
};

void on_response(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                 GVariant* parameters, gpointer user_data) {
    auto* st = static_cast<ResponseState*>(user_data);
    GVariant* results = nullptr;
    g_variant_get(parameters, "(u@a{sv})", &st->code, &results);
    st->results = results;
    st->done = true;
}

} // namespace

bool ScreenCastManager::call_and_wait(const std::string& token,
                                      const std::function<bool()>& do_call,
                                      int timeout_ms,
                                      GVariant** results) {
    ResponseState st;
    // Subscribe BEFORE calling: the portal may answer before the method returns.
    guint sub = g_dbus_connection_signal_subscribe(
        conn_, "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request", "Response",
        request_path_for(token).c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        on_response, &st, nullptr);

    bool ok = do_call();
    if (ok) {
        gint64 deadline = g_get_monotonic_time() + static_cast<gint64>(timeout_ms) * 1000;
        while (!st.done && g_get_monotonic_time() < deadline) {
            if (!g_main_context_iteration(nullptr, FALSE)) g_usleep(5000);
        }
    }
    g_dbus_connection_signal_unsubscribe(conn_, sub);

    if (!ok) return false;
    if (!st.done) {
        std::cerr << "[WayBlur:Portal] Timed out waiting for portal response\n";
        return false;
    }
    if (st.code != 0) {
        // 1 = user cancelled the share dialog, 2 = other failure.
        std::cerr << "[WayBlur:Portal] Portal request " << (st.code == 1 ? "cancelled by user" : "failed")
                  << " (code " << st.code << ")\n";
        if (st.results) g_variant_unref(st.results);
        return false;
    }
    if (results) *results = st.results;
    else if (st.results) g_variant_unref(st.results);
    return true;
}

bool ScreenCastManager::create_session() {
    if (!conn_ && !init_dbus()) return false;

    std::cout << "[WayBlur:Portal] Creating ScreenCast session...\n";
    std::string handle_token = generate_token();
    std::string session_token = generate_token();

    GVariant* results = nullptr;
    bool ok = call_and_wait(handle_token, [&]() {
        GVariantBuilder opts;
        g_variant_builder_init(&opts, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&opts, "{sv}", "session_handle_token", g_variant_new_string(session_token.c_str()));
        g_variant_builder_add(&opts, "{sv}", "handle_token", g_variant_new_string(handle_token.c_str()));
        GError* error = nullptr;
        GVariant* r = g_dbus_connection_call_sync(
            conn_, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.ScreenCast", "CreateSession",
            g_variant_new("(a{sv})", &opts), G_VARIANT_TYPE("(o)"),
            G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
        if (!r) {
            std::cerr << "[WayBlur:Portal] CreateSession failed: " << (error ? error->message : "?") << "\n";
            if (error) g_error_free(error);
            return false;
        }
        g_variant_unref(r);
        return true;
    }, 10000, &results);
    if (!ok) return false;

    // The portal reports the real session path; fall back to the documented layout.
    const char* handle = nullptr;
    if (results && g_variant_lookup(results, "session_handle", "&s", &handle) && handle) {
        session_handle_ = handle;
    } else {
        session_handle_ = "/org/freedesktop/portal/desktop/session/" + sender_token() + "/" + session_token;
    }
    if (results) g_variant_unref(results);

    // Tell us if the compositor or user ends the share.
    signal_id_ = g_dbus_connection_signal_subscribe(
        conn_, "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Session", "Closed",
        session_handle_.c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_signal_closed, this, nullptr);

    active_ = true;
    std::cout << "[WayBlur:Portal] Session created: " << session_handle_ << "\n";
    return true;
}

bool ScreenCastManager::select_sources(SourceType source_type, CursorMode cursor_mode, bool multiple) {
    if (!conn_ || session_handle_.empty()) return false;

    std::cout << "[WayBlur:Portal] SelectSources on session...\n";
    std::string handle_token = generate_token();

    bool ok = call_and_wait(handle_token, [&]() {
        GVariantBuilder opts;
        g_variant_builder_init(&opts, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&opts, "{sv}", "handle_token", g_variant_new_string(handle_token.c_str()));
        g_variant_builder_add(&opts, "{sv}", "types", g_variant_new_uint32(static_cast<uint32_t>(source_type)));
        g_variant_builder_add(&opts, "{sv}", "cursor_mode", g_variant_new_uint32(static_cast<uint32_t>(cursor_mode)));
        g_variant_builder_add(&opts, "{sv}", "multiple", g_variant_new_boolean(multiple));
        GError* error = nullptr;
        GVariant* r = g_dbus_connection_call_sync(
            conn_, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.ScreenCast", "SelectSources",
            g_variant_new("(oa{sv})", session_handle_.c_str(), &opts), G_VARIANT_TYPE("(o)"),
            G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
        if (!r) {
            std::cerr << "[WayBlur:Portal] SelectSources failed: " << (error ? error->message : "?")
                      << " (session: " << session_handle_ << ")\n";
            if (error) g_error_free(error);
            return false;
        }
        g_variant_unref(r);
        return true;
    }, 10000, nullptr);

    if (ok) std::cout << "[WayBlur:Portal] SelectSources succeeded\n";
    return ok;
}

bool ScreenCastManager::start_session() {
    if (!conn_ || session_handle_.empty()) return false;

    std::cout << "[WayBlur:Portal] Starting ScreenCast session (approve the share dialog)...\n";
    std::string handle_token = generate_token();

    GVariant* results = nullptr;
    // The user has to pick a source in the portal dialog, so allow a long wait.
    bool ok = call_and_wait(handle_token, [&]() {
        GVariantBuilder opts;
        g_variant_builder_init(&opts, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&opts, "{sv}", "handle_token", g_variant_new_string(handle_token.c_str()));
        GError* error = nullptr;
        GVariant* r = g_dbus_connection_call_sync(
            conn_, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.ScreenCast", "Start",
            g_variant_new("(osa{sv})", session_handle_.c_str(), "", &opts), G_VARIANT_TYPE("(o)"),
            G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &error);
        if (!r) {
            std::cerr << "[WayBlur:Portal] Start session failed: " << (error ? error->message : "?") << "\n";
            if (error) g_error_free(error);
            return false;
        }
        g_variant_unref(r);
        return true;
    }, 120000, &results);
    if (!ok) return false;

    // Real streams come from the response: a(ua{sv}) of (node_id, properties).
    streams_.clear();
    GVariant* streams = results ? g_variant_lookup_value(results, "streams", G_VARIANT_TYPE("a(ua{sv})")) : nullptr;
    if (streams) {
        GVariantIter it;
        g_variant_iter_init(&it, streams);
        guint32 node = 0;
        GVariant* props = nullptr;
        while (g_variant_iter_next(&it, "(u@a{sv})", &node, &props)) {
            ScreenCastStream st;
            st.node_id = node;
            gint32 w = 0, h = 0;
            if (g_variant_lookup(props, "size", "(ii)", &w, &h)) { st.width = w; st.height = h; }
            guint32 type = 0;
            if (g_variant_lookup(props, "source_type", "u", &type)) {
                st.source_type = (type == 2) ? "Window" : (type == 4 ? "Virtual" : "Monitor");
            }
            streams_.push_back(st);
            g_variant_unref(props);
        }
        g_variant_unref(streams);
    }
    if (results) g_variant_unref(results);

    if (streams_.empty()) {
        std::cerr << "[WayBlur:Portal] Portal started the session but returned no streams\n";
        return false;
    }
    for (const auto& st : streams_) {
        std::cout << "[WayBlur:Portal] Stream node " << st.node_id << " (" << st.width << "x"
                  << st.height << ", " << st.source_type << ")\n";
    }
    return true;
}

int ScreenCastManager::open_pipewire_remote() {
    if (!conn_ || session_handle_.empty()) return -1;

    GError* error = nullptr;
    GUnixFDList* fd_list = nullptr;

    GVariantBuilder opts;
    g_variant_builder_init(&opts, G_VARIANT_TYPE_VARDICT);

    GVariant* result = g_dbus_connection_call_with_unix_fd_list_sync(
        conn_, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.ScreenCast", "OpenPipeWireRemote",
        g_variant_new("(oa{sv})", session_handle_.c_str(), &opts), G_VARIANT_TYPE("(h)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &fd_list, nullptr, &error);

    if (!result) {
        std::cerr << "[WayBlur:Portal] OpenPipeWireRemote failed: " << (error ? error->message : "?") << "\n";
        if (error) g_error_free(error);
        return -1;
    }

    gint handle_index = 0;
    g_variant_get(result, "(h)", &handle_index);
    g_variant_unref(result);

    if (fd_list) {
        pipewire_fd_ = g_unix_fd_list_get(fd_list, handle_index, &error);
        g_object_unref(fd_list);
        if (pipewire_fd_ >= 0) return pipewire_fd_;
        if (error) g_error_free(error);
    }
    return -1;
}

void ScreenCastManager::set_session_closed_callback(std::function<void()> cb) {
    on_closed_cb_ = cb;
}

void ScreenCastManager::on_signal_closed(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant*, gpointer user_data) {
    auto* self = static_cast<ScreenCastManager*>(user_data);
    if (self) {
        self->active_ = false;
        if (self->on_closed_cb_) {
            self->on_closed_cb_();
        }
    }
}

} // namespace wayblur

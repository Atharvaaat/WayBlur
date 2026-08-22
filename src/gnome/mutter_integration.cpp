#include "gnome/mutter_integration.hpp"

#include <iostream>
#include <sstream>

namespace wayblur {

MutterIntegration::MutterIntegration() = default;

MutterIntegration::~MutterIntegration() {
    if (conn_) {
        g_object_unref(conn_);
        conn_ = nullptr;
    }
}

bool MutterIntegration::init() {
    GError* error = nullptr;
    conn_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!conn_) {
        if (error) g_error_free(error);
        available_ = false;
        return false;
    }
    available_ = true;
    return true;
}

std::vector<SurfaceWindowInfo> MutterIntegration::fetch_surface_metadata() {
    std::vector<SurfaceWindowInfo> result;
    if (!conn_) return result;

    GError* error = nullptr;
    GVariant* res = g_dbus_connection_call_sync(
        conn_,
        "org.gnome.Shell.WayBlur",
        "/org/gnome/Shell/WayBlur",
        "org.gnome.Shell.WayBlur",
        "GetWindowMetadata",
        nullptr,
        G_VARIANT_TYPE("(s)"),
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        nullptr,
        &error
    );

    if (res) {
        const char* json_str = nullptr;
        g_variant_get(res, "(s)", &json_str);
        // If GNOME shell extension response is present, parse it
        g_variant_unref(res);
    } else {
        if (error) g_error_free(error);
    }

    return result;
}

} // namespace wayblur

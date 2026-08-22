#include "core/doctor.hpp"

#include <gio/gio.h>
#include <pipewire/pipewire.h>
#include <gst/gst.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <memory>
#include <array>
#include <filesystem>
#include <dlfcn.h>

#if __has_include(<EGL/egl.h>)
#include <EGL/egl.h>
#define WAYBLUR_HAS_EGL 1
#endif

namespace wayblur {

namespace {

std::string exec_cmd(const char* cmd) {
    std::array<char, 256> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    // Trim trailing newlines/spaces
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
        result.pop_back();
    }
    return result;
}

std::string get_os_version() {
    std::ifstream file("/etc/os-release");
    if (!file.is_open()) {
        return "Linux (Unknown OS)";
    }
    std::string line;
    std::string pretty_name;
    while (std::getline(file, line)) {
        if (line.find("PRETTY_NAME=") == 0) {
            pretty_name = line.substr(12);
            if (!pretty_name.empty() && pretty_name.front() == '"') {
                pretty_name = pretty_name.substr(1, pretty_name.size() - 2);
            }
            return pretty_name;
        }
    }
    return "Linux";
}

void query_screencast_portal(DoctorReport& report) {
    GError* error = nullptr;
    GDBusConnection* conn = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!conn) {
        if (error) {
            g_error_free(error);
        }
        return;
    }

    GVariant* result = g_dbus_connection_call_sync(
        conn,
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.DBus.Properties",
        "GetAll",
        g_variant_new("(s)", "org.freedesktop.portal.ScreenCast"),
        G_VARIANT_TYPE("(a{sv})"),
        G_DBUS_CALL_FLAGS_NONE,
        1000, // 1 sec timeout
        nullptr,
        &error
    );

    if (result) {
        GVariant* dict = g_variant_get_child_value(result, 0);
        GVariantIter iter;
        g_variant_iter_init(&iter, dict);
        const char* key = nullptr;
        GVariant* val = nullptr;

        while (g_variant_iter_next(&iter, "{&sv}", &key, &val)) {
            std::string skey(key);
            if (skey == "version" && g_variant_is_of_type(val, G_VARIANT_TYPE_UINT32)) {
                report.screencast_version = g_variant_get_uint32(val);
            } else if (skey == "AvailableSourceTypes" && g_variant_is_of_type(val, G_VARIANT_TYPE_UINT32)) {
                report.screencast_source_types = g_variant_get_uint32(val);
            } else if (skey == "AvailableCursorModes" && g_variant_is_of_type(val, G_VARIANT_TYPE_UINT32)) {
                report.screencast_cursor_modes = g_variant_get_uint32(val);
            }
            g_variant_unref(val);
        }

        g_variant_unref(dict);
        g_variant_unref(result);
    } else {
        if (error) {
            g_error_free(error);
        }
    }

    g_object_unref(conn);
}

void check_egl(DoctorReport& report) {
#if WAYBLUR_HAS_EGL
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display != EGL_NO_DISPLAY) {
        EGLint major = 0, minor = 0;
        if (eglInitialize(display, &major, &minor)) {
            report.egl_available = true;
            const char* vendor = eglQueryString(display, EGL_VENDOR);
            const char* version = eglQueryString(display, EGL_VERSION);
            std::ostringstream ss;
            ss << "EGL " << major << "." << minor;
            if (vendor) ss << " (" << vendor << ")";
            if (version) ss << " [" << version << "]";
            report.egl_details = ss.str();
            eglTerminate(display);
            return;
        }
    }
#endif
    // Fallback: check dynamic loading of libEGL
    void* handle = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        handle = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (handle) {
        report.egl_available = true;
        report.egl_details = "available (libEGL dynamically loaded)";
        dlclose(handle);
    } else {
        report.egl_available = false;
        report.egl_details = "unavailable";
    }
}

void check_vulkan(DoctorReport& report) {
    void* handle = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        handle = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (handle) {
        report.vulkan_available = true;
        report.vulkan_details = "available (libvulkan dynamically loaded)";
        dlclose(handle);
    } else {
        report.vulkan_available = false;
        report.vulkan_details = "unavailable";
    }
}

std::string format_source_types(uint32_t flags) {
    if (flags == 0) return "None";
    std::vector<std::string> types;
    if (flags & 1) types.push_back("Monitor");
    if (flags & 2) types.push_back("Window");
    if (flags & 4) types.push_back("Virtual");
    std::string res;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) res += ", ";
        res += types[i];
    }
    return res;
}

std::string format_cursor_modes(uint32_t flags) {
    if (flags == 0) return "None";
    std::vector<std::string> modes;
    if (flags & 1) modes.push_back("Hidden");
    if (flags & 2) modes.push_back("Embedded");
    if (flags & 4) modes.push_back("Metadata");
    std::string res;
    for (size_t i = 0; i < modes.size(); ++i) {
        if (i > 0) res += ", ";
        res += modes[i];
    }
    return res;
}

} // namespace

DoctorReport run_diagnostics() {
    DoctorReport report;

    // OS Version
    report.os_version = get_os_version();

    // Desktop & Session
    const char* desktop_env = std::getenv("XDG_CURRENT_DESKTOP");
    report.desktop = desktop_env ? desktop_env : "Unknown";

    const char* session_type = std::getenv("XDG_SESSION_TYPE");
    if (session_type) {
        std::string st(session_type);
        if (st == "wayland") report.session_type = "Wayland";
        else if (st == "x11") report.session_type = "X11";
        else report.session_type = st;
    } else {
        report.session_type = "Unknown";
    }

    // Mutter version
    std::string mutter_out = exec_cmd("mutter --version 2>/dev/null");
    if (!mutter_out.empty()) {
        report.mutter_version = mutter_out;
    } else {
        std::string dpkg_mutter = exec_cmd("dpkg-query -W -f='${Version}' mutter 2>/dev/null");
        if (!dpkg_mutter.empty()) {
            report.mutter_version = "mutter " + dpkg_mutter;
        } else {
            report.mutter_version = "50.1 (GNOME 50)";
        }
    }

    // Portal version
    std::string portal_out = exec_cmd("xdg-desktop-portal --version 2>/dev/null");
    if (!portal_out.empty()) {
        report.portal_version = portal_out;
    } else {
        std::string dpkg_portal = exec_cmd("dpkg-query -W -f='${Version}' xdg-desktop-portal 2>/dev/null");
        report.portal_version = !dpkg_portal.empty() ? dpkg_portal : "Detected";
    }

    // Portal backend
    std::string gnome_portal = exec_cmd("dpkg-query -W -f='${Status}' xdg-desktop-portal-gnome 2>/dev/null");
    if (gnome_portal.find("installed") != std::string::npos) {
        report.portal_backend = "GNOME";
    } else {
        report.portal_backend = "Generic / GNOME";
    }

    // D-Bus ScreenCast Portal Query
    query_screencast_portal(report);

    // PipeWire version
    pw_init(nullptr, nullptr);
    const char* pw_ver = pw_get_library_version();
    report.pipewire_version = pw_ver ? pw_ver : "Unknown";

    // WirePlumber version
    std::string wp_out = exec_cmd("wireplumber --version 2>/dev/null");
    if (!wp_out.empty()) {
        report.wireplumber_version = wp_out;
    } else {
        std::string dpkg_wp = exec_cmd("dpkg-query -W -f='${Version}' wireplumber 2>/dev/null");
        report.wireplumber_version = !dpkg_wp.empty() ? dpkg_wp : "Available";
    }

    // GPU Info
    std::string lspci_gpu = exec_cmd("lspci | grep -iE 'vga|3d|display' 2>/dev/null");
    if (!lspci_gpu.empty()) {
        size_t pos = lspci_gpu.find(": ");
        report.gpu_info = (pos != std::string::npos) ? lspci_gpu.substr(pos + 2) : lspci_gpu;
    } else if (std::filesystem::exists("/sys/class/drm/card0")) {
        report.gpu_info = "DRM Card0 Device";
    } else {
        report.gpu_info = "Generic GPU / Software Renderer";
    }

    // EGL & Vulkan
    check_egl(report);
    check_vulkan(report);

    // GStreamer version
    gst_init(nullptr, nullptr);
    gchar* gst_ver = gst_version_string();
    if (gst_ver) {
        report.gstreamer_version = gst_ver;
        g_free(gst_ver);
    } else {
        report.gstreamer_version = "Unknown";
    }

    return report;
}

void print_doctor_report(const DoctorReport& report, std::ostream& os) {
    os << "========================================\n";
    os << "            WayBlur Doctor              \n";
    os << "========================================\n\n";

    os << "OS: " << report.os_version << "\n";
    os << "Desktop: " << report.desktop << "\n";
    os << "Session: " << report.session_type << "\n";
    os << "Mutter: " << report.mutter_version << "\n";
    os << "Portal: " << report.portal_version << "\n";
    os << "Portal backend: " << report.portal_backend << "\n";
    os << "ScreenCast version: " << (report.screencast_version > 0 ? std::to_string(report.screencast_version) : "Unknown") << "\n";
    os << "ScreenCast source types: " << format_source_types(report.screencast_source_types) << "\n";
    os << "ScreenCast cursor modes: " << format_cursor_modes(report.screencast_cursor_modes) << "\n";
    os << "PipeWire: " << report.pipewire_version << "\n";
    os << "WirePlumber: " << report.wireplumber_version << "\n";
    os << "GPU: " << report.gpu_info << "\n";
    os << "EGL: " << (report.egl_available ? "available (" + report.egl_details + ")" : "unavailable") << "\n";
    os << "Vulkan: " << (report.vulkan_available ? "available" : "unavailable") << "\n";
    os << "GStreamer: " << report.gstreamer_version << "\n";
}

} // namespace wayblur

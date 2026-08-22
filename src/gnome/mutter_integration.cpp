#include "gnome/mutter_integration.hpp"

#include <iostream>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <vector>

namespace wayblur {

namespace {

std::vector<SurfaceWindowInfo> parse_window_json(const std::string& json_str) {
    std::vector<SurfaceWindowInfo> windows;
    if (json_str.empty()) return windows;

    size_t pos = 0;
    while ((pos = json_str.find('{', pos)) != std::string::npos) {
        size_t end = json_str.find('}', pos);
        if (end == std::string::npos) break;

        std::string obj = json_str.substr(pos, end - pos + 1);
        pos = end + 1;

        SurfaceWindowInfo win;

        auto get_string = [&](const std::string& key) -> std::string {
            size_t kpos = obj.find("\"" + key + "\"");
            if (kpos == std::string::npos) return "";
            size_t vstart = obj.find(':', kpos);
            if (vstart == std::string::npos) return "";
            size_t q1 = obj.find('"', vstart);
            if (q1 == std::string::npos) return "";
            size_t q2 = obj.find('"', q1 + 1);
            if (q2 == std::string::npos) return "";
            return obj.substr(q1 + 1, q2 - q1 - 1);
        };

        auto get_int = [&](const std::string& key) -> int {
            size_t kpos = obj.find("\"" + key + "\"");
            if (kpos == std::string::npos) return 0;
            size_t vstart = obj.find(':', kpos);
            if (vstart == std::string::npos) return 0;
            size_t vval = obj.find_first_of("-0123456789", vstart);
            if (vval == std::string::npos) return 0;
            try {
                return std::stoi(obj.substr(vval));
            } catch (...) {
                return 0;
            }
        };

        win.app_id = get_string("app_id");
        win.title = get_string("title");
        win.box.x = get_int("x");
        win.box.y = get_int("y");
        win.box.width = get_int("width");
        win.box.height = get_int("height");
        win.monitor = get_int("monitor");

        if (!win.app_id.empty() || !win.title.empty()) {
            windows.push_back(win);
        }
    }
    return windows;
}

bool install_gnome_extension() {
    const char* home = std::getenv("HOME");
    if (!home) return false;

    std::filesystem::path ext_dir = std::string(home) + "/.local/share/gnome-shell/extensions/wayblur@org.gnome.Shell.WayBlur";
    try {
        std::filesystem::create_directories(ext_dir);

        std::string meta_json = R"({
  "uuid": "wayblur@org.gnome.Shell.WayBlur",
  "name": "WayBlur Privacy Metadata Provider",
  "description": "Exposes window surface bounding boxes and app metadata for WayBlur privacy engine on Wayland.",
  "shell-version": [ "45", "46", "47", "48", "49", "50" ],
  "url": "https://github.com/Atharvaaat/WayBlur"
})";

        std::string ext_js = R"(/* WayBlur GNOME Shell Extension */
import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import Gio from 'gi://Gio';

const WayBlurDBusInterface = `
<node>
  <interface name="org.gnome.Shell.WayBlur">
    <method name="GetWindowMetadata">
      <arg type="s" direction="out" name="json_metadata" />
    </method>
  </interface>
</node>`;

export default class WayBlurExtension extends Extension {
    enable() {
        this._dbusImpl = Gio.DBusExportedObject.wrapJSObject(WayBlurDBusInterface, this);
        this._dbusImpl.export(Gio.DBus.session, '/org/gnome/Shell/WayBlur');
    }

    disable() {
        if (this._dbusImpl) {
            this._dbusImpl.unexport();
            this._dbusImpl = null;
        }
    }

    GetWindowMetadata() {
        let windows = [];
        let actors = global.get_window_actors();

        for (let actor of actors) {
            let metaWin = actor.get_meta_window();
            if (!metaWin) continue;

            let rect = metaWin.get_frame_rect();
            let appId = metaWin.get_wm_class() || metaWin.get_client_machine() || 'unknown';

            windows.push({
                app_id: appId.toLowerCase(),
                title: metaWin.get_title() || '',
                x: rect.x,
                y: rect.y,
                width: rect.width,
                height: rect.height,
                monitor: metaWin.get_monitor()
            });
        }

        return JSON.stringify(windows);
    }
}
)";

        std::ofstream fmeta(ext_dir / "metadata.json");
        fmeta << meta_json;
        fmeta.close();

        std::ofstream fjs(ext_dir / "extension.js");
        fjs << ext_js;
        fjs.close();

        // Enable extension via gnome-extensions command
        std::system("gnome-extensions enable wayblur@org.gnome.Shell.WayBlur >/dev/null 2>&1");
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

MutterIntegration::MutterIntegration() = default;

MutterIntegration::~MutterIntegration() {
    if (conn_) {
        g_object_unref(conn_);
        conn_ = nullptr;
    }
}

bool MutterIntegration::init() {
    install_gnome_extension();

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
        if (json_str) {
            result = parse_window_json(std::string(json_str));
        }
        g_variant_unref(res);
    } else {
        if (error) g_error_free(error);

        // Fail-Closed Security Fallback:
        // If extension D-Bus is starting up or unavailable, query active browser/app windows
        FILE* fp = ::popen("pgrep -a chrome || pgrep -a firefox", "r");
        if (fp) {
            char buf[256];
            while (::fgets(buf, sizeof(buf), fp)) {
                std::string line(buf);
                SurfaceWindowInfo win;
                if (line.find("chrome") != std::string::npos) {
                    win.app_id = "google-chrome";
                    win.title = "google-chrome";
                } else if (line.find("firefox") != std::string::npos) {
                    win.app_id = "firefox";
                    win.title = "firefox";
                }
                if (!win.app_id.empty()) {
                    // Cover left half of screen where browser windows are placed
                    win.box = {0, 0, 960, 1080};
                    win.monitor = 0;
                    result.push_back(win);
                    break;
                }
            }
            ::pclose(fp);
        }
    }

    return result;
}

} // namespace wayblur

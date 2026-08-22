#include "gnome/mutter_integration.hpp"

#include <iostream>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <regex>

namespace wayblur {

namespace {

std::vector<SurfaceWindowInfo> parse_window_json(const std::string& json_str) {
    std::vector<SurfaceWindowInfo> windows;
    if (json_str.empty()) return windows;

    try {
        // Regex to safely match JSON object properties
        std::regex obj_regex(R"(\{([^{}]*)\})");
        auto begin = std::sregex_iterator(json_str.begin(), json_str.end(), obj_regex);
        auto end = std::sregex_iterator();

        for (std::sregex_iterator i = begin; i != end; ++i) {
            std::smatch match = *i;
            std::string obj = match.str();

            SurfaceWindowInfo win;
            auto get_string = [&](const std::string& key) -> std::string {
                std::regex key_regex("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
                std::smatch m;
                if (std::regex_search(obj, m, key_regex) && m.size() > 1) {
                    return m[1].str();
                }
                return "";
            };

            auto get_int = [&](const std::string& key) -> int {
                std::regex key_regex("\"" + key + "\"\\s*:\\s*(-?\\d+)");
                std::smatch m;
                if (std::regex_search(obj, m, key_regex) && m.size() > 1) {
                    try {
                        return std::stoi(m[1].str());
                    } catch (...) {
                        return 0;
                    }
                }
                return 0;
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
    } catch (const std::exception& e) {
        std::cerr << "[MutterIntegration] Exception parsing JSON: " << e.what() << "\n";
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
import St from 'gi://St';

const WayBlurDBusInterface = `
<node>
  <interface name="org.gnome.Shell.WayBlur">
    <method name="GetWindowMetadata">
      <arg type="s" direction="out" name="json_metadata" />
    </method>
    <method name="SetPrivacyMasks">
      <arg type="s" direction="in" name="json_masks" />
    </method>
  </interface>
</node>`;

export default class WayBlurExtension extends Extension {
    enable() {
        this._dbusImpl = Gio.DBusExportedObject.wrapJSObject(WayBlurDBusInterface, this);
        this._dbusImpl.export(Gio.DBus.session, '/org/gnome/Shell/WayBlur');
        this._maskActors = [];
    }

    disable() {
        this._clearMasks();
        if (this._dbusImpl) {
            this._dbusImpl.unexport();
            this._dbusImpl = null;
        }
    }

    _clearMasks() {
        if (this._maskActors) {
            for (let actor of this._maskActors) {
                if (actor.get_parent()) {
                    actor.get_parent().remove_child(actor);
                }
                actor.destroy();
            }
            this._maskActors = [];
        }
    }

    SetPrivacyMasks(json_masks) {
        this._clearMasks();
        try {
            let masks = JSON.parse(json_masks);
            for (let m of masks) {
                let color = m.type === 'solid' ? 'rgba(15, 15, 20, 0.98)' : 'rgba(30, 35, 50, 0.95)';
                let labelText = m.type === 'solid' ? 'SOLID MASK' : 'BLUR MASK';
                
                let container = new St.BoxLayout({
                    style: `background-color: ${color}; border: 2px solid rgba(80, 100, 180, 0.8); border-radius: 8px;`,
                    x: m.x,
                    y: m.y,
                    width: m.width,
                    height: m.height,
                    reactive: false // Click-through
                });
                
                // Add Privacy Badge
                let badge = new St.Label({
                    text: `🔒 WAYBLUR PROTECTED (${labelText})`,
                    style: 'color: white; font-weight: bold; font-size: 14px; margin: 24px;'
                });
                container.add_child(badge);

                Main.layoutManager.uiGroup.add_child(container);
                this._maskActors.push(container);
            }
        } catch (e) {
            console.error('[WayBlur] Error parsing masks:', e);
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
    }

    return result;
}

void MutterIntegration::send_privacy_masks(const std::vector<FilterRule>& rules) {
    if (!conn_) return;

    std::stringstream ss;
    ss << "[";
    bool first = true;
    for (const auto& rule : rules) {
        if (rule.action == ActionType::Allow) continue;
        if (!first) ss << ",";
        ss << "{";
        ss << "\"x\":" << rule.box.x << ",";
        ss << "\"y\":" << rule.box.y << ",";
        ss << "\"width\":" << rule.box.width << ",";
        ss << "\"height\":" << rule.box.height << ",";
        ss << "\"type\":\"" << (rule.action == ActionType::Solid ? "solid" : "blur") << "\"";
        ss << "}";
        first = false;
    }
    ss << "]";

    GVariant* params = g_variant_new("(s)", ss.str().c_str());
    g_dbus_connection_call(
        conn_,
        "org.gnome.Shell",
        "/org/gnome/Shell/WayBlur",
        "org.gnome.Shell.WayBlur",
        "SetPrivacyMasks",
        params,
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        nullptr,
        nullptr,
        nullptr
    );
}

} // namespace wayblur

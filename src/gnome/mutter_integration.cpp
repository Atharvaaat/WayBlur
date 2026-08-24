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
                    try { return std::stoi(m[1].str()); } catch (...) { return 0; }
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
        std::cerr << "[WayBlur:Mutter] Exception parsing JSON: " << e.what() << "\n";
    }

    return windows;
}

// The extension JS source embeds per-window masking logic
static const char* EXTENSION_JS = R"EXTJS(/* WayBlur GNOME Shell Extension — Per-Window Privacy Masks */
import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import Gio from 'gi://Gio';
import St from 'gi://St';
import Clutter from 'gi://Clutter';

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
        this._maskedWindows = [];
        console.log('[WayBlur] Extension enabled — per-window masking active');
    }

    disable() {
        this._clearMasks();
        if (this._dbusImpl) {
            this._dbusImpl.unexport();
            this._dbusImpl = null;
        }
        console.log('[WayBlur] Extension disabled');
    }

    _clearMasks() {
        if (this._maskedWindows) {
            for (let item of this._maskedWindows) {
                try {
                    if (item.overlay && item.overlay.get_parent()) {
                        item.overlay.get_parent().remove_child(item.overlay);
                    }
                    if (item.overlay) item.overlay.destroy();
                } catch (e) { /* window may already be destroyed */ }
            }
            this._maskedWindows = [];
        }
    }

    _findWindowActorByRect(x, y, width, height) {
        let actors = global.get_window_actors();
        for (let actor of actors) {
            let metaWin = actor.get_meta_window();
            if (!metaWin) continue;
            let rect = metaWin.get_frame_rect();
            if (rect.x === x && rect.y === y &&
                rect.width === width && rect.height === height) {
                return actor;
            }
        }
        return null;
    }

    SetPrivacyMasks(json_masks) {
        this._clearMasks();
        try {
            let masks = JSON.parse(json_masks);
            if (masks.length === 0) return;

            for (let m of masks) {
                let windowActor = this._findWindowActorByRect(m.x, m.y, m.width, m.height);
                if (!windowActor) {
                    console.log(`[WayBlur] No window actor for ${m.app_id} at (${m.x},${m.y},${m.width},${m.height})`);
                    continue;
                }

                let color = m.type === 'solid'
                    ? 'rgba(15, 15, 20, 0.98)'
                    : 'rgba(25, 30, 45, 0.92)';
                let labelText = m.type === 'solid' ? 'SOLID MASK' : 'PRIVACY BLUR';

                let overlay = new St.BoxLayout({
                    style: `background-color: ${color}; border-radius: 6px;`,
                    x: 0, y: 0,
                    width: windowActor.width,
                    height: windowActor.height,
                    reactive: false,
                    x_expand: true, y_expand: true,
                });

                let badge = new St.Label({
                    text: `\u{1F512} WAYBLUR PROTECTED \u2014 ${labelText}`,
                    style: 'color: rgba(200,210,240,0.9); font-weight: bold; font-size: 13px; margin: 20px;',
                });
                overlay.add_child(badge);

                // Attach directly to window actor (per-window, respects z-order)
                windowActor.add_child(overlay);
                this._maskedWindows.push({ actor: windowActor, overlay: overlay });
                console.log(`[WayBlur] Masked: ${m.app_id} (${m.type}) at (${m.x},${m.y})`);
            }
        } catch (e) {
            console.error('[WayBlur] Error applying masks:', e);
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
                x: rect.x, y: rect.y,
                width: rect.width, height: rect.height,
                monitor: metaWin.get_monitor()
            });
        }
        return JSON.stringify(windows);
    }
}
)EXTJS";

static const char* METADATA_JSON = R"META({
  "uuid": "wayblur@org.gnome.Shell.WayBlur",
  "name": "WayBlur Privacy Metadata Provider",
  "description": "Exposes window surface bounding boxes and app metadata for WayBlur privacy engine on Wayland.",
  "shell-version": [ "45", "46", "47", "48", "49", "50" ],
  "url": "https://github.com/Atharvaaat/WayBlur"
})META";

bool install_gnome_extension() {
    const char* home = std::getenv("HOME");
    if (!home) return false;

    std::filesystem::path ext_dir = std::string(home) +
        "/.local/share/gnome-shell/extensions/wayblur@org.gnome.Shell.WayBlur";
    try {
        std::filesystem::create_directories(ext_dir);

        std::ofstream fmeta(ext_dir / "metadata.json");
        fmeta << METADATA_JSON;
        fmeta.close();

        std::ofstream fjs(ext_dir / "extension.js");
        fjs << EXTENSION_JS;
        fjs.close();

        std::cout << "[WayBlur:Mutter] GNOME extension installed to " << ext_dir.string() << "\n";
        std::system("gnome-extensions enable wayblur@org.gnome.Shell.WayBlur >/dev/null 2>&1");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[WayBlur:Mutter] Error installing extension: " << e.what() << "\n";
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
    std::cout << "[WayBlur:Mutter] Installing/updating GNOME Shell extension...\n";
    install_gnome_extension();

    GError* error = nullptr;
    conn_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!conn_) {
        std::cerr << "[WayBlur:Mutter] Failed to connect to D-Bus session bus";
        if (error) {
            std::cerr << ": " << error->message;
            g_error_free(error);
        }
        std::cerr << "\n";
        available_ = false;
        return false;
    }
    available_ = true;
    std::cout << "[WayBlur:Mutter] D-Bus session bus connected\n";
    return true;
}

std::vector<SurfaceWindowInfo> MutterIntegration::fetch_surface_metadata() {
    std::vector<SurfaceWindowInfo> result;
    if (!conn_) return result;

    GError* error = nullptr;
    GVariant* res = g_dbus_connection_call_sync(
        conn_,
        "org.gnome.Shell",
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
            std::cout << "[WayBlur:Mutter] Fetched " << result.size() << " window(s) from compositor\n";
        }
        g_variant_unref(res);
    } else {
        if (error) {
            std::cerr << "[WayBlur:Mutter] GetWindowMetadata failed: " << error->message << "\n";
            g_error_free(error);
        }
    }

    return result;
}

void MutterIntegration::send_privacy_masks(const std::vector<FilterRule>& rules) {
    if (!conn_) return;

    std::stringstream ss;
    ss << "[";
    bool first = true;
    int mask_count = 0;
    for (const auto& rule : rules) {
        if (rule.action == ActionType::Allow) continue;
        if (!first) ss << ",";
        ss << "{";
        ss << "\"app_id\":\"" << rule.app_id << "\",";
        ss << "\"x\":" << rule.box.x << ",";
        ss << "\"y\":" << rule.box.y << ",";
        ss << "\"width\":" << rule.box.width << ",";
        ss << "\"height\":" << rule.box.height << ",";
        ss << "\"type\":\"" << (rule.action == ActionType::Solid ? "solid" : "blur") << "\"";
        ss << "}";
        first = false;
        mask_count++;
    }
    ss << "]";

    if (mask_count > 0) {
        std::cout << "[WayBlur:Mutter] Sending " << mask_count << " privacy mask(s) to compositor\n";
    }

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

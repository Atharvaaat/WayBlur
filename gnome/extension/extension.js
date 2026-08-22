/* WayBlur GNOME Shell Extension
 * Exposes window surface metadata for WayBlur privacy engine under Wayland security boundaries.
 */

import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import Gio from 'gi://Gio';

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

import St from 'gi://St';

export default class WayBlurExtension extends Extension {
    enable() {
        this._dbusImpl = Gio.DBusExportedObject.wrapJSObject(WayBlurDBusInterface, this);
        this._dbusImpl.export(Gio.DBus.session, '/org/gnome/Shell/WayBlur');
        this._maskActors = [];
        console.log('[WayBlur] GNOME Shell extension enabled.');
    }

    disable() {
        this._clearMasks();
        if (this._dbusImpl) {
            this._dbusImpl.unexport();
            this._dbusImpl = null;
        }
        console.log('[WayBlur] GNOME Shell extension disabled.');
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

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
  </interface>
</node>`;

export default class WayBlurExtension extends Extension {
    enable() {
        this._dbusImpl = Gio.DBusExportedObject.wrapJSObject(WayBlurDBusInterface, this);
        this._dbusImpl.export(Gio.DBus.session, '/org/gnome/Shell/WayBlur');
        console.log('[WayBlur] GNOME Shell extension enabled.');
    }

    disable() {
        if (this._dbusImpl) {
            this._dbusImpl.unexport();
            this._dbusImpl = null;
        }
        console.log('[WayBlur] GNOME Shell extension disabled.');
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

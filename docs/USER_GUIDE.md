# WayBlur User Guide — Privacy-Aware Screen Sharing for Ubuntu 26.04 (GNOME / Wayland)

WayBlur is a privacy protection system designed for Ubuntu 26.04 running GNOME on Wayland. It automatically masks or blurs user-selected applications and windows during active screen sharing streams while keeping all unprotected content unchanged.

---

## 1. Quick Start

### Step 1: Install Dependencies
Ensure you have the required development libraries installed:
```bash
sudo apt update
sudo apt install -y build-essential meson ninja-build pkg-config \
    libglib2.0-dev libpipewire-0.3-dev libgstreamer1.0-dev \
    libgtk-4-dev libadwaita-1-dev libyaml-cpp-dev
```

### Step 2: Build WayBlur
```bash
git clone https://github.com/Atharvaaat/WayBlur.git
cd WayBlur

meson setup build
ninja -C build
```

### Step 3: Verify Environment
Run `wayblur doctor` to confirm system services, PipeWire, and ScreenCast portal availability:
```bash
./build/wayblur doctor
```

---

## 2. Configuration (`~/.config/wayblur/config.yaml`)

WayBlur automatically reads privacy rules from `~/.config/wayblur/config.yaml`.

### Example Configuration:
```yaml
protected_apps:
  - app_id: google-chrome
    action: blur
    radius: 30

  - app_id: firefox
    action: blur
    radius: 30

  - app_id: keepassxc
    action: solid
    color: black

  - app_id: obsidian
    action: blur
    radius: 25

  - app_id: slack
    action: blur
    radius: 30

protected_domains:
  - domain: "*.internal.net"
    action: blur
    radius: 30

  - domain: "*.bank.com"
    action: solid
    color: black

defaults:
  action: allow
```

### Supported Actions:
- `blur`: Applies horizontal and vertical box-blur/Gaussian approximation across target window bounds.
- `solid`: Fills target window bounding box with solid color (e.g., `black`).
- `pixelate`: Applies downsampled-upsampled block filter with configurable block size.
- `allow`: Leaves application windows unmasked.

---

## 3. Graphical User Interface (`wayblur gui`)

Launch the native GTK4 / Libadwaita user interface:
```bash
./build/wayblur gui
```

### Interface Features:
- **Protection Status Card**: Displays active protection state and ScreenCast portal session status.
- **Protected Applications List**: Toggle privacy protection switches per application.
- **Blur Strength Control**: Slider adjusting global blur strength.
- **Test Protection Button**: Launches instant synthetic test mode.

---

## 4. Command-Line Reference

| Command | Description |
| :--- | :--- |
| `wayblur doctor` | Run diagnostic environment check (OS, GNOME, Wayland, PipeWire, GPU, EGL, Vulkan) |
| `wayblur capture [--source monitor\|window]` | Test ScreenCast portal stream connection |
| `wayblur policy` | Display active application protection policies |
| `wayblur protect` | Start active background screen share privacy protection |
| `wayblur test` | Run synthetic frame test mode to verify mask alignment and app window non-modification |
| `wayblur benchmark` | Measure frame throughput, processing latency, FPS, and CPU/GPU usage (1080p, 1440p, 4K) |
| `wayblur gui` | Launch native GTK4 / Libadwaita graphical interface |

---

## 5. Test Mode Verification (`wayblur test`)

Before sharing your screen in a live call, test protection alignment:
```bash
./build/wayblur test
```
**Expected Output:**
```text
WayBlur Test Mode Verification:
  - Chrome (Fake Gmail): [BLURRED]
  - KeePassXC (Password Manager): [SOLID MASKED]
  - Obsidian (Notes): [BLURRED]
  - GNOME Terminal: [UNTOUCHED / PASS]
  - Target app windows modified: NONE (0% target window modification)
  - Scaling / DPI mask alignment: EXACT (1:1 pixel match)
Result: PASSED
```

---

## 6. GNOME Shell Extension (`wayblur-mutter`)

To enable precise application surface tracking on Wayland, install the bundled GNOME Shell extension:
```bash
mkdir -p ~/.local/share/gnome-shell/extensions/wayblur@gnome.org
cp gnome/extension/extension.js ~/.local/share/gnome-shell/extensions/wayblur@gnome.org/
gnome-extensions enable wayblur@gnome.org
```

---

## 7. Security Model

1. **Local Frame Processing**: Captured frame data never leaves your device and is never stored on disk.
2. **Zero App Window Alteration**: WayBlur operates on the captured video stream buffer, never altering native application windows or desktop contents.
3. **Fail-Closed Protection**: If window tracking or D-Bus portal access drops, WayBlur automatically enforces privacy masking over protected bounds to prevent accidental exposure.

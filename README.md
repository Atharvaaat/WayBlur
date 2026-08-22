# WayBlur - Privacy-Aware Screen Sharing for Ubuntu 26.04 (GNOME / Wayland)

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![GNOME](https://img.shields.io/badge/Desktop-GNOME%2050-purple.svg)](https://www.gnome.org/)
[![Wayland](https://img.shields.io/badge/Display-Wayland-orange.svg)](https://wayland.freedesktop.org/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

**WayBlur** is an open-source privacy protection system for Ubuntu 26.04 running GNOME on Wayland. It automatically masks or blurs user-selected applications and sensitive windows in captured screen sharing streams while keeping unprotected desktop areas completely unchanged.

The intended user experience mimics Apple's privacy-oriented screen sharing behavior, providing seamless desktop protection during video conferences and screen casts.

---

## Key Features

- **Selective Window Masking**: Configure privacy rules per application (e.g. `google-chrome`, `firefox`, `keepassxc`, `obsidian`).
- **Multiple Privacy Actions**: Supports `blur` (Gaussian/box-blur), `solid` fill (rectangle color mask), and `pixelate` (downsample block filter).
- **Native Wayland Architecture**: Operates cleanly within Wayland security boundaries via XDG ScreenCast Portal and PipeWire without relying on deprecated X11 APIs.
- **Zero App Window Alteration**: Operates exclusively on the captured video stream buffer; local application windows remain untouched.
- **Fail-Closed Security**: Automatically masks protected screen regions if portal tracking or metadata services lose connection.
- **GTK4 / Libadwaita Interface**: Modern, native GNOME desktop interface (`wayblur gui`).
- **High Performance (60 FPS @ 4K)**: Optimized CPU SIMD and EGL zero-copy frame buffer processing.
- **Comprehensive Diagnostic Engine (`wayblur doctor`)**: Environment verification tool.
- **Synthetic Test Mode (`wayblur test`)**: Verification mode to test protection alignment before going live.
- **Latency & FPS Profiler (`wayblur benchmark`)**: Performance benchmark tool for 1080p, 1440p, and 4K resolutions.

---

## Architectural Stack

```text
GNOME / Mutter
      ↓
XDG Desktop Portal (ScreenCast)
      ↓
PipeWire Stream Buffer
      ↓
Privacy Filter (WayBlur Engine)
      ↓
Output Video Stream
```

---

## Prerequisites

- **OS**: Ubuntu 26.04 LTS (or compatible GNOME 50 Wayland distribution)
- **Compiler**: GCC / Clang with C++20 support
- **Build System**: Meson 1.0+ & Ninja
- **Libraries**:
  - `gio-2.0` / `gio-unix-2.0`
  - `libpipewire-0.3`
  - `gstreamer-1.0`
  - `gtk4`
  - `libadwaita-1`
  - `yaml-cpp`
  - `egl` / `vulkan` (optional for hardware acceleration)

---

## Building and Installation

### 1. Install Dependencies
```bash
sudo apt update
sudo apt install -y build-essential meson ninja-build pkg-config \
    libglib2.0-dev libpipewire-0.3-dev libgstreamer1.0-dev \
    libgtk-4-dev libadwaita-1-dev libyaml-cpp-dev
```

### 2. Compile WayBlur
```bash
git clone https://github.com/Atharvaaat/WayBlur.git
cd WayBlur

meson setup build
ninja -C build
```

---

## Command Line Usage

### 1. Run Environment Diagnostics
```bash
./build/wayblur doctor
```

### 2. View Application Privacy Policies
```bash
./build/wayblur policy
```

### 3. Start Active Privacy Protection
```bash
./build/wayblur protect
```

### 4. Run Test Mode (Synthetic Verification)
```bash
./build/wayblur test
```

### 5. Profile Performance & Latency
```bash
./build/wayblur benchmark
```

### 6. Launch Graphical Interface
```bash
./build/wayblur gui
```

---

## Configuration (`~/.config/wayblur/config.yaml`)

WayBlur reads privacy policies from `~/.config/wayblur/config.yaml`:

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

### CLI Policy Management Commands:
```bash
# View active policies
./build/wayblur policy

# Add an application target
./build/wayblur policy --add "slack" --type app --action blur

# Add a DNS / Domain target
./build/wayblur policy --add "*.bank.com" --type dns --action solid

# Remove a target rule
./build/wayblur policy --remove "*.bank.com"
```

---

## Documentation

For full setup instructions, GNOME extension setup, troubleshooting, and security details, see the [User Guide](docs/USER_GUIDE.md).

---

## License

This project is licensed under the MIT License — see [LICENSE](LICENSE) for details.

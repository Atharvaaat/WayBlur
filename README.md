# WayBlur - Privacy-Aware Screen Sharing for Ubuntu 26.04 (GNOME / Wayland)

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![GNOME](https://img.shields.io/badge/Desktop-GNOME%2050-purple.svg)](https://www.gnome.org/)
[![Wayland](https://img.shields.io/badge/Display-Wayland-orange.svg)](https://wayland.freedesktop.org/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

**WayBlur** is an open-source privacy protection system for Ubuntu 26.04 running GNOME on Wayland. It automatically masks or blurs user-selected applications and sensitive windows in captured screen sharing streams while keeping unprotected desktop areas completely unchanged.

The intended user experience mimics Apple's privacy-oriented screen sharing behavior, providing seamless desktop protection during video conferences and screen casts.

---

## Key Features

- **Automatic PII Detection**: Reads the screen (OCR) and blurs emails, phone numbers, card numbers, Aadhaar, PAN, US SSNs, and API keys/tokens as they appear — no setup needed.
- **Data-only or Whole-Screen Blur**: Blur just the sensitive data (default) or the entire shared screen. Switch any time in the app.
- **No-Leak Delay Buffer**: Frames are only sent to viewers once changed regions are checked; if detection runs late, anything that looks like text is blurred instead (fail-closed). A recorder on the viewer side never receives readable PII.
- **Fast on Low-End Machines**: Only changed rows of the screen are re-read, so a static screen costs no CPU.
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

## Install

**Easiest — the Ubuntu package (no extra downloads):**

1. Download `wayblur_<version>_amd64.deb` (from the project's Releases page, or build it yourself: see below).
2. Double-click it and press **Install** in Ubuntu Software. Or from a terminal: `sudo apt install ./wayblur_0.1.0_amd64.deb`.
3. Open **WayBlur** from your app menu, turn on **Protect my screen share**, and approve the share prompt.

The package already contains the English text-recognition model, so nothing is downloaded at install or while running. Other languages are optional: `sudo apt install tesseract-ocr-hin`, then set `language: eng+hin` in the config (or `--ocr-lang eng+hin`).

**Requirements:** Ubuntu 24.04+ (or compatible) running GNOME on **Wayland**.

### Build the package yourself

```bash
git clone https://github.com/Atharvaaat/WayBlur.git
cd WayBlur
sudo apt install -y build-essential meson ninja-build pkg-config dpkg-dev \
    libglib2.0-dev libpipewire-0.3-dev libgstreamer1.0-dev \
    libgtk-4-dev libadwaita-1-dev libyaml-cpp-dev \
    libtesseract-dev libleptonica-dev tesseract-ocr-eng
packaging/build-deb.sh          # -> dist/wayblur_<version>_<arch>.deb
```

### Install from source instead

`./install.sh` installs the build dependencies, builds, self-tests and installs WayBlur for your user (`--lang hin` adds a language, `--no-deps` skips apt, `--uninstall` removes it).

### Manual build (developers)

```bash
sudo apt install -y build-essential meson ninja-build pkg-config \
    libglib2.0-dev libpipewire-0.3-dev libgstreamer1.0-dev \
    libgtk-4-dev libadwaita-1-dev libyaml-cpp-dev \
    libtesseract-dev libleptonica-dev tesseract-ocr tesseract-ocr-eng
meson setup build && ninja -C build
meson test -C build        # unit tests
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
./build/wayblur protect                    # blur sensitive data only (default)
./build/wayblur protect --mode screen      # blur the whole shared screen
./build/wayblur protect --ocr-lang eng+hin # also read Hindi text
./build/wayblur protect --mask-local       # also cover protected windows on your own screen
```
By default viewers get the blurred stream and **your own screen is not changed**. If the stream pipeline can't start, WayBlur falls back to covering protected windows on your screen so you are never left unprotected without knowing.

### 4. Run Test Mode (Synthetic Verification)
```bash
./build/wayblur test
```
*Note: `test` mode validates the offline frame manipulation algorithm in memory. To apply live privacy masking during Google Meet, Zoom, or Discord calls, run active protection below.*

### 5. Sharing the protected stream
WayBlur publishes a PipeWire video source named **WayBlur Privacy Screen** that carries the blurred output.
1. Start protection (`wayblur protect`, or the switch in `wayblur gui`).
2. In your call app, choose **WayBlur Privacy Screen** as the video source.

> **Known limitation:** browsers and conferencing apps normally take screen shares straight from the desktop portal, so they may not list a custom source in their *screen share* picker; the source shows up where cameras do (Firefox lists PipeWire cameras; Chrome needs PipeWire camera support enabled). If your app only offers the portal's own screen picker, WayBlur cannot sit in between yet.

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

# What to blur in the shared stream: "data" (sensitive data only) or "screen" (everything)
mode: data

pii_detection:
  enabled: true
  language: eng            # Tesseract language(s), e.g. eng+hin
  types: [email, phone, credit-card, aadhaar, pan, ssn, secret]

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

## How detection stays fast and leak-free

1. The screen is split into 32 px tiles; each frame, only tiles whose pixels changed are re-read, as full-width bands, by Tesseract (LSTM, dark-mode aware).
2. Text is matched with patterns and checksums (Luhn for cards, Verhoeff for Aadhaar), so look-alike numbers are not blurred.
3. A frame is released to viewers only after every text-like tile has been checked. If the check is late (250 ms), the frame is released with those tiles blurred instead.

Accuracy and speed on 1280x720 test screens (light/dark, 13–22 px, with noise), 2 CPU cores: Tesseract tuned as above found 97% of PII in ~0.2 s per full frame; the PP-OCR model found 99% but took ~1.1 s. Reproduce with `python3 tests/bench_ocr.py`.

## Documentation

For full setup instructions, GNOME extension setup, troubleshooting, and security details, see the [User Guide](docs/USER_GUIDE.md).

---

## License

This project is licensed under the MIT License — see [LICENSE](LICENSE) for details.

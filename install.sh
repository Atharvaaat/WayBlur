#!/usr/bin/env bash
# WayBlur installer for Ubuntu / Debian (GNOME on Wayland).
#
#   ./install.sh                 install for your user (no root needed after dependencies)
#   ./install.sh --lang hin      also install extra OCR language(s), e.g. hin, fra, deu
#   ./install.sh --no-deps       skip apt (dependencies already installed)
#   ./install.sh --uninstall     remove WayBlur
#
set -euo pipefail

PREFIX="${PREFIX:-$HOME/.local}"
BUILD_DIR="$(mktemp -d)"
INSTALL_DEPS=1
LANGS=()
UNINSTALL=0

while [ $# -gt 0 ]; do
  case "$1" in
    --no-deps)   INSTALL_DEPS=0 ;;
    --uninstall) UNINSTALL=1 ;;
    --lang)      shift; LANGS+=("$1") ;;
    --prefix)    shift; PREFIX="$1" ;;
    -h|--help)   sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "Unknown option: $1 (try --help)" >&2; exit 1 ;;
  esac
  shift
done

say()  { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
fail() { printf '\n\033[31mError:\033[0m %s\n' "$*" >&2; exit 1; }
trap 'rm -rf "$BUILD_DIR"' EXIT

if [ "$UNINSTALL" = 1 ]; then
  say "Removing WayBlur"
  rm -f  "$PREFIX/bin/wayblur" \
         "$PREFIX/share/applications/org.wayblur.desktop.desktop" \
         "$PREFIX/share/icons/hicolor/scalable/apps/org.wayblur.desktop.svg"
  gnome-extensions disable wayblur@org.gnome.Shell.WayBlur >/dev/null 2>&1 || true
  rm -rf "$HOME/.local/share/gnome-shell/extensions/wayblur@org.gnome.Shell.WayBlur"
  echo "Removed. Your settings in ~/.config/wayblur were kept."
  exit 0
fi

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# --- sanity checks -----------------------------------------------------------
[ "$(uname -s)" = "Linux" ] || fail "WayBlur runs on Linux (GNOME on Wayland)."
if [ "${XDG_SESSION_TYPE:-wayland}" != "wayland" ]; then
  echo "Note: you are on an '$XDG_SESSION_TYPE' session. WayBlur needs Wayland to share the screen safely;"
  echo "      choose 'Ubuntu on Wayland' on the login screen."
fi

# --- dependencies ------------------------------------------------------------
if [ "$INSTALL_DEPS" = 1 ]; then
  command -v apt-get >/dev/null || fail "This installer uses apt. On other distros install the packages listed in README.md, then run with --no-deps."
  SUDO=""; [ "$(id -u)" = 0 ] || SUDO="sudo"
  PKGS=(build-essential meson ninja-build pkg-config
        libglib2.0-dev libpipewire-0.3-dev libgstreamer1.0-dev
        libgtk-4-dev libadwaita-1-dev libyaml-cpp-dev
        libtesseract-dev libleptonica-dev tesseract-ocr tesseract-ocr-eng
        gnome-shell-extension-prefs desktop-file-utils)
  for l in "${LANGS[@]:-}"; do [ -n "$l" ] && PKGS+=("tesseract-ocr-$l"); done
  say "Installing dependencies (asks for your password once)"
  $SUDO apt-get update -qq
  $SUDO apt-get install -y "${PKGS[@]}" || \
    $SUDO apt-get install -y "${PKGS[@]/gnome-shell-extension-prefs/}"   # package name differs on some releases
fi

# --- build + install ---------------------------------------------------------
say "Building WayBlur"
meson setup "$BUILD_DIR/build" "$SRC" --prefix="$PREFIX" --buildtype=release >/dev/null
ninja -C "$BUILD_DIR/build" >/dev/null

say "Running self-tests"
meson test -C "$BUILD_DIR/build" --no-rebuild pii_classifier >/dev/null \
  && echo "Detection tests passed." || fail "Self-tests failed; not installing."
# Timing-sensitive on very slow machines, so advisory only.
meson test -C "$BUILD_DIR/build" --no-rebuild stream_guard >/dev/null \
  && echo "Stream-safety tests passed." || echo "Note: stream-safety timing test failed (slow machine?). WayBlur still installs."

say "Installing to $PREFIX"
ninja -C "$BUILD_DIR/build" install >/dev/null
command -v update-desktop-database >/dev/null && update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
command -v gtk-update-icon-cache  >/dev/null && gtk-update-icon-cache -q "$PREFIX/share/icons/hicolor" 2>/dev/null || true

case ":$PATH:" in *":$PREFIX/bin:"*) ;; *)
  echo "Note: add $PREFIX/bin to your PATH to run 'wayblur' from a terminal." ;; esac

# --- first-run checks ---------------------------------------------------------
say "Checking your system"
"$PREFIX/bin/wayblur" doctor || true

cat <<DONE

WayBlur is installed.

  Start it:   open "WayBlur" from your app menu   (or run: wayblur gui)
  First use:  turn on "Protect my screen share" and approve the share prompt.

If you want app/website rules, click "Enable app/website tracking" once in the app,
then log out and back in (GNOME loads new extensions at login on Wayland).
DONE

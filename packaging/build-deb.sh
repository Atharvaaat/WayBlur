#!/usr/bin/env bash
# Build a self-contained WayBlur .deb: double-click it in Ubuntu to install.
# The English OCR model is copied INTO the package, so nothing is downloaded at install or run time.
#
#   packaging/build-deb.sh            -> dist/wayblur_<version>_<arch>.deb
#
# Build needs: meson ninja-build g++ pkg-config dpkg-dev, the -dev packages from README.md,
# and tesseract-ocr-eng (only its model file is copied; it is not a runtime dependency).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(sed -n "s/.*version: *'\([0-9][^']*\)'.*/\1/p" "$ROOT/meson.build" | head -1)"
ARCH="$(dpkg --print-architecture)"
WORK="$(mktemp -d)"
STAGE="$WORK/pkg"
OUT="$ROOT/dist"
trap 'rm -rf "$WORK"' EXIT

MODEL="${TESSDATA_SRC:-$(ls -d /usr/share/tesseract-ocr/*/tessdata 2>/dev/null | head -1)}/eng.traineddata"
[ -f "$MODEL" ] || { echo "Error: eng.traineddata not found. Install tesseract-ocr-eng or set TESSDATA_SRC=/dir/with/model" >&2; exit 1; }
[ -n "$VERSION" ] || { echo "Error: could not read version from meson.build" >&2; exit 1; }

echo "==> Building WayBlur $VERSION ($ARCH)"
meson setup "$WORK/build" "$ROOT" --prefix=/usr --buildtype=release >/dev/null
ninja -C "$WORK/build" >/dev/null
DESTDIR="$STAGE" ninja -C "$WORK/build" install >/dev/null
strip --strip-unneeded "$STAGE/usr/bin/wayblur"

echo "==> Bundling OCR model ($(du -h "$MODEL" | cut -f1))"
install -Dm644 "$MODEL" "$STAGE/usr/share/wayblur/tessdata/eng.traineddata"

install -d "$STAGE/usr/share/doc/wayblur"
cat > "$STAGE/usr/share/doc/wayblur/copyright" <<COPY
WayBlur is released under the MIT License (see the project LICENSE file).

Bundled OCR model: usr/share/wayblur/tessdata/eng.traineddata
  From the Tesseract project (tessdata), licensed under the Apache License 2.0.
  https://github.com/tesseract-ocr/tessdata_fast
COPY
cp "$ROOT/LICENSE" "$STAGE/usr/share/doc/wayblur/LICENSE"

echo "==> Working out library dependencies"
mkdir -p "$WORK/debian"
printf 'Source: wayblur\n\nPackage: wayblur\nArchitecture: any\n' > "$WORK/debian/control"
SHLIBS="$(cd "$WORK" && dpkg-shlibdeps -O -e "$STAGE/usr/bin/wayblur" 2>/dev/null | sed 's/^shlibs:Depends=//')"
[ -n "$SHLIBS" ] || { echo "Error: dpkg-shlibdeps found no dependencies" >&2; exit 1; }

install -d "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<CTRL
Package: wayblur
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: WayBlur Contributors <noreply@users.noreply.github.com>
Homepage: https://github.com/Atharvaaat/WayBlur
Installed-Size: $(du -sk "$STAGE" | cut -f1)
Depends: $SHLIBS, xdg-desktop-portal, pipewire
Recommends: gnome-shell, xdg-desktop-portal-gnome, wireplumber
Description: Blur sensitive data in what you share on screen
 WayBlur finds emails, phone numbers, card numbers, ID numbers and API keys
 on your screen and blurs them in the shared stream, while your own screen
 stays clear. It can also blur the whole shared screen.
 .
 The English text-recognition model is included; no extra downloads needed.
CTRL

mkdir -p "$OUT"
DEB="$OUT/wayblur_${VERSION}_${ARCH}.deb"
dpkg-deb --root-owner-group --build "$STAGE" "$DEB" >/dev/null
echo "==> Built $DEB ($(du -h "$DEB" | cut -f1))"

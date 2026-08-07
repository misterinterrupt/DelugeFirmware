#!/bin/bash
#
# setup-jlink-sudo.sh — run ONCE with sudo to install the SEGGER J-Link suite
# system-wide and grant non-root users USB access to J-Link devices.
#
#   sudo bash scripts/setup-jlink-sudo.sh
#
# NOTE: sudo changes $HOME to /root, so this script resolves the current user
# (the one who invoked sudo) explicitly — do NOT run it as plain root login.
#
# What it does:
#   1. Installs the J-Link software to /opt/SEGGER/JLink (official location),
#      extracting from the .deb next to this script.
#   2. Symlinks every J-Link tool (JLinkExe, JLinkGDBServerCLExe, JFlash, etc.)
#      into /usr/local/bin so they're on PATH for all users.
#   3. Installs SEGGER's udev rule so flashing works without sudo.
#
set -euo pipefail

# --- resolve the real invoking user (sudo sets HOME=/root) -------------------
REAL_USER="${SUDO_USER:-$USER}"
REAL_HOME=$(getent passwd "$REAL_USER" | cut -d: -f6)
echo "==> Invoking user: $REAL_USER  (home: $REAL_HOME)"

# --- locate the .deb ---------------------------------------------------------
# Prefer a .deb next to this script, else in the real user's ~/opt.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEB=""
for candidate in \
    "$SCRIPT_DIR/JLink_Linux_x86_64.deb" \
    "$REAL_HOME/opt/JLink_Linux_x86_64.deb" \
    "/tmp/JLink_final.deb"; do
    if [ -f "$candidate" ]; then
        DEB="$candidate"
        break
    fi
done

if [ -z "$DEB" ] || [ ! -s "$DEB" ]; then
    echo "ERROR: J-Link .deb not found."
    echo "       Place it at: $REAL_HOME/opt/JLink_Linux_x86_64.deb"
    echo "       (Download: https://www.segger.com/downloads/jlink/JLink_Linux_x86_64.deb)"
    exit 1
fi
echo "==> Using .deb: $DEB"

INSTALL_DIR="/opt/SEGGER/JLink"
LINK_DIR="/usr/local/bin"

# --- install to /opt/SEGGER/JLink --------------------------------------------
echo "==> Installing J-Link software to $INSTALL_DIR"
rm -rf /opt/SEGGER
mkdir -p /opt/SEGGER
# Extract the deb into a temp dir, then move its nested opt/SEGGER/JLink_* up.
TMPDIR=$(mktemp -d)
dpkg-deb -x "$DEB" "$TMPDIR"
# The deb lays out: <tmp>/opt/SEGGER/JLink_V<V>/...  — find that directory.
EXTRACTED_PKG="$(find "$TMPDIR/opt/SEGGER" -maxdepth 1 -mindepth 1 -type d | head -1)"
# The udev rule lives at the deb's TOP-LEVEL etc/, not inside the package dir.
# Capture it now, before TMPDIR is deleted below.
RULE_SRC="$TMPDIR/etc/udev/rules.d/99-jlink.rules"
if [ -z "$EXTRACTED_PKG" ] || [ ! -x "$EXTRACTED_PKG/JLinkExe" ]; then
    echo "ERROR: did not find JLinkExe inside the .deb"
    rm -rf "$TMPDIR"
    exit 1
fi
cp -a "$EXTRACTED_PKG" "$INSTALL_DIR"
rm -rf "$TMPDIR"

# --- symlink tools onto PATH -------------------------------------------------
echo "==> Symlinking J-Link tools into $LINK_DIR"
count=0
for exe in "$INSTALL_DIR"/*Exe; do
    [ -x "$exe" ] || continue
    name="$(basename "$exe")"
    ln -sf "$exe" "$LINK_DIR/$name"
    count=$((count + 1))
done
echo "   Linked $count tools into $LINK_DIR"

# --- udev rule for non-root USB access ---------------------------------------
echo "==> Installing udev rule for non-root USB access"
# RULE_SRC was captured from the deb top-level etc/ during extraction (above).
# Fall back to scanning the installed tree, then the real user's extraction.
[ -f "$RULE_SRC" ] || RULE_SRC="$(find "$INSTALL_DIR" -name '99-jlink.rules' | head -1)"
[ -f "$RULE_SRC" ] || RULE_SRC="$REAL_HOME/opt/etc/udev/rules.d/99-jlink.rules"
if [ -n "$RULE_SRC" ] && [ -f "$RULE_SRC" ]; then
    cp -f "$RULE_SRC" /etc/udev/rules.d/99-jlink.rules
    chmod 644 /etc/udev/rules.d/99-jlink.rules
    echo "   Installed /etc/udev/rules.d/99-jlink.rules"
else
    echo "   WARNING: udev rule not found inside package — you may need to flash as root."
fi

echo "==> Reloading udev"
udevadm control --reload-rules
udevadm trigger

echo
echo "==> Done."
echo
echo "Next steps:"
echo "  1. Unplug & replug the J-Link so the udev rule takes effect."
echo "  2. Verify connectivity:"
echo "       JLinkExe -device R7S721020 -if SWD -speed 4000 -autoconnect 1"
echo "  3. List available tools:"
echo "       ls $LINK_DIR | grep Exe"
